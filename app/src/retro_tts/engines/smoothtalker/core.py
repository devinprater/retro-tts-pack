# -*- coding: utf-8 -*-
"""
Emulation core for the Dr. Sbaitso / First Byte SmoothTalker synthesizer.

Runs the original 1990 16-bit DOS speech engine under Unicorn and returns
PCM audio.  No DOS, no DOSBox -- the engine image is a snapshot of
conventional memory taken with SBTALKER already resident, mapped verbatim
at linear 0 so every far pointer inside it stays valid.

Engine interface (recovered by reverse engineering the original binaries):
    INT 2Fh AX=0FBFBh -> ES:BX = descriptor
    [ES:BX+4] = far entry point,  ES:BX+20h = text buffer
    buffer[0] = LENGTH byte, text follows at buffer+1
    AL = 7, then CALL FAR the entry point
"""

import struct

from unicorn import (
    Uc, UcError, UC_ARCH_X86, UC_MODE_16, UC_PROT_ALL,
    UC_HOOK_INSN, UC_HOOK_INTR, UC_HOOK_BLOCK,
)
from unicorn.x86_const import (
    UC_X86_INS_IN, UC_X86_INS_OUT,
    UC_X86_REG_AX, UC_X86_REG_BX, UC_X86_REG_CX, UC_X86_REG_DX,
    UC_X86_REG_SI, UC_X86_REG_DI, UC_X86_REG_BP, UC_X86_REG_SP,
    UC_X86_REG_CS, UC_X86_REG_DS, UC_X86_REG_ES, UC_X86_REG_SS,
    UC_X86_REG_IP, UC_X86_REG_EFLAGS,
)

MEM_SIZE = 0x110000
DUMP_HDR = 16
STACK_SEG = 0x4000
STACK_TOP = 0xFFF0
SENTINEL_SEG = 0x5FFF
SENTINEL = SENTINEL_SEG * 16

FN_INIT = 0x02
FN_SPEAK = 0x07

#: Longest utterance the engine can take in one call.  buffer[0] is a single
#: length byte and the text area is exactly 0x100 bytes before the engine's
#: `outstring` area begins at buffer+0x100, so 255 characters is structural.
#: Overrunning it is worse than truncation: 256 wraps the length byte to 0
#: (silence) and 300 wraps to 44 (a fragment).  Callers must chunk.
MAX_TEXT = 255
#: Settings block, relative to the text buffer.  The field names are First
#: Byte's own: READ.EXE and SET-ECHO.EXE both embed the struct's field list,
#:
#:     outstring, gender, tone, volume, pitch, speed, startpos, action
#:
#: and Dr. Sbaitso's help text documents the ranges:
#:     ".PARAM tvps  - tvps are 4 digits representing: Tone/Volume/Pitch/Speed"
#:     "Tone(0/1), Volume(0-9), Pitch(0-9), Speed(0-9)"
#:
#:   +0x200  gender    no effect here -- SBTALKER 3.5 is the male-voice build
#:   +0x202  tone      0/1; 1 cuts the low end (thinner, brighter)
#:   +0x204  volume    0-9   (RMS 2.0 .. 31.4, no clipping at the top)
#:   +0x206  pitch     0-9   (about 41 Hz .. 154 Hz)
#:   +0x208  speed     0-9   (1.91 s .. 0.81 s for a phrase; higher = faster)
#:   +0x20A  startpos  argument to AL=0
#:   +0x20C  action    argument to AL=4
#: Values above 9 clamp to 9.  Applied by an AL=2 call.
PARAM_OFF = 0x200
DEFAULT_GENDER = 0
DEFAULT_TONE = 0
DEFAULT_VOLUME = 5
DEFAULT_PITCH = 5
DEFAULT_RATE = 5
SB_BASE = 0x220
SB_IRQ = 7

MAX_INSNS = 50_000_000
MAX_BLOCKS = 4096
SILENCE_LIMIT = 16          # safety net; a correct length byte ends cleanly


class EngineError(RuntimeError):
    pass


class _SoundBlaster:
    """Enough SB DSP + 8237 DMA to satisfy BLASTER.DRV and capture its output."""

    def __init__(self, uc, on_block=None):
        self.uc = uc
        self.on_block = on_block
        self.readfifo = []
        self.pending = []
        self.time_constant = None
        self.dma_addr = 0
        self.dma_page = 0
        self.dma_count = 0
        self.flipflop = 0
        self.pcm = bytearray()
        self.blocks = 0
        self.irq_pending = False
        self.pending_dma = None
        self.silent_run = 0
        self.finished = False

    @property
    def sample_rate(self):
        if self.time_constant is None:
            return None
        return int(round(1000000.0 / (256 - self.time_constant)))

    def read(self, port, size):
        b = SB_BASE
        if port == b + 0x0C:
            return 0x00                     # write buffer always ready
        if port == b + 0x0E:
            return 0x80 if self.readfifo else 0x00
        if port == b + 0x0A:
            return self.readfifo.pop(0) if self.readfifo else 0xFF
        return 0xFF

    def write(self, port, size, value):
        b, v = SB_BASE, value & 0xFF
        if port == b + 0x06:
            if v & 1:
                self.readfifo = []
                self.pending = []
            else:
                self.readfifo.append(0xAA)
        elif port == b + 0x0C:
            self._dsp(v)
        elif port == 0x02:
            self.dma_addr = ((self.dma_addr & 0xFF00) | v) if not self.flipflop \
                else ((self.dma_addr & 0x00FF) | (v << 8))
            self.flipflop ^= 1
        elif port == 0x03:
            self.dma_count = ((self.dma_count & 0xFF00) | v) if not self.flipflop \
                else ((self.dma_count & 0x00FF) | (v << 8))
            self.flipflop ^= 1
        elif port == 0x0C:
            self.flipflop = 0
        elif port in (0x81, 0x82, 0x83, 0x87):
            self.dma_page = v

    def _dsp(self, v):
        if self.pending:
            self.pending.append(v)
            cmd = self.pending[0]
            if cmd == 0x40 and len(self.pending) == 2:
                self.time_constant = self.pending[1]
                self.pending = []
            elif cmd in (0x14, 0x1C, 0x48) and len(self.pending) == 3:
                length = (self.pending[2] << 8 | self.pending[1]) + 1
                self.pending = []
                if cmd != 0x48:
                    self._arm(length)
            return
        if v in (0x40, 0x14, 0x1C, 0x48):
            self.pending = [v]
        elif v == 0xE1:
            self.readfifo.extend((2, 1))

    def _arm(self, length):
        phys = (self.dma_page << 16) | self.dma_addr
        self.pending_dma = (phys, length)
        self.irq_pending = True
        self.uc.emu_stop()

    def commit(self):
        if not self.pending_dma:
            return
        phys, length = self.pending_dma
        self.pending_dma = None
        data = bytes(self.uc.mem_read(phys, length))
        self.blocks += 1
        if self.on_block is not None:
            # Stream it out now so playback can start before synthesis ends.
            self.on_block(data, self.sample_rate or 11025)
        else:
            self.pcm += data
        if max(data) - min(data) < 4:
            self.silent_run += 1
            if self.silent_run >= SILENCE_LIMIT:
                self.finished = True
        else:
            self.silent_run = 0


class Engine:
    """
    Holds the engine image and synthesizes utterances.

    A fresh Unicorn instance is built per utterance: the image is a snapshot
    of an *idle, already-initialised* engine, so starting from it every time
    keeps utterances independent and needs no re-init call.
    """

    def __init__(self, image_path):
        with open(image_path, 'rb') as f:
            raw = f.read()
        if raw[:4] != b'SBR1':
            raise EngineError('engine image has a bad magic header')
        (self.res_seg, self.res_bx, self.ent_off, self.ent_seg,
         self.psp, flags) = struct.unpack('<6H', raw[4:16])
        if not flags & 1:
            raise EngineError('engine image was captured without SBTALKER resident')
        self.image = raw[DUMP_HDR:]
        self.buf_off = (self.res_bx + 0x20) & 0xFFFF
        self._uc = None
        self._sb = None
        self._state = {}
        self._params = None
        self._justRebuilt = False
        #: Set when a call did not run to completion (cancelled, stalled or
        #: faulted).  The engine is then left part-way through a far call and
        #: its state cannot be trusted -- re-entering with AL=7 happens to
        #: survive that, but AL=2 faults -- so the image is rebuilt instead.
        self._dirty = False

    # -- emulation ---------------------------------------------------------
    def reset(self):
        """Drop the emulator so the next utterance starts from a clean image."""
        self._uc = None
        self._dirty = False

    def _ensure(self):
        """
        Build the emulator once and keep it.

        The engine is a TSR: the original software called it repeatedly
        without reloading anything, so reusing the instance is what it was
        designed for -- and it avoids re-mapping 1 MB and rewriting the
        640 KB image on every utterance.  Hooks are installed once and
        dispatch through self._sb / self._state, which speak() replaces.
        """
        self._justRebuilt = False
        if self._uc is not None and not self._dirty:
            return self._uc
        self._uc = None
        self._dirty = False
        self._justRebuilt = True

        uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc.mem_map(0, MEM_SIZE, UC_PROT_ALL)
        uc.mem_write(0, self.image)

        def on_in(uc_, port, size, user):
            return self._sb.read(port, size)

        def on_out(uc_, port, size, value, user):
            self._sb.write(port, size, value)

        def on_intr(uc_, intno, user):
            state = self._state
            ax = uc_.reg_read(UC_X86_REG_AX)
            ah, al = (ax >> 8) & 0xFF, ax & 0xFF
            if intno == 0x21:
                if ah == 0x35:                          # get interrupt vector
                    off, seg = struct.unpack('<HH', uc_.mem_read(al * 4, 4))
                    uc_.reg_write(UC_X86_REG_ES, seg)
                    uc_.reg_write(UC_X86_REG_BX, off)
                elif ah == 0x25:                        # set interrupt vector
                    ds = uc_.reg_read(UC_X86_REG_DS)
                    dx = uc_.reg_read(UC_X86_REG_DX)
                    uc_.mem_write(al * 4, struct.pack('<HH', dx, ds))
                elif ah == 0x30:
                    uc_.reg_write(UC_X86_REG_AX, 0x0005)
                elif ah in (0x2A, 0x2C):
                    uc_.reg_write(UC_X86_REG_CX, 0)
                    uc_.reg_write(UC_X86_REG_DX, 0)
                uc_.reg_write(UC_X86_REG_EFLAGS,
                              uc_.reg_read(UC_X86_REG_EFLAGS) & ~0x01)
            elif intno == 0x16:                         # keyboard: never a key
                if ah in (0x01, 0x11):
                    uc_.reg_write(UC_X86_REG_EFLAGS,
                                  uc_.reg_read(UC_X86_REG_EFLAGS) | 0x40)
                uc_.reg_write(UC_X86_REG_AX, 0)
            elif intno == 0x1A and ah == 0x00:
                uc_.reg_write(UC_X86_REG_CX, (state['ticks'] >> 16) & 0xFFFF)
                uc_.reg_write(UC_X86_REG_DX, state['ticks'] & 0xFFFF)
                state['ticks'] += 1

        def on_block(uc_, address, size, user):
            state = self._state
            state['blocks'] += 1
            if state['blocks'] % 2000:
                return
            state['ticks'] += 1
            uc_.mem_write(0x46C, struct.pack('<I', state['ticks'] & 0xFFFFFFFF))
            cancel = state.get('cancel')
            if cancel is not None and cancel():
                state['cancelled'] = True
                uc_.emu_stop()

        uc.hook_add(UC_HOOK_INSN, on_in, None, 1, 0, UC_X86_INS_IN)
        uc.hook_add(UC_HOOK_INSN, on_out, None, 1, 0, UC_X86_INS_OUT)
        uc.hook_add(UC_HOOK_INTR, on_intr)
        uc.hook_add(UC_HOOK_BLOCK, on_block)

        self._uc = uc
        # A rebuilt image is back at the engine's own defaults, so any
        # settings we were asked for have to be re-applied.
        if self._params is not None:
            self._apply(uc, self._params)
        return uc

    def _push(self, uc, value):
        sp = (uc.reg_read(UC_X86_REG_SP) - 2) & 0xFFFF
        ss = uc.reg_read(UC_X86_REG_SS)
        uc.mem_write(ss * 16 + sp, struct.pack('<H', value & 0xFFFF))
        uc.reg_write(UC_X86_REG_SP, sp)

    def speak(self, text, should_cancel=None, on_block=None):
        """
        Synthesize `text`.  Returns (pcm_bytes, sample_rate); the PCM is
        8-bit unsigned mono, exactly as the Sound Blaster would have played.

        `should_cancel` is polled periodically and aborts synthesis early.
        If `on_block(data, rate)` is given, each DMA block is handed over as
        soon as the engine produces it -- playback can then begin long before
        a long utterance has finished rendering -- and the return value's PCM
        is empty.
        """
        if isinstance(text, str):
            text = text.encode('cp437', 'replace')
        text = text[:MAX_TEXT]
        if not text:
            return b'', 11025

        try:
            uc = self._ensure()
            sb = _SoundBlaster(uc, on_block=on_block)
            self._sb = sb
            self._state = {'ticks': 0, 'blocks': 0, 'cancelled': False,
                           'cancel': should_cancel}
            return self._run(uc, sb, text)
        except EngineError:
            # Don't let a damaged emulator state poison later utterances.
            self.reset()
            raise

    def configure(self, params):
        """
        Write the five-word parameter block at buffer+0x200 and re-init.

        SBAITSO2.EXE wrote 0, 0, 5, 5, 5 there before calling AL=2, so these
        are the engine's own settings.  Parameters persist on the resident
        engine until changed.
        """
        self._params = tuple(params)
        uc = self._ensure()
        # _ensure() re-applies params itself after a rebuild; only do it here
        # when it handed back an already-live emulator.
        if not self._justRebuilt:
            self._apply(uc, self._params)

    def _apply(self, uc, params):
        self._sb = _SoundBlaster(uc)
        self._state = {'ticks': 0, 'blocks': 0, 'cancelled': False,
                       'cancel': None}
        uc.mem_write(self.res_seg * 16 + self.buf_off + PARAM_OFF,
                     struct.pack('<5H', *params))
        self._setup_regs(uc, FN_INIT)
        self._loop(uc, self._sb)

    def _run(self, uc, sb, text):
        # buffer[0] is a length byte; the text itself starts at buffer+1.
        # Clamp the whole payload to the 0x100-byte text area so a full-length
        # utterance cannot spill into `outstring` at buffer+0x100.
        payload = (bytes([len(text)]) + text + b'\r\x00' + b'\x00' * 8)[:0x100]
        uc.mem_write(self.res_seg * 16 + self.buf_off, payload)
        self._setup_regs(uc, FN_SPEAK)
        self._loop(uc, sb)
        return bytes(sb.pcm), sb.sample_rate or 11025

    def _setup_regs(self, uc, func):
        uc.reg_write(UC_X86_REG_SS, STACK_SEG)
        uc.reg_write(UC_X86_REG_SP, STACK_TOP)
        uc.reg_write(UC_X86_REG_DS, self.res_seg)
        uc.reg_write(UC_X86_REG_ES, self.res_seg)
        uc.reg_write(UC_X86_REG_SI, self.buf_off)
        uc.reg_write(UC_X86_REG_DI, self.buf_off)
        uc.reg_write(UC_X86_REG_BX, self.res_bx)
        uc.reg_write(UC_X86_REG_CX, 0)
        uc.reg_write(UC_X86_REG_DX, 0)
        uc.reg_write(UC_X86_REG_BP, 0)
        uc.reg_write(UC_X86_REG_AX, func)
        uc.reg_write(UC_X86_REG_CS, self.ent_seg)
        uc.reg_write(UC_X86_REG_IP, self.ent_off)
        self._push(uc, SENTINEL_SEG)
        self._push(uc, 0x0000)

    def _loop(self, uc, sb):
        state = self._state
        clean = False
        pc = self.ent_seg * 16 + self.ent_off
        try:
            for _ in range(MAX_BLOCKS + 64):
                try:
                    uc.emu_start(pc, SENTINEL, count=MAX_INSNS)
                except UcError as e:
                    raise EngineError('emulation fault: %s' % e)
                if state['cancelled']:
                    break
                cs, ip = uc.reg_read(UC_X86_REG_CS), uc.reg_read(UC_X86_REG_IP)
                if cs * 16 + ip == SENTINEL:
                    clean = True                        # engine returned
                    break
                if not sb.irq_pending:
                    break                               # stalled
                sb.irq_pending = False
                sb.commit()
                if sb.finished or sb.blocks >= MAX_BLOCKS:
                    break
                pc = self._vector_irq(uc)
                if pc is None:
                    break
        finally:
            # Anything other than a clean return leaves the engine part-way
            # through a far call; force a rebuild before the next one.
            self._dirty = not clean

    def _vector_irq(self, uc):
        """Simulate the PIC delivering the SB completion IRQ."""
        vec = 0x08 + SB_IRQ
        off, seg = struct.unpack('<HH', uc.mem_read(vec * 4, 4))
        if not seg and not off:
            return None
        self._push(uc, uc.reg_read(UC_X86_REG_EFLAGS))
        self._push(uc, uc.reg_read(UC_X86_REG_CS))
        self._push(uc, uc.reg_read(UC_X86_REG_IP))
        uc.reg_write(UC_X86_REG_CS, seg)
        uc.reg_write(UC_X86_REG_IP, off)
        return seg * 16 + off


def to_pcm16(pcm8):
    """Sound Blaster 8-bit unsigned -> 16-bit signed, which every output path takes."""
    out = bytearray(len(pcm8) * 2)
    for i, b in enumerate(pcm8):
        v = (b - 128) << 8
        out[i*2] = v & 0xFF
        out[i*2+1] = (v >> 8) & 0xFF
    return bytes(out)


class Resampler(object):
    """
    Streaming linear resampler for 16-bit mono.

    Carries the phase and the last sample across calls, so feeding audio in
    DMA-block-sized pieces produces the same continuous result as converting
    the whole utterance at once -- resampling each block independently would
    leave a discontinuity, and therefore a click, at every boundary.
    """

    def __init__(self, src_rate, dst_rate):
        self.ratio = float(src_rate) / float(dst_rate)
        self.pos = 0.0
        self.prev = 0
        self.passthrough = (src_rate == dst_rate)

    def feed(self, pcm16):
        if self.passthrough or not pcm16:
            return pcm16
        n = len(pcm16) // 2
        src = struct.unpack('<%dh' % n, pcm16)
        out = []
        pos, prev = self.pos, self.prev
        while pos < n:
            i = int(pos)
            frac = pos - i
            a = prev if i == 0 else src[i - 1]
            b = src[i]
            out.append(int(a + (b - a) * frac))
            pos += self.ratio
        self.pos = pos - n
        self.prev = src[n - 1]
        return struct.pack('<%dh' % len(out), *out) if out else b''


def resample16(pcm16, src_rate, dst_rate):
    """Linear resample of 16-bit mono.  The engine's 8475 Hz is an odd rate
    and not every audio path accepts it, so we normalise before playback."""
    if src_rate == dst_rate or not pcm16:
        return pcm16
    n = len(pcm16) // 2
    src = struct.unpack('<%dh' % n, pcm16)
    m = max(1, int(n * dst_rate / src_rate))
    out = []
    step = (n - 1) / float(m) if m > 1 else 0
    for i in range(m):
        p = i * step
        j = int(p)
        frac = p - j
        a = src[j]
        b = src[j + 1] if j + 1 < n else a
        out.append(int(a + (b - a) * frac))
    return struct.pack('<%dh' % m, *out)
