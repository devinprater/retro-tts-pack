# -*- coding: utf-8 -*-
"""
Emulation core for the First Byte "Monologue" (ProVoice) synthesizer.

Runs the real 16-bit Windows 3.1 speech engine (FB_SPCH.DLL, FB_TIMER.DLL,
FB_NGN.EXE and a voice-table DLL) under the Unicorn CPU emulator (see win16.py),
captures the PCM the engine would have sent to waveOut, converts it to 16-bit /
22050 Hz mono, and returns it.  Because everything is emulated, it runs
identically in 32-bit and 64-bit NVDA.

Two voices are provided:
    FB_22K16 - 22 kHz, 16-bit  (higher quality)
    FB_11K8  - 11 kHz,  8-bit  (the older, grainier build)
The 11 kHz voice is upsampled and widened to the common 22050/16 output.

Interface used by the NVDA driver:
    eng = Engine(bin_dir)
    eng.set_voice('fb22')                 # or 'fb11'
    eng.configure(volume=?, pitch=?, rate=?)
    eng.speak(text, should_cancel=..., on_block=lambda pcm16: ...)
"""

import os
import json
import threading
from array import array

try:
    from . import win16
except ImportError:                       # standalone
    import win16

#: Common output format handed to NVDA.
OUT_RATE = 22050
BITS = 16
CHANNELS = 1

#: id -> (voice-table DLL, display name, native rate, native bits)
VOICES = {
    'fb22': ('FB_22K16', 'Monologue (ProVoice 22 kHz, male)', 22050, 16),
    'fb11': ('FB_11K8', 'Monologue (ProVoice 11 kHz, male)', 11025, 8),
}
DEFAULT_VOICE = 'fb22'

#: Longest text handed to the engine in one call; the driver splits longer text.
MAX_TEXT = 240

#: Engine parameters are 0-9 (First Byte convention), 5 = neutral.
ENGINE_MIN, ENGINE_MAX, ENGINE_DEFAULT = 0, 9, 5
DEFAULT_VOLUME = 9
DEFAULT_PITCH = 5
DEFAULT_RATE = 5

#: Trailing-silence gate.  The engine pads every utterance with ~0.5-0.9 s of
#: silence; feeding that to NVDA makes navigation feel laggy.  Frames quieter
#: than SIL_THRESH at the end of the stream are dropped; silence *between* speech
#: is kept (it is emitted as soon as more speech follows).
SIL_FRAME = 440          # bytes @ 22050/16 mono ~= 10 ms
SIL_THRESH = 300         # peak |sample| below this = silence

# MAINWNDPROC message ids driven during synthesis.
_WM_COMMAND = 0x111
_MM_WOM_DONE = 0x3BD
_CMD_TICK = 0x705
_HWAVEOUT = 0x0BED
_WINMAIN = 0x260E


class EngineError(RuntimeError):
    pass


# -- PCM helpers -----------------------------------------------------------
def to_pcm16(data, bits):
    """Bytes in the engine's native sample format -> array('h') of int16."""
    if bits == 16:
        n = len(data) & ~1
        return array('h', bytes(data[:n]))        # little-endian on Windows
    # 8-bit unsigned PCM (Sound Blaster style) -> signed 16-bit
    return array('h', [(b - 128) << 8 for b in data])


class Resampler(object):
    """Stateful linear resampler; passthrough when src == dst."""

    def __init__(self, src, dst):
        self.passthru = (src == dst)
        self.step = float(src) / float(dst)       # input samples per output sample
        self.pos = 0.0
        self.prev = 0
        self.have_prev = False

    def feed(self, samples):
        if self.passthru:
            return samples
        out = []
        ap = out.append
        prev = self.prev
        pos = self.pos
        have = self.have_prev
        step = self.step
        for s in samples:
            if not have:
                prev = s
                have = True
                continue
            while pos < 1.0:
                ap(int(prev + (s - prev) * pos))
                pos += step
            pos -= 1.0
            prev = s
        self.prev = prev
        self.pos = pos
        self.have_prev = have
        return out


def _pack(samples):
    return samples.tobytes() if isinstance(samples, array) else array('h', samples).tobytes()


def _peak(buf):
    if not buf:
        return 0
    n = len(buf) // 2
    return max(abs(x) for x in array('h', bytes(buf[:n * 2]))) if n else 0


class _SilenceGate(object):
    """Pass audio through but hold back a trailing run of silence, dropping it at
    the end.  Silence surrounded by speech is emitted normally."""

    def __init__(self, sink):
        self._sink = sink
        self._held = bytearray()

    def feed(self, pcm16):
        self._held += pcm16
        nframes = len(self._held) // SIL_FRAME
        if nframes == 0:
            return
        last_speech = -1
        for i in range(nframes):
            fr = self._held[i * SIL_FRAME:(i + 1) * SIL_FRAME]
            if _peak(fr) >= SIL_THRESH:
                last_speech = i
        if last_speech >= 0:
            cut = (last_speech + 1) * SIL_FRAME
            self._sink(bytes(self._held[:cut]))
            del self._held[:cut]

    def flush(self):
        # emit any remaining audio that still contains speech; drop pure silence
        if _peak(self._held) >= SIL_THRESH:
            self._sink(bytes(self._held))
        self._held = bytearray()


class Engine(object):
    def __init__(self, bin_dir):
        self.bin_dir = bin_dir
        self._lock = threading.Lock()
        self._params = (DEFAULT_VOLUME, DEFAULT_PITCH, DEFAULT_RATE)
        self._voice = None
        self._native_fmt = (OUT_RATE, BITS, CHANNELS)
        try:
            self._boot()
        except EngineError:
            raise
        except Exception as e:
            raise EngineError('engine initialisation failed: %s' % e)

    # -- boot --------------------------------------------------------------
    def _boot(self):
        emu = win16.Win16Emu(verbose=False)
        emu.bin_dir = os.fspath(self.bin_dir)
        acp = os.path.join(self.bin_dir, 'argclean.json')
        if os.path.isfile(acp):
            with open(acp) as f:
                emu.argclean_tables = json.load(f)
        dsp = os.path.join(self.bin_dir, 'ds_sensitive.json')
        if os.path.isfile(dsp):
            with open(dsp) as f:
                emu.ds_sensitive_tables = {k: set(v) for k, v in json.load(f).items()}
        self.emu = emu
        d = self.bin_dir
        self.spch = emu.load(os.path.join(d, 'FB_SPCH.DLL'))
        self.tmr = emu.load(os.path.join(d, 'FB_TIMER.DLL'))
        self.ngn = emu.load(os.path.join(d, 'FB_NGN.EXE'), run_init=False)
        self._sspch = self.spch['segsel'][1]
        self._sdg = self.spch['dgroup']
        self._ndg = self.ngn['dgroup']

        emu.stack_sel = self._ndg
        emu.sp = 0xFFF0
        emu.dispatch_wm_create = True
        cmd_sel, cmd_base = emu.alloc(16)
        emu.uc.mem_write(cmd_base, b'\x00')
        emu.call_far(self.ngn['segsel'][1], _WINMAIN,
                     args=((self._ndg, 2), (0, 2), ((cmd_sel << 16) | 0, 4), (1, 2)),
                     dgroup=self._ndg)
        if emu.rw(self._sdg, 0xFCC) == 0:
            raise EngineError('speech engine task failed to initialise')

        self.scr, base = emu.alloc(0x1000)
        self._text_off = 0x100
        self.scb = 0
        self._scbs = {}          # voice_id -> its OPENSPEECH control block (opened once)
        self.set_voice(DEFAULT_VOICE)

    def _off(self, name):
        return self.spch['ne'].loc_of(name)[1]

    # -- voice -------------------------------------------------------------
    def available_voices(self):
        return [(vid, VOICES[vid][1]) for vid in VOICES]

    @property
    def voice(self):
        return self._voice

    def set_voice(self, voice_id):
        """Switch to the given voice, opening its control block once and reusing
        it thereafter (no per-switch OPENSPEECH, so no leak and faster switching).
        Emulator-touching: caller must serialise with speak() (the NVDA driver
        does this on its worker thread)."""
        if voice_id not in VOICES:
            voice_id = DEFAULT_VOICE
        if voice_id == self._voice:
            return
        dll, _disp, rate, bits = VOICES[voice_id]
        scb = self._scbs.get(voice_id)
        if scb is None:
            emu = self.emu
            emu.uc.mem_write(emu.lin(self.scr, 0), (dll + '\x00').encode('latin1'))
            scb, ok = emu.call_far(
                self._sspch, self._off('OPENSPEECH'),
                args=((self._ndg, 2), (0, 2), ((self.scr << 16) | 0, 4)),
                dgroup=self._sdg, ret32=True)
            if not scb:
                raise EngineError('OPENSPEECH failed for voice %s' % dll)
            self._scbs[voice_id] = scb
        self.scb = scb
        self._voice = voice_id
        self._native_fmt = (rate, bits, 1)

    # -- settings ----------------------------------------------------------
    def configure(self, volume=None, pitch=None, rate=None):
        v, p, r = self._params
        self._params = (
            _clamp(volume) if volume is not None else v,
            _clamp(pitch) if pitch is not None else p,
            _clamp(rate) if rate is not None else r,
        )

    def _apply_params(self):
        # Control-block parameter words, confirmed by measuring the output:
        #   +0 = pitch (F0), +2 = rate (duration), +4 = volume (RMS).  0-9.
        sel = (self.scb >> 16) & 0xFFFF
        off = self.scb & 0xFFFF
        vol, pit, rate = self._params
        self.emu.ww(sel, off + 0, pit)
        self.emu.ww(sel, off + 2, rate)
        self.emu.ww(sel, off + 4, vol)

    # -- synthesis ---------------------------------------------------------
    def speak(self, text, should_cancel=None, on_block=None):
        """Synthesize *text*; on_block(pcm16_bytes @ 22050/16) is called as audio appears."""
        text = (text or '').strip()
        if not text:
            return
        with self._lock:
            emu = self.emu
            emu.pcm = bytearray()
            rate, bits, _ch = self._native_fmt
            rs = Resampler(rate, OUT_RATE)
            gate = _SilenceGate(on_block) if on_block else None

            def convert(chunk):
                out = rs.feed(to_pcm16(chunk, bits))
                if gate and out:
                    gate.feed(_pack(out))

            emu.on_block = convert if on_block else None
            try:
                for piece in _split_text(text, MAX_TEXT):
                    if should_cancel and should_cancel():
                        break
                    self._speak_piece(piece, should_cancel)
            finally:
                emu.on_block = None
            if gate:
                gate.flush()

    def _speak_piece(self, text, should_cancel):
        emu = self.emu
        emu.uc.mem_write(emu.lin(self.scr, self._text_off),
                         text.encode('latin1', 'replace') + b'\x00')
        self._apply_params()
        phon, ok = emu.call_far(
            self._sspch, self._off('TEXTTOPHONETICS'),
            args=((self.scb, 4), ((self.scr << 16) | self._text_off, 4), (0, 2)),
            dgroup=self._sdg, ret32=True)
        if not phon:
            return
        emu.call_far(self._sspch, self._off('SPEAKPHONETICS'),
                     args=((self.scb, 4), (phon, 4)),
                     dgroup=self._sdg, ret32=True)
        last = len(emu.pcm)
        stable = 0
        for _ in range(40000):
            if should_cancel and should_cancel():
                break
            emu._send_to_wndproc(emu.ngn_hwnd, _MM_WOM_DONE, _HWAVEOUT, 0)
            emu._send_to_wndproc(emu.ngn_hwnd, _WM_COMMAND, _CMD_TICK, 0)
            if len(emu.pcm) != last:
                last = len(emu.pcm)
                stable = 0
            else:
                stable += 1
                if stable > 20:
                    break
        self._reset_phrase()
        # The 16-bit engine has no "free phonetics" export, so the buffer that
        # TEXTTOPHONETICS allocated leaks a GDT selector every utterance. Now that
        # the phrase has been fully synthesized, reclaim it ourselves.
        try:
            emu._GlobalFree((phon >> 16) & 0xFFFF)
        except Exception:
            pass

    def _reset_phrase(self):
        try:
            self.emu.ww(self._ndg, 0x197E, 0)
            self.emu.ww(self._ndg, 0x1980, 0)
        except Exception:
            pass


def _clamp(v, lo=ENGINE_MIN, hi=ENGINE_MAX, default=ENGINE_DEFAULT):
    try:
        v = int(v)
    except (TypeError, ValueError):
        return default
    return max(lo, min(hi, v))


def _split_text(text, limit):
    text = ' '.join(text.split())
    if not text:
        return []
    out = []
    while len(text) > limit:
        window = text[:limit + 1]
        cut = -1
        for seps in ('.!?', ',;:'):
            best = -1
            for sep in seps:
                idx = window.rfind(sep + ' ')
                if idx > best:
                    best = idx
            if best > limit // 4:
                cut = best + 1
                break
        if cut < 0:
            cut = window.rfind(' ')
        if cut <= 0:
            cut = limit
        out.append(text[:cut].strip())
        text = text[cut:].strip()
    if text:
        out.append(text)
    return [c for c in out if c]
