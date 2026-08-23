# -*- coding: utf-8 -*-
"""MacinTalk 2 as an engine the driver can drive, alongside `.sp`.

Same shape as `engine.py` -- `translate`, `speak`, `stop`, `set_rate`,
`set_voice`, `close` -- so `outspoken.py` does not have to know which engine it
is talking to.  Two things genuinely differ and the driver must not assume
otherwise:

* **There is no separate translation step.**  `.sp` speaks phonemes and nothing
  else, so the driver runs the NRL rules itself.  MacinTalk 2 ships its own
  front end and takes English, so `translate` only does the number pass and
  hands the text straight through.
* **Speaking is asynchronous.**  `SpeakBuffer` renders one buffer and returns;
  everything after that arrives because the host keeps answering the Sound
  Manager.  See `speak`.

Only one engine can be live at a time, here as in `engine.py`: `osp_init()`
resets the emulator's global state, so building a second Engine invalidates the
first.  The driver rebuilds when the user picks a voice from another engine.

Everything about the component protocol is in docs/macintalk2-components.md.
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

FRONT_BASE = 0x00040000
BACK_BASE = 0x00060000
HEAP = 0x00080000
HEAP_SIZE = 0x000E0000
STACK = 0x00200000
TEXT_BUF = 0x00195000
VOICE_SPEC = 0x00196100
STATUS_BUF = 0x00196200
PARAM_BUF = 0x00196300

CPUFLAG, RESERR, MEMERR = 0x012F, 0x0A60, 0x0220

#: The engine renders at the classic Macintosh rate; the driver resamples
#: nothing, exactly as it does not for `.sp`.
NATIVE_RATE = 22254

#: Standard component selectors, from the -1..-6 table at Cecy 3 +$30.
OPEN, CLOSE = -1, -2
#: Component selectors, identified from their handlers.
STATUS, SPEAK, STOP, GET_INFO, SET_INFO = 0, 1, 2, 5, 6

#: Speech Manager selectors, from Apple's Speech.h.
SO_CURRENT_VOICE = 0x63766F78          # 'cvox'
SO_RATE = 0x72617465                   # 'rate'
SO_PITCH_BASE = 0x70626173             # 'pbas'
SO_PITCH_MOD = 0x706D6F64              # 'pmod'

#: What a voice with no modulation of its own is given at the top of the
#: slider. Only RoboVox and Xero are affected here, and only above the
#: midpoint -- at 50 they are still exactly as Apple shipped them.
INFLECTION_REFERENCE = 25.0

#: The shared tables, which every voice needs.  `ttsd 2` is optional in
#: principle; in practice every extraction has both.
TABLES = (("ttsr", 1), ("ttsd", 1), ("ttsd", 2),
          ("ttss", 0), ("ttph", 1), ("ttop", 1))

REQUIRED = ("Cecy_1.bin", "Cecy_3.bin")

_LIVE = []


def _fixed(x):
    """A Fixed 16.16, which is how the Speech Manager passes rate and pitch."""
    return int(round(x * 65536.0)) & 0xFFFFFFFF


def _unfixed(u):
    """A Fixed 16.16 back to a float, signed."""
    if u & 0x80000000:
        u -= 1 << 32
    return u / 65536.0


def find(roots):
    """-> ({name: path}, [Voice]) for whatever of MacinTalk 2 is installed.

    Returns empty rather than raising when it is absent: a user with only the
    1984 engine must still get a working synthesiser, which is the whole point
    of enumerating instead of hardcoding.
    """
    files = {}
    for root in roots:
        if not os.path.isdir(root):
            continue
        for dirpath, _dirs, names in os.walk(root):
            for n in names:
                files.setdefault(n, os.path.join(dirpath, n))
    if not all(r in files for r in REQUIRED):
        return {}, []
    # `speakable` asks the same question a second way -- is this engine really
    # installed, not just some of its voices -- and it is the rule every engine
    # module shares. See voices.ENGINE_FILES; MacinTalk Pro is the case that
    # makes it matter, because the extractor hands out its ~900 KB voices to
    # anyone whose disk image had them, engine or no engine.
    found, _bad = voicelib.installed("mtk2", roots=roots, speakable=True)
    return files, found


def usable(roots):
    """Also checks the DLL can actually do components.

    A stale osp_host.dll is easy to end up with, because NVDA holds it open
    while the synthesizer is loaded and it cannot be replaced in place. Listing
    MacinTalk 2 voices against a binary that cannot run them would offer the
    user voices that are guaranteed to be silent.
    """
    files, found = find(roots)
    if not (files and found):
        return False
    try:
        # Already loaded, so this is the same handle rather than a second copy.
        return hasattr(ctypes.CDLL(osp.DLL), "osp_add_component")
    except Exception:
        return False


class Engine(object):
    """One open MacinTalk 2 front end, with one voice selected."""

    number_mode = "words"

    def __init__(self, files, allvoices, voice=None):
        """Register *every* MacinTalk 2 voice, then select one.

        Rebuilding the emulator to change voice would be the obvious reading of
        the one-engine-at-a-time constraint, but it is not necessary here: the
        voices use distinct resource ids -- which is exactly why each `ttvd`
        names its own `ttvi` and `ttvw` -- so all ten can sit in the resource
        table at once and switching is a single SetSpeechInfo('cvox').

        Ten voices is about 300 KB against a 917 KB heap and 40 of the host's
        64 resource slots, so this is affordable rather than clever.  A rebuild
        is still needed to move between *engines*, since osp_init() resets
        everything.
        """
        for old in _LIVE:
            old._dead = True
        _LIVE.append(self)
        self._dead = False
        self.voices = list(allvoices)
        self.voice = voice or self.voices[0]
        self._rate = None
        #: Tenths of a semitone from the voice's own pitch, and that pitch as
        #: the engine reports it. The second is per-voice, so `select` drops it.
        self._pitch = 0
        self._base_pitch = None
        self._mod = 50
        self._base_mod = None

        h = self.h = osp.Host()
        h.load(FRONT_BASE, open(files["Cecy_3.bin"], "rb").read())
        h.load(BACK_BASE, open(files["Cecy_1.bin"], "rb").read())
        h.heap(HEAP, HEAP_SIZE)
        h.mem_traps(True)
        h.w8(CPUFLAG, 0)
        h.w16(RESERR, 0)
        h.w16(MEMERR, 0)

        for rtype, rid in TABLES:
            path = files.get("%s_%d.bin" % (rtype, rid))
            if path:
                h.add_resource(rtype, rid, open(path, "rb").read())

        # Every voice's three resources, under the ids its own ttvd asks for.
        # A voice that will not load is dropped rather than fatal: one bad
        # extraction must not cost the user the other nine.
        loaded = []
        for v in self.voices:
            try:
                ttvd = None
                for kind, path in sorted(v.files.items()):
                    data = open(path, "rb").read()
                    rid = int(os.path.basename(path).split("_")[1].split(".")[0])
                    h.add_resource(kind, rid, data)
                    if kind == "ttvd":
                        ttvd = rid
                if ttvd is None:
                    continue
                # We are the Speech Manager: GetVoiceInfo('fref') answers with
                # the ttvd id, because that is what the engine opens a voice by.
                h.add_voice(v.creator, v.id, ttvd)
                loaded.append(v)
            except Exception:
                continue
        self.voices = loaded
        if not loaded:
            raise RuntimeError("no MacinTalk 2 voice could be loaded")
        # Match on identity of the *voice*, not of the Python object. The
        # caller's Voice comes from its own scan of the ROM folder, so it is
        # never the same instance as ours -- `not in loaded` was therefore
        # always true, and every first MacinTalk 2 voice silently became
        # whichever one sorted first. It spoke, so nothing looked broken.
        self.voice = self._match(self.voice) or loaded[0]

        fe = h.add_component("ttsc", "mtk2", "mtk2", FRONT_BASE)
        h.add_component("t2be", "t2be", "mtk2", BACK_BASE)
        self.chan = h.open_instance(fe)

        h.set_reg(osp.A7, STACK)
        h.set_reg(osp.SR, 0x2700)
        # MacinTalk 2's callback only refills on its second invocation, so
        # answering it while the engine is still mid-call spends the first one
        # before there is anything for it to be about.
        h.defer_callbacks(True)

        reason, result = h.component_call(self.chan, OPEN, [self.chan],
                                          max_instr=50_000_000)
        if reason != 1 or result != 0:
            raise RuntimeError("MacinTalk 2 Open returned %d (stop %d)"
                               % (_signed(result), reason))
        self.select(self.voice)

    # -- set-up ------------------------------------------------------------
    def _match(self, voice):
        """Our own Voice record for `voice`, matched on creator and id."""
        if voice is None:
            return None
        for v in self.voices:
            if v.creator == voice.creator and v.id == voice.id:
                return v
        return None

    def select(self, voice):
        """Switch voice without rebuilding: SetSpeechInfo('cvox', VoiceSpec).

        -> True if the engine took it. A refusal leaves the previous voice in
        place, which is the right failure: still speaking in the wrong voice
        beats silence.
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
        # The new voice brings its own 'pbas' -- Votron's 38 against Mariel's
        # 61 is more than an octave apart -- so the cached one is now wrong.
        #
        # Dropping it rather than re-reading it here is safe because taking a
        # voice resets the channel's pitch to that voice's own: measured, Ben
        # reads 60, moving the slider to the top makes it 72, selecting Votron
        # makes it 38, and coming back to Ben makes it 60 again. So the cache
        # is empty exactly when the channel is untouched, and the next
        # question gets the voice's real pitch rather than our own offset read
        # back as though it were one. The driver re-applies settings after
        # every switch, which is what puts the offset on the new base.
        self._base_pitch = None
        self._base_mod = None
        return True

    def _set_info(self, selector, arg):
        """SetSpeechInfo(selector, ptr-or-value) -> OSErr."""
        if self._dead:
            return -1
        reason, result = self.h.component_call(
            self.chan, SET_INFO, [selector, arg], max_instr=50_000_000)
        return _signed(result) if reason == 1 else -1

    # -- settings ----------------------------------------------------------
    def _fixed_arg(self, value):
        """Put a Fixed somewhere and return its address.

        **SetSpeechInfo takes a pointer for every selector**, including the
        scalar ones -- `soRate` wants a `Fixed *`, not a `Fixed`. Passing the
        value directly is not merely ignored: the engine dereferences it, and
        for a rate of 232 that means reading address $00E80000. It does not
        crash, it quietly corrupts, and every utterance afterwards came out at
        6.65 seconds instead of 1.84 regardless of the rate asked for.

        'cvox' worked from the start only because a VoiceSpec is passed by
        address anyway, which hid this until the first scalar setting.
        """
        self.h.w32(PARAM_BUF, _fixed(value))
        return PARAM_BUF

    def set_rate(self, rate):
        """Words per minute, which is what the Speech Manager's 'rate' is."""
        self._rate = rate
        self._set_info(SO_RATE, self._fixed_arg(rate))

    def base_pitch(self):
        """The current voice's own 'pbas', asked of the engine and kept.

        Every voice has its own: Votron sits at 38 and Mariel at 61, so an
        absolute scale would put the middle of a slider in a different place
        for each one. Asking beats tabulating -- the number is in the voice,
        and a table here could disagree with the files on disk.
        """
        if self._base_pitch is None:
            self._base_pitch = self.current_pitch()
        return self._base_pitch

    def current_pitch(self):
        """GetSpeechInfo('pbas') -- what the channel holds right now.

        Not the same question as `base_pitch`: this one moves as the slider
        does. It is how the tests check that an offset arrived, since the
        result code cannot be trusted to say so.
        """
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

        **'pbas' is a musical scale, not hertz** -- twelve units to the
        octave, and 60.000 is where Apple put middle C. That one fact is the
        whole of why this was switched off for so long: the driver was handing
        it hertz, so a request for 90 Hz meant a note near 2 kHz and 180 Hz
        meant one near 350 kHz. Both landed past the engine's ceiling, both
        clamped to the same place, and the byte-identical renders that came
        back looked like the selector was broken rather than obeyed.

        Measured on Ben, `tools/probe_pitch.py`: -24 gives 0.253 of the base
        frequency where the scale predicts 0.250, -6 gives 0.722 against
        0.707, +6 gives 1.391 against 1.414. It is twelve to the octave.

        The engine has a ceiling of its own around 'pbas' 72 -- Ben renders
        identically at 72, 78, 84, 90 and 180 -- which is harmless, and is
        why nothing here needs to clamp: asking too high wastes the top of
        the slider rather than breaking anything.
        """
        self._pitch = tenths
        base = self.base_pitch()
        if base is None:
            return
        self._set_info(SO_PITCH_BASE, self._fixed_arg(base + tenths / 10.0))

    def base_inflection(self):
        """This voice's own 'pmod'. Eight of the ten answer 100.000.

        The two that answer 0.000 are RoboVox and Xero, and that is the voice
        rather than a fault: a robot that never varies its pitch is the point
        of both of them.
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

        **This engine's 'pmod' has two states and not a scale.** Send it
        anything at all above zero and it stores 100.000: 6.25, 12.5, 25 and
        50 all read back as 100 and render to the same bytes. So the slider
        means "flat" at 0 and "as Apple made it" everywhere else, and the
        arithmetic below -- shared with the two engines that do have a scale --
        happens to produce exactly that.

        Telling the user that is the readme's job, not this driver's: the
        alternative is a slider that lies about being continuous on the other
        engines to be honest about this one.
        """
        self._mod = percent
        base = self.base_inflection()
        if base is None:
            return
        if base > 0:
            value = base * percent / 50.0
        else:
            # RoboVox and Xero, which would otherwise have a dead slider.
            value = INFLECTION_REFERENCE * max(0, percent - 50) / 50.0
        self._set_info(SO_PITCH_MOD, self._fixed_arg(min(100.0, value)))

    def read_settings(self):
        return {"rate": self._rate, "pitch": self._pitch,
                "inflection": self._mod}

    # -- speaking ----------------------------------------------------------
    #: Punctuation MacinTalk 2 pronounces as a word, which has to go because
    #: NVDA has already named whatever the user asked to hear.
    #:
    #: Measured, not assumed. Speaking "x <c> x" for every punctuation
    #: character and comparing against "x x": `-` and `'` come out *shorter*
    #: than the baseline, `, ; :` add only a pause, and everything below is a
    #: second of extra speech. That is why "(x64)" was read as "left paren open
    #: paren x sixty four right paren close parenthesis" -- NVDA supplied the
    #: names and the engine supplied them again.
    #:
    #: `, . ; : ! ? - '` are kept: they are prosody here, not vocabulary.
    SPOKEN_PUNCTUATION = "()[]{}<>@#$%^&*+=/\\|~`\"_"

    def translate(self, text):
        """MacinTalk 2 has its own front end, so this only prepares the text.

        Whatever this returns is what `speak` is handed, which is how the
        driver can treat both engines alike.
        """
        if self.number_mode in ("words", "digits"):
            text = numwords.normalise(
                text, spell_out=(self.number_mode == "digits"))
        # A space, not nothing: removing the character outright would run the
        # words either side together into one.
        for ch in self.SPOKEN_PUNCTUATION:
            if ch in text:
                text = text.replace(ch, " ")
        return text

    def busy(self):
        """SpeechStatusInfo.outputBusy, which is how the engine says it is done.

        `outputBusy` is byte 0 and `inputBytesLeft` is a long at +2; see the
        status handler at Cecy 3 +$5CE, which fills the struct field for field.
        """
        if self._dead:
            return False
        for off in (0, 4, 8):
            self.h.w32(STATUS_BUF + off, 0)
        reason, _r = self.h.component_call(self.chan, STATUS, [STATUS_BUF],
                                           max_instr=20_000_000)
        if reason != 1:
            return False
        return bool(self.h.r8(STATUS_BUF)) or bool(self.h.r32(STATUS_BUF + 2))

    #: A ceiling on one utterance, in buffers. Nothing legitimate approaches
    #: it -- a long sentence is about thirty -- so reaching it means the engine
    #: is producing without ever finishing, and the right answer is to stop and
    #: say so rather than hand NVDA two minutes of silence.
    #: About 70 seconds: this engine's buffers are 2057 bytes. Counted in
    #: seconds rather than buffers because the same count means 23 s on
    #: MacinTalk Pro and 60 on MacinTalk 3, and Pro was being truncated.
    MAX_BUFFERS = 800

    def speak(self, text):
        """-> 8-bit unsigned PCM at NATIVE_RATE, trailing silence trimmed.

        SpeakBuffer returns as soon as the first buffer is queued, so the audio
        only exists if the host keeps being the Sound Manager afterwards. Each
        callback installs a deferred task, and *that* renders.

        The stopping condition has to come from the engine, not from the
        callback chain going quiet. Pumping until nothing is pending gave 8.5
        seconds for "Testing 30 items" and, after a few voice changes, 131
        seconds of silence: a real-time synthesiser keeps its channel fed
        whether or not it has anything left to say.
        """
        if self._dead:
            return b""
        raw = text.strip().encode("mac-roman", "replace")
        if not raw:
            return b""
        h = self.h
        h.pcm_reset()
        mark = h.buflog_n()
        h.load(TEXT_BUF, raw)
        reason, _res = h.component_call(self.chan, SPEAK,
                                        [TEXT_BUF, len(raw), 0],
                                        max_instr=400_000_000)
        if reason != 1:
            return b""
        while h.buffers_taken < self.MAX_BUFFERS:
            if not h.run_callbacks(max_rounds=8):
                break                       # nothing pending: really finished
            if not self.busy():
                break
        else:
            # Guarded: this module is driven from tools and tests as well as
            # from NVDA, and the ceiling is exactly the case those reach.
            try:
                from logHandler import log
                log.warning("MacinTalk 2: utterance hit the %d buffer ceiling"
                            % self.MAX_BUFFERS)
            except ImportError:
                pass

        # Take the audio *now*, then let the engine settle and throw away
        # whatever that produces.
        #
        # `busy()` going false does not mean the Sound Manager is idle: there
        # can still be a callback pending that belongs to the utterance just
        # finished. Left alone it does not run until the *next* speak() pumps,
        # and then it queues the old buffer, so the previous utterance's tail
        # arrives at the front of the new one -- "type here to search" followed
        # by the "ch" of the item before it. Draining here keeps each utterance
        # to its own audio.
        pcm = h.pcm
        lengths = h.buflog_lengths(mark)
        h.run_callbacks(max_rounds=64)
        h.pcm_reset()
        return _trim(_drop_restated(pcm, lengths))

    def stop(self):
        """Deliberately does not touch the emulator. Read this before "fixing".

        The obvious implementation is StopSpeech(kImmediate) -- selector 2 --
        and it is wrong here, dangerously so. `cancel()` runs on NVDA's **main**
        thread while `speak()` is running on the worker, and a component call
        drives the 68000. Two threads stepping one CPU corrupts its state: it
        sounded like buzzing, utterances ran to six seconds of near-silence for
        the word "button", and the pump hit its buffer ceiling.

        `.sp` can stop from another thread because its stop is a single byte
        written into emulated memory. MacinTalk 2 has no equivalent, so the
        honest answer is not to try.

        Nothing is lost. Rendering an utterance takes 15-150 ms, and cancel's
        real work -- draining the queues and stopping the player -- is what
        actually interrupts. At worst one short buffer finishes rendering into
        a queue that is about to be emptied.
        """
        return

    def close(self):
        """Close the component, then unload the DLL.

        The component gets its own Close first so it can release what it
        allocated; then the library goes, which is what unlocks the file for
        the next build. See osp.Host.close.
        """
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


def _signed(v):
    return v - 0x100000000 if v & 0x80000000 else v


#: 8-bit unsigned silence. The engine clears its buffers to this before it
#: renders into them, so a partly-used final buffer ends in a run of it.
_SILENT = 0x80


def _drop_restated(pcm, lengths):
    """Drop a final buffer that merely restates the one before it.

    MacinTalk 2 double-buffers, and when it finishes it can hand the Sound
    Manager the *other* half again without refilling it. On real hardware the
    channel has been stopped by then and nobody hears it; we take every
    bufferCmd offered, so the last chunk of speech arrived twice. Heard as
    "select synthesizer-er", and on a single letter as the sound followed by a
    little tail of itself.

    The signature is exact, and both halves of it matter:

        s                    live buffers ... 4, 6   identical, gap 2  -> drop
        type here to search  live buffers ... 9, 11  identical, gap 2  -> drop
        a, b, c, d, e        live buffers ... 15, 17 identical, gap 2  -> drop
        hello, world. ...    live buffers ... 20, 21 differ,    gap 1  -> keep
        quote                live buffers ... 5, 6   differ,    gap 1  -> keep

    Comparing whole buffers rather than cutting at the first silent one is
    what makes this safe: silent buffers occur *inside* an utterance wherever
    there is a pause, four times over in "a, b, c, d, e", so a rule based on
    silence alone would truncate at the first comma.
    """
    if len(lengths) < 2:
        return pcm
    bounds, off = [], 0
    for n in lengths:
        bounds.append((off, off + n))
        off += n

    def chunk(i):
        a, b = bounds[i]
        return pcm[a:b]

    # Drop repeatedly: the engine can restate more than once at the tail.
    end = len(bounds)
    while True:
        live = [i for i in range(end)
                if any(c != _SILENT for c in chunk(i))]
        if len(live) < 2:
            break
        last = live[-1]
        # Compare against *every* earlier buffer, not just the previous live
        # one. The restated buffer is the other half of the double buffer, so
        # it matches the chunk two slots back -- and there is not always a
        # silent buffer in between. Comparing only with the previous live
        # buffer caught "s" and missed "six", "button", "close" and "eight",
        # which is what "still repeats on some words" turned out to be.
        if not any(chunk(last) == chunk(j) for j in range(last)):
            break
        end = last
    return pcm[:bounds[end - 1][1]] if end < len(bounds) else pcm


def _trim(pcm, keep=1200, lead=220):
    """Drop the silence at both ends, leaving a little at each.

    **The leading silence is the one that is felt.** MacinTalk 2 primes its
    double buffer before it renders anything, so every utterance began with
    about 0.38 s of nothing -- a third of a second of dead air before each
    typed character, heard as a pause and then the tail of the sound arriving
    late. Trailing silence matters too, since the final buffer is only part
    used, but that one only costs latency before the *next* item.

    `keep` leaves about 50 ms at the end and `lead` about 10 ms at the start,
    because cutting hard on a sample clicks.
    """
    if not pcm:
        return pcm
    n = len(pcm)
    start = 0
    while start < n and pcm[start] == _SILENT:
        start += 1
    if start >= n:
        return b""                      # nothing but silence: say nothing
    end = n
    while end > start and pcm[end - 1] == _SILENT:
        end -= 1
    return pcm[max(0, start - lead):min(n, end + keep)]
