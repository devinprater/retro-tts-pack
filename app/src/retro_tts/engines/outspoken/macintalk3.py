# -*- coding: utf-8 -*-
"""Classic MacinTalk 3 as an engine the driver can drive.

The 1994 68k build, running as 68k -- not a native port. Same shape as
`macintalk2.py` (`translate`, `speak`, `stop`, `set_rate`, `set_pitch`,
`close`) and the same `'ttsc'` Component Manager protocol, so the host glue
carries over unchanged. What differs is worth reading before touching anything:

* **It requires a 68040.** `ttvi 10 +0x234` is `rtd`, which is 68010 and up,
  and only a 68040 answers Open with 0.
* **The engine hides in a `ttvi`.** Apple named these resources after
  composers, so the component code is `ttvi` 10 -- "Bach" -- and the type that
  means "voice info" for every other engine here means "the engine" for this
  one. `ttvi` 8 and 9 are more of it; `ttvi` 11 is the **PowerPC** build of the
  same engine and must never be registered.
* **A voice is usually nothing but a parameter set.** MacinTalk 3 is formant,
  so Fred is a 714-byte `ttvd` with no companion resource at all. Nine of the
  nineteen -- the singing and novelty voices -- also carry a `ttvw`, and the
  engine refuses them outright with -192 without it. See `voices.VOICE_PARTS`.
* **All nineteen fit at once.** 17 engine resources plus 19 `ttvd` plus 9
  `ttvw` is 45 against the host's 64, and 19 voices against 32, so every voice
  is registered up front and switching is a single SetSpeechInfo('cvox') --
  the MacinTalk 2 model rather than Pro's one-voice-per-instance.

**The bug that hid this engine for days is worth remembering.** It rendered
two buffers and then ran away into four hundred million bus faults, and the
recorded diagnosis -- that the host needed a Sound Manager command queue --
was wrong; the host had been copying commands all along. It was
`_Microseconds`, which returns its count in the A0/D0 pair and writes no
memory. The host had stored eight bytes through A0, which held whatever the
caller left there; here that was the engine's own SndCommand. The clock was
corrupting the thing it was timing. See `tools/probe_mt3_open.py`.
"""
import ctypes
import os
import sys

_HERE = os.path.dirname(os.path.abspath(__file__))
if _HERE not in sys.path:
    sys.path.insert(0, _HERE)

import osp                                                    # noqa: E402
import numwords                                               # noqa: E402
import voices as voicelib                                     # noqa: E402
import ospaudio                                              # noqa: E402

CODE = 0x00040000
HEAP = 0x00080000
HEAP_SIZE = 0x00200000
STACK = 0x00400000
TEXT_BUF = 0x00410000
VOICE_SPEC = 0x00411000
STATUS_BUF = 0x00412000
PARAM_BUF = 0x00413000

CPUFLAG, RESERR, MEMERR = 0x012F, 0x0A60, 0x0220

#: The classic Macintosh rate, as every other engine here renders at.
NATIVE_RATE = 22254

OPEN, CLOSE = -1, -2
#: The same selector map MacinTalk 2 and MacinTalk Pro use.
STATUS, SPEAK, STOP, GET_INFO, SET_INFO = 0, 1, 2, 5, 6

SO_CURRENT_VOICE = 0x63766F78          # 'cvox'
SO_RATE = 0x72617465                   # 'rate'
SO_PITCH_BASE = 0x70626173             # 'pbas'
SO_PITCH_MOD = 0x706D6F64              # 'pmod'

#: The depth given at the top of the slider to a voice that has none of its
#: own, in 'pmod' units. Chosen between Albert's 12.500 and Fred's 50.000, so
#: a robot asked for intonation gets a middling amount of it rather than a
#: caricature. Nothing below the midpoint uses this: at 50 those voices are
#: still exactly as Apple shipped them.
INFLECTION_REFERENCE = 25.0

#: The engine's own resources. `ttvi 11` is the PowerPC build of the same
#: engine: registering it wastes a slot at best and is loaded at worst.
ENGINE_TYPES = ("ttvi", "ttss", "ttsp", "STR ", "vers")
SKIP = (("ttvi", 11),)

REQUIRED = ("ttvi_10.bin",)

#: **The gate, and it is open.** 2026-08-21: Tomi listened to Fred, Kathy,
#: Princess and Ralph and said "all sound perfect and clear, no choppyness,
#: nothing like that at all".
#:
#: Before that it stayed False on a branch while the engine opened, took a
#: voice and rendered 0.20 s of real audio before running away -- because
#: listing a voice that then says nothing is the one failure this project
#: treats as worse than not listing it at all. See `SynthDriver.check`.
#:
#: `py -3 tools/probe_mt3_open.py --speak "..."` writes the WAV, and a person
#: hearing it is what this flag means. Do not flip it back on a passing test.
SPEAKS = True

_LIVE = []


def _fixed(x):
    """A Fixed 16.16, which is how the Speech Manager passes rate and pitch."""
    return int(round(x * 65536.0)) & 0xFFFFFFFF


def _unfixed(u):
    if u & 0x80000000:
        u -= 1 << 32
    return u / 65536.0


def _signed(v):
    return v - 0x100000000 if v & 0x80000000 else v


def engine_dir(roots):
    for root in roots:
        d = os.path.join(root, "macintalk3")
        if os.path.isdir(d) and os.path.isfile(os.path.join(d, "ttvi_10.bin")):
            return d
    return None


def find(roots):
    """-> (engine folder, [Voice]) for whatever of MacinTalk 3 is installed.

    Empty rather than raising when it is absent: a user with only the older
    engines must still get a working synthesizer.
    """
    d = engine_dir(roots)
    if not d:
        return None, []
    found, _bad = voicelib.installed("mtk3", roots=roots, speakable=True)
    return d, found


def usable(roots):
    """Also checks the DLL can actually do components.

    A stale osp_host.dll is easy to end up with, because NVDA holds it open
    while the synthesizer is loaded and it cannot be replaced in place.
    Listing voices against a binary that cannot run them would offer the user
    voices guaranteed to be silent.
    """
    if not SPEAKS:
        return False
    d, found = find(roots)
    if not (d and found):
        return False
    try:
        return hasattr(ctypes.CDLL(osp.DLL), "osp_add_component")
    except Exception:
        return False


class Engine(object):
    """One open MacinTalk 3, with every installed voice registered."""

    number_mode = "words"

    #: `speak` takes a `sink` and hands audio over as it renders. Declared so
    #: the driver never has to guess from a signature or catch a TypeError,
    #: which would also swallow one raised inside a render.
    #:
    #: This engine is the one that needs it: about 24x realtime against
    #: MacinTalk 2's 157x, so a long sentence cost the best part of a second
    #: before anything could be played. The others may follow once this has
    #: been heard in NVDA.
    STREAMS = True

    #: A ceiling on one utterance. Reaching it means the engine is producing
    #: without ever finishing, which is a finding rather than an utterance --
    #: so it must sit well above anything real. About 70 seconds here, since
    #: this engine's buffers are 2237 bytes; counted in seconds rather than
    #: buffers because the same count meant 23 s on MacinTalk Pro, where long
    #: paragraphs were being truncated mid-word.
    MAX_BUFFERS = 700

    def __init__(self, folder, allvoices, voice=None):
        for old in _LIVE:
            old._dead = True
        _LIVE.append(self)
        self._dead = False
        self.voices = list(allvoices)
        self.voice = voice or self.voices[0]
        self._rate = None
        self._pitch = 0
        self._base_pitch = None
        self._mod = 50
        self._base_mod = None

        h = self.h = osp.Host()
        # Not optional, and not a preference: `+0x234` is `rtd`, so a 68000
        # refuses to Open, and the synthesis path uses instructions a 68020
        # does not have either.
        h.set_cpu(osp.Host.CPU_68040)
        with open(os.path.join(folder, "ttvi_10.bin"), "rb") as fh:
            h.load(CODE, fh.read())
        h.heap(HEAP, HEAP_SIZE)
        h.mem_traps(True)
        h.w8(CPUFLAG, 0)
        h.w16(RESERR, 0)
        h.w16(MEMERR, 0)

        for name in sorted(os.listdir(folder)):
            split = _split(name)
            if not split or split in SKIP or split[0] not in ENGINE_TYPES:
                continue
            with open(os.path.join(folder, name), "rb") as fh:
                h.add_resource(split[0], split[1], fh.read())

        # Every voice's resources, under the ids its own ttvd asks for. A voice
        # that will not load is dropped rather than fatal: one bad extraction
        # must not cost the user the other eighteen.
        loaded = []
        for v in self.voices:
            try:
                ttvd = None
                for kind, path in sorted(v.files.items()):
                    if kind not in ("ttvd", "ttvw"):
                        continue          # `vers` is metadata, not a resource
                    rid = int(os.path.basename(path).split("_")[1].split(".")[0])
                    with open(path, "rb") as fh:
                        h.add_resource(kind, rid, fh.read())
                    if kind == "ttvd":
                        ttvd = rid
                if ttvd is None:
                    continue
                # We are the Speech Manager: GetVoiceInfo('fref') answers with
                # the ttvd id, which is what the engine opens a voice by. The
                # two are not the same number -- Bubbles is VoiceSpec id 50 in
                # a resource numbered 12 -- so neither may be assumed for the
                # other.
                h.add_voice(v.creator, v.id, ttvd)
                loaded.append(v)
            except Exception:
                continue
        self.voices = loaded
        if not loaded:
            raise RuntimeError("no MacinTalk 3 voice could be loaded")
        if self.voice not in loaded:
            self.voice = loaded[0]

        self.comp = h.add_component("ttsc", "mtk3", "mtk3", CODE)
        self.chan = h.open_instance(self.comp)
        h.set_reg(osp.A7, STACK)
        h.set_reg(osp.SR, 0x2700)
        # Not optional either. The engine's callback installs a deferred task
        # and queues the next command; running callbacks inside the call runs
        # them against state SpeakBuffer has already moved past.
        h.defer_callbacks(True)

        reason, result = h.component_call(self.chan, OPEN, [self.chan],
                                          max_instr=50_000_000)
        if reason != 1 or _signed(result) != 0:
            self._dead = True
            raise RuntimeError("MacinTalk 3 would not open (%s, result %d)"
                               % (osp.STOP[reason], _signed(result)))
        if not self.select(self.voice):
            raise RuntimeError("MacinTalk 3 would not take voice %s"
                               % self.voice.name)

    # -- set-up ------------------------------------------------------------
    def _match(self, voice):
        for v in self.voices:
            if v.creator == voice.creator and v.id == voice.id:
                return v
        return None

    def select(self, voice):
        """Switch voice without rebuilding: SetSpeechInfo('cvox', VoiceSpec).

        -> True if the engine took it. A refusal leaves the previous voice in
        place, which is the right failure: still speaking in the wrong voice
        beats silence. -192 here means the voice wanted a `ttvw` that is not
        registered, which the gate in `voices.py` should have caught first.
        """
        if self._dead:
            return False
        voice = self._match(voice) or voice
        creator = voice.creator.encode("mac-roman", "replace")
        self.h.w32(VOICE_SPEC, int.from_bytes(creator[:4].ljust(4, b" "), "big"))
        self.h.w32(VOICE_SPEC + 4, voice.id)
        if self._set_info(SO_CURRENT_VOICE, VOICE_SPEC) != 0:
            return False
        self.voice = voice
        # The new voice brings its own 'pbas'. Dropping the cache rather than
        # re-reading it here is safe because taking a voice resets the
        # channel's pitch to that voice's own -- measured, the same way it was
        # for MacinTalk 2. See tests/test_macintalk3.py.
        self._base_pitch = None
        self._base_mod = None
        return True

    def _set_info(self, selector, arg):
        """SetSpeechInfo(selector, ptr) -> OSErr."""
        if self._dead:
            return -1
        reason, result = self.h.component_call(
            self.chan, SET_INFO, [selector, arg], max_instr=50_000_000)
        return _signed(result) if reason == 1 else -1

    # -- settings ----------------------------------------------------------
    def _fixed_arg(self, value):
        """Put a Fixed somewhere and return its address.

        **SetSpeechInfo takes a pointer for every selector**, the scalar ones
        included. Passing the value directly has the engine dereference it,
        which cost a day on MacinTalk 2.
        """
        self.h.w32(PARAM_BUF, _fixed(value))
        return PARAM_BUF

    def set_rate(self, rate):
        """Words per minute, which is what the Speech Manager's 'rate' is."""
        self._rate = rate
        self._set_info(SO_RATE, self._fixed_arg(rate))

    def base_pitch(self):
        """This voice's own 'pbas', asked of the engine and kept."""
        if self._base_pitch is None:
            self._base_pitch = self.current_pitch()
        return self._base_pitch

    def current_pitch(self):
        """GetSpeechInfo('pbas') -- what the channel holds right now."""
        if self._dead:
            return None
        self.h.w32(PARAM_BUF, 0)
        reason, result = self.h.component_call(
            self.chan, GET_INFO, [SO_PITCH_BASE, PARAM_BUF],
            max_instr=50_000_000)
        if reason != 1 or result != 0:
            return None
        return _unfixed(self.h.r32(PARAM_BUF))

    def set_pitch(self, tenths):
        """Tenths of a semitone away from the voice's own pitch.

        'pbas' is a musical scale at twelve units to the octave, the same one
        the other two engines use -- not hertz. See macintalk2.set_pitch for
        the whole of that story.
        """
        self._pitch = tenths
        base = self.base_pitch()
        if base is None:
            return
        self._set_info(SO_PITCH_BASE, self._fixed_arg(base + tenths / 10.0))

    def base_inflection(self):
        """This voice's own 'pmod', asked of the engine and kept.

        Nineteen voices and five different answers: 50.000 for Fred, Ralph and
        Whisper, 39.999 for Junior, Kathy and Princess, 25.000 for Boing,
        12.500 for Albert and Bahh, and **0.000 for the nine novelty voices**
        -- Zarvox, Trinoids, Deranged and the singing ones. Those last are not
        broken. A robot that never varies its pitch is the voice, and a sung
        line gets its pitch from the note rather than from a contour.
        """
        if self._base_mod is None:
            self._base_mod = self.current_inflection()
        return self._base_mod

    def current_inflection(self):
        """GetSpeechInfo('pmod') -- what the channel holds right now."""
        if self._dead:
            return None
        self.h.w32(PARAM_BUF, 0)
        reason, result = self.h.component_call(
            self.chan, GET_INFO, [SO_PITCH_MOD, PARAM_BUF],
            max_instr=50_000_000)
        if reason != 1 or result != 0:
            return None
        return _unfixed(self.h.r32(PARAM_BUF))

    def set_inflection(self, percent):
        """NVDA's 0-100, with 50 leaving the voice exactly as recorded.

        'pmod' is a depth rather than a position, so the slider scales it
        instead of offsetting it: half at 25, the voice's own at 50, twice it
        at 100. Measured across all nineteen voices, every value is stored
        exactly as given and every one of them renders differently, so there
        is nothing here to work around -- see `tools/probe_inflection.py`.

        **The nine voices whose own 'pmod' is zero would otherwise have a dead
        slider**, since twice nothing is nothing. They get an absolute depth
        above the midpoint instead, which leaves them untouched at 50 and lets
        anyone who wants Zarvox to have intonation ask for it.
        """
        self._mod = percent
        base = self.base_inflection()
        if base is None:
            return
        if base > 0:
            value = base * percent / 50.0
        else:
            value = INFLECTION_REFERENCE * max(0, percent - 50) / 50.0
        self._set_info(SO_PITCH_MOD, self._fixed_arg(min(100.0, value)))

    def read_settings(self):
        return {"rate": self._rate, "pitch": self._pitch,
                "inflection": self._mod}

    # -- speaking ----------------------------------------------------------
    #: Punctuation the engine pronounces as a word, which has to go because
    #: NVDA has already named whatever the user asked to hear. Taken from
    #: MacinTalk 2, whose front end this one is descended from.
    SPOKEN_PUNCTUATION = "()[]{}<>@#$%^&*+=/\\|~`\"_"

    def translate(self, text):
        """MacinTalk 3 has its own front end, so this only prepares the text."""
        if self.number_mode in ("words", "digits"):
            text = numwords.normalise(
                text, spell_out=(self.number_mode == "digits"))
        for ch in self.SPOKEN_PUNCTUATION:
            if ch in text:
                text = text.replace(ch, " ")
        return text

    def busy(self):
        """SpeechStatusInfo.outputBusy, which is how it says it is done."""
        if self._dead:
            return False
        for off in (0, 4, 8):
            self.h.w32(STATUS_BUF + off, 0)
        reason, _r = self.h.component_call(self.chan, STATUS, [STATUS_BUF],
                                           max_instr=20_000_000)
        if reason != 1:
            return False
        return bool(self.h.r8(STATUS_BUF)) or bool(self.h.r32(STATUS_BUF + 2))

    def speak(self, text, sink=None):
        """-> 8-bit unsigned PCM at NATIVE_RATE, or b"" when `sink` took it.

        SpeakBuffer returns as soon as the first buffer is queued, so the audio
        only exists if the host keeps being the Sound Manager afterwards: each
        callback installs a deferred task, and *that* renders the next buffer.

        **`sink` is why long sentences stopped costing a second of silence.**
        This engine renders at about 24x realtime -- MacinTalk 2 manages 157x,
        so it is genuinely the slow one -- and a 23-second utterance therefore
        took the best part of a second before a sample of it could be played.
        Given a sink, each piece goes out as it is rendered instead, so the
        first sound arrives after one buffer rather than after all of them.

        Handing it out is not quite as simple as sending every piece, because
        the trailing silence can only be recognised once it has stopped
        growing. So one piece is always held back -- the lookbehind the Tiger
        add-on needed for the same reason -- and the held piece keeps
        *absorbing* while it is entirely silent, or a tail spanning two
        buffers would ship half of itself. Only the last piece is trimmed at
        the end; only the first at the start.

        With no sink the return value is exactly what it always was, and the
        two paths are asserted byte-identical in tests/test_macintalk3.py.
        """
        if self._dead:
            return b""
        raw = text.strip().encode("mac-roman", "replace")
        if not raw:
            return b""
        h = self.h
        h.pcm_reset()
        # The engine reads its text buffer as a C string in places, so the
        # terminator goes in even though the length is passed too.
        h.load(TEXT_BUF, raw + b"\0")
        reason, _res = h.component_call(self.chan, SPEAK,
                                        [TEXT_BUF, len(raw), 0],
                                        max_instr=400_000_000)
        if reason != 1:
            return b""

        stream = ospaudio.Stream(sink) if sink is not None else None
        while h.buffers_taken < self.MAX_BUFFERS:
            if not h.run_callbacks(max_rounds=8):
                break                       # nothing pending: really finished
            if stream is not None:
                piece = h.pcm
                if piece:
                    h.pcm_reset()
                    if not stream.feed(piece):
                        break               # cancelled: stop rendering
            if not self.busy():
                break
        else:
            try:
                from logHandler import log
                log.warning("MacinTalk 3: utterance hit the %d buffer ceiling"
                            % self.MAX_BUFFERS)
            except ImportError:
                pass

        pcm = h.pcm
        if stream is not None and stream.aborted:
            # **Abandoned, so tell the engine to stop rather than pumping the
            # rest of the utterance into a bin.** Draining alone still did 40
            # to 48 per cent of the work: measured on a Mastodon-sized post,
            # giving up after the first piece took 165 ms of a 342 ms render,
            # because `run_callbacks` goes on rendering whatever it is handed.
            # Scrolling past five posts spent seconds on audio nobody heard,
            # and every keystroke queued behind it.
            #
            # StopSpeech(kImmediate) first brings that to 39 ms on MacinTalk 3
            # and 112 on Pro, and the next utterance stays byte-identical --
            # which is the thing to check, since a half-stopped engine is
            # exactly how audio bleeds from one utterance into the next.
            #
            # Safe here and NOT from `stop()`: this runs on the worker, inside
            # speak, and is the only thread driving the 68000. `stop()` is
            # called on NVDA's main thread and must stay a no-op.
            self._quiet()
        elif stream is not None:
            stream.finish(pcm)
        # Drain whatever the engine still has queued and throw it away, so the
        # tail of this utterance cannot arrive at the front of the next one.
        h.run_callbacks(max_rounds=64)
        h.pcm_reset()
        return b"" if stream is not None else ospaudio.trim(pcm)

    def _quiet(self):
        """StopSpeech(kImmediate), from the worker thread only."""
        if self._dead:
            return
        try:
            self.h.component_call(self.chan, STOP, [0], max_instr=20_000_000)
        except Exception:
            pass

    def stop(self):
        """Deliberately does not touch the emulator; see macintalk2.stop.

        `cancel()` runs on NVDA's main thread while `speak()` runs on the
        worker, and a component call drives the 68000. Two threads stepping
        one CPU corrupts it.
        """
        return

    def close(self):
        if self._dead:
            return
        self._dead = True
        try:
            _LIVE.remove(self)
        except ValueError:
            pass
        try:
            self.h.component_call(self.chan, CLOSE, [], max_instr=20_000_000)
        except Exception:
            pass
        try:
            self.h.close()
        except Exception:
            pass


def _split(name):
    """`ttvi_10.bin` -> ('ttvi', 10).  None for anything that is not one."""
    stem, ext = os.path.splitext(name)
    if ext.lower() != ".bin" or "_" not in stem:
        return None
    rtype, rest = stem.rsplit("_", 1)
    try:
        return rtype, int(rest)
    except ValueError:
        return None
