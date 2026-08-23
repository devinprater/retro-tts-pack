# -*- coding: utf-8 -*-
"""Run MacinTalk and hand back PCM.

The whole sequence, and why each step is there, is in docs/driver-api.md,
docs/sound-model.md and docs/frame-format.md. The short version:

    Open                      allocates dCtlStorage, loads TALK 1
    driver+$0034              install the per-frame callback  <- load-bearing
    driver+$001E              hand over the channel and two buffers
    Prime (_Write)            speak; PCM arrives at every bufferCmd

One instance owns one emulator, and **there can only be one**. The host DLL is
a single CPU with global state, so constructing a second Engine resets the
first out from under it -- silently, and with no error to notice. Every call
into it must also come from the same thread; see the worker in
`synthDrivers/outspoken.py`. The one exception is `stop()`, which writes a
single byte the callback polls, and a lone `osp_w8` is safe from outside.
"""
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import osp                                                    # noqa: E402
import nrl                                                    # noqa: E402
import numwords                                               # noqa: E402

DRV_BASE = 0x00040000
HEAP, HEAP_SIZE = 0x00080000, 0x00080000
STACK = 0x00200000
WORK = 0x00190000

CPUFLAG, RESERR = 0x012F, 0x0A60
EXPORT_MACSTARTSOUND = 0x001E
EXPORT_SET_CALLBACK = 0x0034
BUF_BYTES = 22 + 3870

FLAG = WORK + 0x280            # our stop flag, polled by the hook
HOOK = WORK + 0x200

#: The rate the driver writes into its own SoundHeader.
NATIVE_RATE = 22254.5454545


#: Silence is 0x80; ClearBuffers also writes 0x60 into the classic frame, and a
#: fresh utterance opens with a single 0x40. All three are padding.
_PAD = (0x80, 0x60, 0x40)

#: A few milliseconds of ramp at each end. Once the padding is gone the audio
#: starts and stops at whatever amplitude it happened to reach, and a step from
#: silence to that value is a click -- heard, in Tomi's words, as raindrops.
_FADE = 90                      # samples, about 4 ms at 22254 Hz

#: A short, fixed gap around each utterance -- MOSTLY IN FRONT.
#:
#: The engine's own trailing padding is 28 to 149 ms depending on where its
#: last buffer happened to end, which is both wasteful and uneven, so it is
#: trimmed and this goes back in its place.
#:
#: The split matters. A gap on the END is the first thing an interruption
#: destroys: type a letter while "space" is still playing and cancel() stops
#: the player mid-word, tail and all, so the letter begins instantly and the
#: two run together -- which is exactly what "space attaches to the next
#: keypress" sounds like. A gap at the START belongs to the new utterance and
#: survives, because it plays after the stop rather than before it.
#:
#: Kept small at the front so it costs little latency, with the remainder
#: behind for the uninterrupted case. Worth exposing as a "shorten pauses"
#: setting later; these two numbers are the whole mechanism.
_LEAD_MS = 70
_TAIL_MS = 40
_LEAD = int(22254.5454 * _LEAD_MS / 1000.0)
_TAIL = int(22254.5454 * _TAIL_MS / 1000.0)

#: One buffer's worth of silence, for wiping between utterances.
_SILENCE = bytes([0x80]) * 3870


def _tidy(pcm):
    """Strip the engine's padding and ramp the edges.

    Leading silence is up to ~157 ms after a cancel; trailing silence is 28 to
    149 ms because the driver pads its last buffer rather than shortening it
    (docs/sound-model.md). On a half-second letter that padding is a third of
    the duration, and with typing echo it is dead air between every keystroke.
    """
    n = len(pcm)
    i = 0
    while i < n and pcm[i] in _PAD:
        i += 1
    j = n
    while j > i and pcm[j - 1] in _PAD:
        j -= 1
    if j - i < 2:
        return b""
    out = bytearray(pcm[i:j])
    fade = min(_FADE, len(out) // 2)
    for k in range(fade):
        g = k / float(fade)
        out[k] = 128 + int((out[k] - 128) * g)
        out[-1 - k] = 128 + int((out[-1 - k] - 128) * g)
    return bytes([0x80]) * _LEAD + bytes(out) + bytes([0x80]) * _TAIL


#: The live engine, if any. See the class docstring: a second one resets the
#: first, so it is worth noticing rather than debugging later.
_LIVE = []


class Engine(object):
    def __init__(self, rom):
        """`rom` maps file name -> path, as `rom.find()` returns."""
        if _LIVE:
            # NVDA builds a new SynthDriver when the user switches synthesizer
            # and back, and osp_init() resets the emulator's global state, so
            # any older instance is quietly invalidated from here on.
            try:
                from logHandler import log
                log.warning("outSPOKEN: a second engine was created; "
                            "the previous one is no longer valid")
            except Exception:
                pass
        for old in _LIVE:
            old._dead = True             # it cannot safely touch the CPU now
        _LIVE.append(self)
        self._dead = False
        self._speaking = False
        image = open(rom["DRVR_1030.bin"], "rb").read()
        self._entries = osp.driver_entries(image)
        self.h = h = osp.Host()
        h.load(DRV_BASE, image)
        h.heap(HEAP, HEAP_SIZE)
        h.mem_traps(True)
        h.w8(CPUFLAG, 0)
        h.w16(RESERR, 0)
        h.add_resource("TALK", 1, open(rom["TALK_1001.bin"], "rb").read())

        self.rules = nrl.Rules(open(rom["RULZ_1129.bin"], "rb").read()) \
            if "RULZ_1129.bin" in rom else None

        # Berkeley's exception list. Optional: without it the engine still
        # speaks, it just says "sea-rch" for SEARCH, which is genuinely what
        # the 1984 rules produce.
        self.dictionary = None
        if "DICT_-4048.bin" in rom:
            try:
                self.dictionary = nrl.Dictionary(
                    open(rom["DICT_-4048.bin"], "rb").read())
            except Exception:
                self.dictionary = None

        self._dce, self._pb = WORK, WORK + 0x100
        self._open()
        self._install_hook()
        self._start_sound()

    # -- set-up ------------------------------------------------------------
    def _open(self):
        h, dce, pb = self.h, self._dce, self._pb
        for off in range(0, 0x80, 4):
            h.w32(dce + off, 0)
            h.w32(pb + off, 0)
        h.w32(dce + 0, DRV_BASE)
        h.w16(dce + 4, 0x4600)
        h.w16(dce + 24, 0xFFEF)
        h.w16(pb + 24, 0xFFEF)
        h.set_reg(osp.SR, 0x2700)
        h.set_reg(osp.A7, STACK)
        h.set_reg(osp.A0, pb)
        h.set_reg(osp.A1, dce)
        if h.call(DRV_BASE + self._entries[0], max_instr=5_000_000) != 1:
            raise RuntimeError("MacinTalk Open did not return")

    def _install_hook(self):
        """The per-frame callback -- see docs/frame-format.md.

        It is not a notification. It reads f[0] and f[1] of every frame, and
        bit 7 of f[0] ends the utterance. It also polls our stop flag, which is
        how `cancel()` works: returning with N set is the engine's own designed
        way to stop, which is why the export is called SetStopSpeechCallback.
        """
        h = self.h
        for i, w in enumerate((
                0x4A39, (FLAG >> 16) & 0xFFFF, FLAG & 0xFFFF,  # tst.b FLAG.l
                0x660E,                                        # bne.s -> stop
                0x1B5E, 0x0001,                # move.b (a6)+, $1(a5)
                0x1B5E, 0x0003,                # move.b (a6)+, $3(a5)
                0x4A2D, 0x0001,                # tst.b  $1(a5)  -- restore N
                0x4E75,                        # rts
                0x70FF,                        # moveq #-1, d0   (N set = stop)
                0x4E75)):                      # rts
            h.w16(HOOK + 2 * i, w)
        h.w8(FLAG, 0)
        h.set_reg(osp.A7, STACK)
        if h.call_with_args(DRV_BASE + EXPORT_SET_CALLBACK, [HOOK],
                            max_instr=1000) != 1:
            raise RuntimeError("could not install the speech callback")

    def _start_sound(self):
        h = self.h
        chan, bufa, bufb, rec = (WORK + 0x400, WORK + 0x1000,
                                 WORK + 0x3000, WORK + 0x300)
        self._bufs = (bufa, bufb)
        for off in range(0, 0x80, 4):
            h.w32(chan + off, 0)
        # ChannelBusy short-circuits on chan+$20 == -1 and reports idle without
        # asking the Sound Manager, which is exactly our model: buffers are
        # consumed the instant they are handed over.
        h.w16(chan + 0x20, 0xFFFF)
        for base in (bufa, bufb):
            for off in range(0, BUF_BYTES + 4, 4):
                h.w32(base + off, 0)
        h.w32(rec + 0, chan)
        h.w32(rec + 4, bufa)
        h.w32(rec + 8, bufb)
        h.set_reg(osp.A7, STACK)
        h.set_reg(osp.A1, self._dce)
        if h.call_with_args(DRV_BASE + EXPORT_MACSTARTSOUND, [rec],
                            max_instr=10_000_000) != 1:
            raise RuntimeError("MACSTARTSOUND failed")

    # -- settings ----------------------------------------------------------
    def _storage(self):
        """dCtlStorage is a HANDLE at DCE+$14; writing through the handle
        itself changes nothing at all and is very quiet about it."""
        return self.h.r32(self.h.r32(self._dce + 0x14))

    def set_voice(self, pitch_hz):
        s = self._storage()
        self.h.w16(s + 0x30, max(65, min(500, int(pitch_hz))))

    def read_settings(self):
        """What the engine is actually holding, not what we asked for.

        Worth reading back rather than trusting: a letter measures twice as
        long inside NVDA as outside it at a nominally identical rate, and only
        one of those two numbers can be true.
        """
        s = self._storage()
        return (self.h.r16(s + 0x30), self.h.r16(s + 0x32))

    def set_rate(self, rate):
        s = self._storage()
        self.h.w16(s + 0x32, max(40, min(2560, int(rate))))

    #: **1984 has no inflection control and the slider cannot pretend it
    #: does.** `Control` takes four csCodes and that is the whole of the
    #: driver's settings: a mode toggle, a rate, a voice bank and a pitch in
    #: hertz (docs/driver-api.md). There is no fifth.
    #:
    #: Flat intonation was looked for and not found. The contour is a per-frame
    #: divisor the engine computes for itself -- `move.l $34(a5), d7 /
    #: divu.w d5, d7` -- so holding `d5` still would be a monotone mode, but
    #: nothing in the driver's interface reaches it. Dropping the stress digits
    #: from the phoneme string narrows the spread without flattening it
    #: (sd 53.8 -> 48.6 Hz), so the stress marks contribute rather than cause.
    #:
    #: Present rather than absent so the driver can call it on every engine
    #: without asking which one it has -- and named so that anyone who finds
    #: the mechanism knows where it goes.
    def set_inflection(self, percent):
        pass

    # -- speaking ----------------------------------------------------------
    #: How to read digits.  None means "leave them to the engine", which spells
    #: them out one at a time because that is all `RULZ` bucket 26 can do.
    number_mode = "words"

    def translate(self, text):
        if self.rules is None:
            return text
        t = text.strip()
        if len(t) == 1 and t.isalpha():
            # Typing echo sends one character, and it wants the letter's NAME.
            return nrl.letter_name(t, self.rules)
        if self.number_mode in ("words", "digits"):
            # Before the rules, never inside them: `30` has to become the word
            # "thirty" while it is still English, because the rules only ever
            # see letters. See numbers.py for why this is an addition rather
            # than a restoration.
            text = numwords.normalise(
                text, spell_out=(self.number_mode == "digits"))
        if self.dictionary is not None:
            # Respell first, then apply the rules -- the dictionary's right-hand
            # side is English, not phonemes. That is how Berkeley fixed
            # pronunciation without touching the 1984 rule set, and it is why
            # SEARCH is listed there as SERCH rather than as SER4CH.
            text = nrl.respell(text, self.dictionary)
        return nrl.translate(text, self.rules)

    def close(self):
        """Retire this engine. Any later call is a no-op rather than a fault.

        Unloads the DLL as well, so switching to another synthesizer releases
        the file instead of locking it for the rest of NVDA's life. See
        osp.Host.close.
        """
        self._dead = True
        try:
            _LIVE.remove(self)
        except ValueError:
            pass
        try:
            self.h.close()
        except Exception:
            pass

    def stop(self):
        """Interrupt the utterance in flight, if there is one.

        Only while actually synthesising. The flag is polled by the frame
        callback and aborts whatever Prime is doing, so setting it while the
        engine is idle poisons the NEXT utterance instead of the current one.
        Holding a key down makes NVDA cancel continuously, and the engine then
        rendered nothing at all, over and over -- 'spoken' frozen at 392 while
        'rendered-empty' climbed. Heard as speech disappearing until the
        synthesizer was switched away and back.
        """
        if self._dead or not self._speaking:
            return
        try:
            self.h.w8(FLAG, 1)
        except Exception:
            pass

    def speak(self, phonemes):
        """-> 8-bit unsigned PCM at NATIVE_RATE, leading silence trimmed."""
        if self._dead:
            # A newer Engine has reset the emulator's global state; driving the
            # CPU from here would run against memory that is no longer ours.
            return b""
        h, pb = self.h, self._pb
        h.w8(FLAG, 0)
        # Wipe the sound buffers first.
        #
        # MACSTARTSOUND is handed two buffers once and the engine reuses them
        # for every utterance, filling each from the start and then handing
        # over its whole declared length. Anything the new utterance has not
        # reached yet is still the PREVIOUS one's speech, so a short phrase
        # arrives with the tail of the one before it stuck on the front.
        #
        # Measured: "space" is 9,761 samples on its own and 14,982 straight
        # after "a" -- and the 5,221 difference is almost exactly the 5,001
        # samples "a" takes. Heard as one utterance running into the next, and
        # as latency, because what you hear first is the thing you asked for
        # last time.
        for base in self._bufs:
            h.load(base + 22, _SILENCE)
        # A trailing space and a cleared run after it are both load-bearing.
        # The parser reads past the text it was given: "IY4" alone renders
        # nothing at all and returns after 408 instructions, while "IY4 " gives
        # 14,904 samples. Worse, without the clear it reads whatever the
        # previous, longer utterance left behind -- which is heard as fragments
        # of another word bleeding onto the end of a short one.
        raw = phonemes.strip().encode("mac-roman", "replace")
        if not raw:
            return b""
        raw += b" "
        txt, txt_h = WORK + 0x8000, WORK + 0x7000
        h.load(txt, raw + b" " * 64)
        h.w32(txt_h, txt)
        h.w32(pb + 32, txt_h)          # ioBuffer is a HANDLE, not a pointer
        h.w32(pb + 36, len(raw))
        h.w32(pb + 40, 0)
        h.w16(pb + 16, 1)
        h.pcm_reset()
        h.set_reg(osp.A7, STACK)
        h.set_reg(osp.A0, pb)
        h.set_reg(osp.A1, self._dce)
        self._speaking = True
        try:
            h.call(DRV_BASE + self._entries[1], max_instr=400_000_000)
        finally:
            # Clear it on the way out as well as on the way in, so a stop that
            # lands late cannot survive into the next utterance.
            self._speaking = False
            h.w8(FLAG, 0)
        return _tidy(h.pcm)
