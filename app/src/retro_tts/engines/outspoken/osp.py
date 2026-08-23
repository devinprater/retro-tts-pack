# -*- coding: utf-8 -*-
"""ctypes binding for the MacinTalk host DLL.

The DLL owns the memory and the CPU; Python owns the policy.  Traps happen a
few dozen times per utterance, so there is no reason for the trap decisions to
live in C where they are hard to change -- but the instruction loop and every
memory access stay on the C side, where they belong.

    sh build.sh          # produces build/osp_host.dll
    py -3 tools/probe_open.py
"""
import ctypes
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

#: Which build to load, decided by the *running interpreter* rather than by the
#: machine.
#:
#: NVDA was a 32-bit process for most of its life and is 64-bit only recently,
#: so an add-on that ships one binary supports one era. `build.sh` already
#: produces both; picking here is what lets the same add-on serve old and new
#: NVDA from one folder. Loading the wrong one does not fail politely -- it is
#: `OSError: [WinError 193] %1 is not a valid Win32 application`, which is
#: exactly what several other synthesizers in this user's log are dying of.
_BITS = 64 if sys.maxsize > 2 ** 32 else 32
_NAME = "osp_host.dll" if _BITS == 64 else "osp_host_x86.dll"

# Deployed inside the add-on the DLL sits beside this file; in the repo it
# lives under build/. Checking both lets one module serve both places.
_CANDIDATES = [os.environ.get("RETRO_TTS_OUTSPOKEN_HOST", ""),
               os.path.join(HERE, "libosp_host.so"),
               os.path.join(HERE, _NAME),
               os.path.join(ROOT, "build", _NAME)]
DLL = next((c for c in _CANDIDATES if os.path.isfile(c)), _CANDIDATES[-1])

# m68k_register_t, in declaration order
(D0, D1, D2, D3, D4, D5, D6, D7,
 A0, A1, A2, A3, A4, A5, A6, A7,
 PC, SR, SP, USP, ISP, MSP, SFC, DFC, VBR, CACR, CAAR,
 PREF_ADDR, PREF_DATA, PPC, IR) = range(31)

STOP = {0: "still running", 1: "returned to sentinel", 2: "INSTRUCTION BUDGET",
        3: "unhandled exception", 4: "fault", 5: "snapshot breakpoint"}

# Vector numbers worth naming when we stop on one.
VECTORS = {
    2: "bus error", 3: "address error", 4: "illegal instruction",
    5: "divide by zero", 6: "CHK", 7: "TRAPV", 8: "privilege violation",
    9: "trace", 10: "line 1010 (A-trap)", 11: "line 1111 (F-trap)",
    24: "spurious interrupt",
}


class Host(object):
    def __init__(self, ram=0x01000000, dll=None):
        path = dll or DLL
        if not os.path.isfile(path):
            raise RuntimeError(
                "%s not found -- this is the %d-bit build, so run "
                "`sh build.sh`, which produces both" % (path, _BITS))
        self.dll = path
        self.lib = ctypes.CDLL(path)
        L = self.lib
        L.osp_init.argtypes = [ctypes.c_uint]
        L.osp_call.argtypes = [ctypes.c_uint, ctypes.c_uint, ctypes.c_longlong]
        L.osp_instr_count.restype = ctypes.c_longlong
        for n in ("osp_r8", "osp_r16", "osp_r32", "osp_get_reg",
                  "osp_heap_used", "osp_magic_sentinel"):
            getattr(L, n).restype = ctypes.c_uint
        for n in ("osp_w8", "osp_w16", "osp_w32"):
            getattr(L, n).argtypes = [ctypes.c_uint, ctypes.c_uint]
        L.osp_write_block.argtypes = [ctypes.c_uint, ctypes.c_char_p, ctypes.c_int]
        L.osp_read_block.argtypes = [ctypes.c_uint, ctypes.c_char_p, ctypes.c_int]
        L.osp_set_reg.argtypes = [ctypes.c_int, ctypes.c_uint]
        L.osp_heap_init.argtypes = [ctypes.c_uint, ctypes.c_uint]
        # Everything MacinTalk Pro needs, bound OPTIONALLY.
        #
        # **NVDA holds osp_host.dll open for as long as the synthesizer is
        # loaded, so it cannot be replaced in place.** A user who updates the
        # add-on and does not restart is running new Python against an old
        # binary, and binding a symbol that binary does not export raises at
        # `Host()` -- which would take `.sp` and MacinTalk 2 down with it, for
        # want of functions neither of them uses. That is the worst failure
        # this driver has: not a wrong voice, but no voice at all.
        #
        # Missing here means Pro is unavailable and nothing else changes.
        self.has_files = True
        try:
            L.osp_set_cpu.argtypes = [ctypes.c_int]
            L.osp_set_cpu.restype = ctypes.c_int
            L.osp_add_file.argtypes = [ctypes.c_char_p, ctypes.c_int,
                                       ctypes.c_char_p, ctypes.c_int,
                                       ctypes.c_char_p, ctypes.c_int]
            L.osp_add_file.restype = ctypes.c_int
            L.osp_map_entry.argtypes = [ctypes.c_uint, ctypes.c_int]
            L.osp_map_entry.restype = ctypes.c_int
            L.osp_name_resource.argtypes = [ctypes.c_uint, ctypes.c_char_p,
                                            ctypes.c_int]
            L.osp_name_resource.restype = ctypes.c_int
            L.osp_instance_error.restype = ctypes.c_int
            L.osp_last_file_request.restype = ctypes.c_char_p
            L.osp_auto_ticks.argtypes = [ctypes.c_int]
        except AttributeError:
            self.has_files = False
        L.osp_set_trap_policy.argtypes = [ctypes.c_uint] * 3
        # The fifth argument (which file a resource came from) is new; an
        # older binary takes four and ignores what it never reads, and cdecl
        # lets the caller push more than the callee uses.
        L.osp_add_resource.argtypes = [ctypes.c_uint, ctypes.c_int,
                                       ctypes.c_char_p, ctypes.c_int,
                                       ctypes.c_int]
        L.osp_add_resource.restype = ctypes.c_uint
        L.osp_call_with_args.argtypes = [ctypes.c_uint,
                                         ctypes.POINTER(ctypes.c_uint),
                                         ctypes.c_int, ctypes.c_longlong]
        L.osp_pcm_get.argtypes = [ctypes.c_char_p, ctypes.c_int]
        # The Component Manager half is bound optionally.
        #
        # An older osp_host.dll does not export it, and NVDA holds the DLL open
        # while it is loaded, so a user can easily end up with new Python and
        # an old binary. Binding these unconditionally made that fatal for
        # *both* engines -- `.sp` needs none of this and went silent anyway,
        # which is the worst failure this driver has. Missing here means
        # MacinTalk 2 is unavailable and nothing else changes.
        self.has_components = True
        try:
            L.osp_add_component.argtypes = [ctypes.c_uint] * 4
            L.osp_add_voice.argtypes = [ctypes.c_uint, ctypes.c_uint,
                                        ctypes.c_int, ctypes.c_int]
            L.osp_open_instance.argtypes = [ctypes.c_int]
            L.osp_open_instance.restype = ctypes.c_uint
            L.osp_instance_storage.argtypes = [ctypes.c_uint]
            L.osp_instance_storage.restype = ctypes.c_uint
            L.osp_component_call.argtypes = [ctypes.c_uint, ctypes.c_int,
                                             ctypes.POINTER(ctypes.c_uint),
                                             ctypes.c_int, ctypes.c_longlong,
                                             ctypes.POINTER(ctypes.c_uint)]
        except AttributeError:
            self.has_components = False
        for n in ("osp_pcm_len", "osp_sample_rate", "osp_cb_scratch"):
            getattr(L, n).restype = ctypes.c_uint
        if L.osp_init(ram) != 0:
            raise RuntimeError("osp_init failed")
        self.ram = ram

    # --- lifecycle ----------------------------------------------------------
    def close(self):
        """Shut the emulator down and unload the DLL.

        **ctypes never calls FreeLibrary.** A CDLL object being garbage
        collected does not unload the library, so `osp_host.dll` stays mapped
        for the life of the NVDA process -- and therefore stays *locked* -- long
        after the user has switched to another synthesizer. Replacing it then
        needs NVDA to exit completely, which is how a user ends up running new
        Python against an old binary without knowing it.

        Windows reference-counts loaded modules and every `Host()` adds one, so
        this drains rather than frees once. The loop is bounded because a stuck
        handle should leave the file locked, not hang NVDA.

        Safe to call twice; the driver's terminate path is not always the only
        caller.
        """
        lib, self.lib = getattr(self, "lib", None), None
        if lib is None:
            return
        try:
            lib.osp_shutdown()
        except Exception:
            pass
        if os.name != "nt":
            return
        try:
            k32 = ctypes.windll.kernel32
            k32.GetModuleHandleW.restype = ctypes.c_void_p
            k32.GetModuleHandleW.argtypes = (ctypes.c_wchar_p,)
            name = os.path.basename(self.dll)
            for _ in range(16):
                h = k32.GetModuleHandleW(self.dll) or k32.GetModuleHandleW(name)
                if not h:
                    break
                k32.FreeLibrary(ctypes.c_void_p(h))
        except Exception:
            pass

    # --- memory -----------------------------------------------------------
    def load(self, addr, data):
        if self.lib.osp_write_block(addr, data, len(data)) != 0:
            raise ValueError("load outside RAM at 0x%X" % addr)

    def read(self, addr, n):
        buf = ctypes.create_string_buffer(n)
        if self.lib.osp_read_block(addr, buf, n) != 0:
            raise ValueError("read outside RAM at 0x%X" % addr)
        return buf.raw

    def w8(self, a, v): self.lib.osp_w8(a, v)
    def w16(self, a, v): self.lib.osp_w16(a, v)
    def w32(self, a, v): self.lib.osp_w32(a, v)
    def r8(self, a): return self.lib.osp_r8(a)
    def r16(self, a): return self.lib.osp_r16(a)
    def r32(self, a): return self.lib.osp_r32(a)

    # --- cpu --------------------------------------------------------------
    def set_reg(self, r, v): self.lib.osp_set_reg(r, v)
    def get_reg(self, r): return self.lib.osp_get_reg(r)

    def heap(self, base, size):
        self.lib.osp_heap_init(base, size)

    #: Gestalt processor values, which is how the host names a CPU.
    CPU_68000, CPU_68010, CPU_68020, CPU_68030, CPU_68040 = 1, 2, 3, 4, 5

    def name_resource(self, handle, name):
        """Give a registered resource its Mac name.

        **MacinTalk Pro looks its pieces up by name, not by id**: the engine is
        modules called `*TTS`, `*Wave`, `*Lex` and so on, and a voice's unit
        database is `EnglMBruceData`. Keyed on the Handle `add_resource`
        returned, because type and id alone no longer identify one resource.
        """
        if isinstance(name, str):
            name = name.encode("mac-roman", "replace")
        if len(name) > 63:
            raise ValueError("a Mac resource name is at most 63 characters")
        return self.lib.osp_name_resource(handle, name, len(name)) == 0

    def add_file(self, name, data=b"", rsrc=b""):
        """Register a file the engine may open: its name and its two forks.

        **Which fork is not a detail.** MacinTalk Pro's lexicon is in its own
        DATA fork, while a voice's 800 KB of units is in that voice file's
        RESOURCE fork -- which Pro reads by walking the map and seeking, not
        by asking for a Handle. Serving one where the other was asked for is
        silent corruption rather than an error.

        There is still no file *system* here: a handful of files, matched by
        name, never written.
        """
        if isinstance(name, str):
            name = name.encode("mac-roman", "replace")
        if len(name) > 63:
            raise ValueError("a Mac file name is at most 63 characters")
        if self.lib.osp_add_file(name, len(name), data, len(data),
                                 rsrc, len(rsrc)) < 0:
            raise RuntimeError("could not register the file")

    def auto_ticks(self, on=True):
        """Let low-memory Ticks ($016A) advance on its own.

        **MacinTalk Pro waits on the clock**: it reads $016A directly and
        compares it with a deadline, so without this its SpeakBuffer never
        returns. Off for the other engines on purpose -- `.sp` is
        time-sensitive and a self-advancing clock makes the same sentence
        render differently twice.
        """
        self.lib.osp_auto_ticks(1 if on else 0)

    def map_entry(self, handle, offset):
        """Where this resource's entry sits in its file's resource map.

        What `RsrcMapEntry` answers. Computed where the fork is already parsed
        -- see rsrc.Resource.map_entry -- rather than parsing the map twice.
        """
        return self.lib.osp_map_entry(handle, offset) == 0

    def set_cpu(self, proc):
        """Pick the CPU. Call before loading code; 68000 is the default.

        MacinTalk Pro's Open asks Gestalt('proc') and refuses a 68000 or a
        68010 outright -- so it wants a 68020, and that is a real requirement
        rather than a name. `.sp` and MacinTalk 2 are 68000 code and must not
        be moved off it.
        """
        if self.lib.osp_set_cpu(proc) != 0:
            raise ValueError("no such CPU: %r" % proc)

    def mem_traps(self, on=True):
        self.lib.osp_enable_mem_traps(1 if on else 0)

    def policy(self, trap_word, d0=0, a0=0):
        self.lib.osp_set_trap_policy(trap_word, d0, a0)

    def add_resource(self, restype, res_id, data, file_index=-1):
        """Register a resource under the id the *driver* asks for.

        outSPOKEN stores these 1000 above the ids `.sp` requests -- TALK 1001
        for TALK 1 -- so the caller does the mapping, not the host.

        `file_index` says which file it came from, for engines that open more
        than one. **The same type and id name different resources in different
        files**: MacinTalk Pro's `gtsg 0` is 1,032 bytes in the engine and 110
        in a voice, so a file-blind table hands back whichever was registered
        first. -1 means "belongs to no file", which is every resource the older
        engines register.

        -> the Handle, which is what name_resource and map_entry key on.
        """
        if isinstance(restype, str):
            restype = restype.encode("mac-roman")
        t = struct.unpack(">I", restype[:4].ljust(4, b" "))[0]
        h = self.lib.osp_add_resource(t, res_id, data, len(data), file_index)
        if not h:
            raise RuntimeError("no room for resource %r %d" % (restype, res_id))
        return h

    def call_with_args(self, entry, args, max_instr=200_000_000):
        arr = (ctypes.c_uint * len(args))(*args)
        return self.lib.osp_call_with_args(entry, arr, len(args), max_instr)

    # --- the Component Manager --------------------------------------------
    # MacinTalk 2 and Pro are components rather than drivers, so the host also
    # answers $A82A.  See docs/macintalk2-components.md.

    @staticmethod
    def _ostype(v):
        if isinstance(v, str):
            v = v.encode("mac-roman")
        return struct.unpack(">I", v[:4].ljust(4, b" "))[0]

    def add_component(self, ctype, subtype, manuf, entry):
        """Register a component's code, as its `thng` resource would."""
        i = self.lib.osp_add_component(self._ostype(ctype),
                                       self._ostype(subtype),
                                       self._ostype(manuf), entry)
        if i < 0:
            raise RuntimeError("no room for another component")
        return i

    def open_instance(self, component):
        tok = self.lib.osp_open_instance(component)
        if not tok:
            raise RuntimeError("cannot open component %d" % component)
        return tok

    def add_voice(self, creator, voice_id, ttvd_res_id, file_index=-1):
        """Register a voice with the Speech Manager side of the host.

        MacinTalk 2 asks for a voice's *file* and only needs the resource id
        back, so its FSSpec has always been nominal. **MacinTalk Pro opens
        that file**: it takes the FSSpec, calls OpenRFPerm on it and walks the
        fork's resource map, so `file_index` must name a file registered with
        `add_file` or Pro reads the wrong one and reports resFNotFound.
        """
        i = self.lib.osp_add_voice(self._ostype(creator), voice_id,
                                   ttvd_res_id, file_index)
        if i < 0:
            raise RuntimeError("no room for another voice")
        return i


    def instance_storage(self, token):
        return self.lib.osp_instance_storage(token)

    def component_call(self, token, what, args=(), max_instr=200_000_000):
        """-> (stop reason, result).  `args` is in declared order."""
        arr = (ctypes.c_uint * max(len(args), 1))(*args)
        res = ctypes.c_uint()
        r = self.lib.osp_component_call(token, what, arr, len(args),
                                        max_instr, ctypes.byref(res))
        if r < 0:
            raise RuntimeError("osp_component_call rejected the call (%d)" % r)
        return r, res.value

    @property
    def cm_log(self):
        """Every $A82A seen: (d0, pc, csp, [4 stack longs], served)."""
        out = []
        d0 = ctypes.c_uint(); pc = ctypes.c_uint(); csp = ctypes.c_uint()
        sv = ctypes.c_int(); words = (ctypes.c_uint * 4)()
        for i in range(self.lib.osp_cm_log_n()):
            self.lib.osp_cm_log_get(i, ctypes.byref(d0), ctypes.byref(pc),
                                    ctypes.byref(csp), words, ctypes.byref(sv))
            out.append((d0.value, pc.value, csp.value, list(words),
                        bool(sv.value)))
        return out

    @property
    def resource_requests(self):
        """Every (type, id, found) the engine asked the Resource Manager for.

        A voice that will not load reports only resNotFound, which does not say
        which resource was missing.  This does."""
        out = []
        t = ctypes.c_uint(); i = ctypes.c_int(); f = ctypes.c_int()
        for k in range(self.lib.osp_reslog_n()):
            self.lib.osp_reslog_get(k, ctypes.byref(t), ctypes.byref(i),
                                    ctypes.byref(f))
            out.append((struct.pack(">I", t.value).decode("mac-roman", "replace"),
                        i.value, bool(f.value)))
        return out

    def defer_callbacks(self, on=True):
        """Hold sound callbacks until the engine is between calls.

        Right for MacinTalk 2, wrong for `.sp`; see the note in osp_host.c."""
        self.lib.osp_defer_callbacks(1 if on else 0)

    def run_callbacks(self, max_rounds=4096, max_instr=200_000_000):
        """Be the Sound Manager until the engine stops queueing buffers.

        MacinTalk 2's speak call is asynchronous, so the audio after the first
        buffer only appears if something keeps answering the callback.
        Returns the number of callbacks run; hitting max_rounds is a stall."""
        self.lib.osp_run_callbacks.argtypes = [ctypes.c_int, ctypes.c_longlong]
        return self.lib.osp_run_callbacks(max_rounds, max_instr)

    @property
    def cp_wraps(self):
        return self.lib.osp_cp_wraps()

    # --- audio ------------------------------------------------------------
    def pcm_reset(self):
        self.lib.osp_pcm_reset()

    @property
    def pcm(self):
        n = self.lib.osp_pcm_len()
        buf = ctypes.create_string_buffer(n)
        self.lib.osp_pcm_get(buf, n)
        return buf.raw

    @property
    def buffers_taken(self): return self.lib.osp_buffers_taken()

    def buflog_n(self):
        return self.lib.osp_buflog_n()

    def buflog_lengths(self, since=0):
        """Sample counts of the bufferCmds taken, from `since` onward.

        `pcm_reset` does not clear this log, so a caller that wants one
        utterance takes the index before speaking and slices from there.
        """
        out = []
        a = ctypes.c_uint(); n = ctypes.c_uint()
        for i in range(since, self.lib.osp_buflog_n()):
            self.lib.osp_buflog_get(i, ctypes.byref(a), ctypes.byref(n))
            out.append(n.value)
        return out

    @property
    def short_buffers(self): return self.lib.osp_short_buffers()

    @property
    def sample_rate(self):
        """Hz, from the SoundHeader the driver filled in (Fixed 16.16)."""
        return self.lib.osp_sample_rate() / 65536.0

    @property
    def sentinel(self):
        return self.lib.osp_magic_sentinel()

    def call(self, entry, max_instr=20_000_000):
        return self.lib.osp_call(entry, self.sentinel, max_instr)

    # --- what happened ----------------------------------------------------
    @property
    def instr(self): return self.lib.osp_instr_count()

    @property
    def stop(self): return self.lib.osp_stop_reason()

    @property
    def stop_vector(self): return self.lib.osp_stop_vector()

    @property
    def stop_pc(self): return self.lib.osp_stop_pc()

    @property
    def stubbed(self): return self.lib.osp_stub_count()

    @property
    def faults(self):
        out = []
        addr = ctypes.c_uint(); pc = ctypes.c_uint()
        wr = ctypes.c_int(); sz = ctypes.c_int()
        for i in range(min(self.lib.osp_fault_count(), 64)):
            self.lib.osp_fault_get(i, ctypes.byref(addr), ctypes.byref(pc),
                                   ctypes.byref(wr), ctypes.byref(sz))
            out.append((addr.value, pc.value, bool(wr.value), sz.value))
        return out

    @property
    def fault_count(self): return self.lib.osp_fault_count()

    @property
    def completions(self):
        """(run, dropped) File Manager completion routines.

        MacinTalk Pro reads its lexicon asynchronously and sleeps until the
        completion routine wakes it, so a run of zero here means every module
        waiting on a lexicon lookup is still asleep. **Dropped is a fault**:
        the caller that never gets its callback waits forever."""
        return (self.lib.osp_ioc_runs(), self.lib.osp_ioc_dropped())

    # --- register snapshots at a PC -------------------------------------
    # Keyed by name so a caller can say snap["a6"] rather than count slots.
    SNAP_NAMES = ("d0 d1 d2 d3 d4 d5 d6 d7 "
                  "a0 a1 a2 a3 a4 a5 a6 a7 pc").split()

    def snap_at(self, pc, halt_on=0):
        """Record d0-d7/a0-a7/pc the first 64 times execution reaches `pc`.

        `halt_on` = N stops execution on the Nth arrival, so a probe can read
        or change emulated memory mid-run and then `resume()`.
        """
        self.lib.osp_snap_set(pc)
        self.lib.osp_snap_halt(halt_on)

    def resume(self, max_instr=400_000_000):
        """Continue from a snapshot breakpoint with every register intact."""
        self.lib.osp_resume.argtypes = [ctypes.c_longlong]
        return self.lib.osp_resume(max_instr)

    @property
    def snaps(self):
        out = []
        buf = (ctypes.c_uint * 17)()
        for i in range(self.lib.osp_snap_n()):
            self.lib.osp_snap_get(i, buf)
            out.append(dict(zip(self.SNAP_NAMES, list(buf))))
        return out

    @property
    def traps(self):
        out = []
        pc = ctypes.c_uint(); w = ctypes.c_uint(); d0 = ctypes.c_uint()
        a0 = ctypes.c_uint(); a1 = ctypes.c_uint(); sv = ctypes.c_int()
        for i in range(self.lib.osp_trap_count()):
            self.lib.osp_trap_get(i, ctypes.byref(pc), ctypes.byref(w),
                                  ctypes.byref(d0), ctypes.byref(a0),
                                  ctypes.byref(a1), ctypes.byref(sv))
            out.append((pc.value, w.value, d0.value, a0.value, a1.value,
                        bool(sv.value)))
        return out

    def trap_d0in(self, i):
        """D0 as the *caller* set it -- the selector, for the traps that take
        one there.  `traps` reports the D0 we answered with, which for
        _Gestalt and _GetTrapAddress is the less interesting half."""
        self.lib.osp_trap_d0in.restype = ctypes.c_uint
        return self.lib.osp_trap_d0in(i)

    @property
    def stackpc_convention(self):
        v = self.lib.osp_stackpc_convention()
        return {-1: "not observed", 0: "past the trap word",
                1: "at the trap word"}[v]


def driver_entries(image):
    """(open, prime, control, status, close) from the DRVR header."""
    return struct.unpack(">HHHHH", image[8:18])
