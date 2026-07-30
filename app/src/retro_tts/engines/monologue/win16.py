"""
Win16-on-Unicorn loader/emulator.

Milestone 1: load FB_SPCH.DLL, run init, far-call exports (done).
Milestone 3 (this file): multi-module loading + inter-module linking, so the real
synthesizer FB_NGN.EXE runs on top of the emulated FB_SPCH/FB_TIMER, loads the
FB_22K16 voice via LoadLibrary + resources, and its MMSYSTEM waveOut calls are
captured to yield headless PCM.

Segmentation model (validated in test_pm.py):
  descriptor index i -> linear base i*0x10000, 64K limit, 16-bit (D=0)
  selector = i<<3 ;  __AHINCR=8 (sel+8 == +64K), __AHSHIFT=3
"""
import struct, os
from unicorn import *
from unicorn.x86_const import *
try:
    from capstone import Cs, CS_ARCH_X86, CS_MODE_16
except ImportError:                       # capstone only needed for disassembly/argclean
    Cs = None
try:
    from . import ne16                     # when imported as a package (NVDA add-on)
except ImportError:
    import ne16                            # standalone

# ---- GDT descriptor helpers ------------------------------------------------
def _desc(base, limit, access, gran=0):
    d  = limit & 0xFFFF
    d |= (base & 0xFFFF) << 16
    d |= ((base >> 16) & 0xFF) << 32
    d |= (access & 0xFF) << 40
    d |= (((limit >> 16) & 0xF) | ((gran & 0xF) << 4)) << 48
    d |= ((base >> 24) & 0xFF) << 56
    return struct.pack('<Q', d)

ACC_CODE = 0x9A
ACC_DATA = 0x92
GDT_LIN, GDT_MAX = 0x00000800, 2048
MAGIC_IDX, STACK_IDX, FIRST_DYN = 1, 2, 3
RET_SENTINEL_OFF = 0x0000
IMPORT_STUB_BASE, IMPORT_STUB_STRIDE = 0x0100, 8

# host import signatures: (MODULE, ordinal) -> (name, [arg sizes L->R], ret32, cdecl)
SIGS = {
 ('KERNEL',1):('FatalExit',[2],0,0), ('KERNEL',3):('GetVersion',[],0,0),
 ('KERNEL',4):('LocalInit',[2,2,2],0,0), ('KERNEL',5):('LocalAlloc',[2,2],0,0),
 ('KERNEL',6):('LocalReAlloc',[2,2,2],0,0), ('KERNEL',7):('LocalFree',[2],0,0),
 ('KERNEL',10):('LocalSize',[2],0,0), ('KERNEL',15):('GlobalAlloc',[2,4],0,0),
 ('KERNEL',16):('GlobalReAlloc',[2,4,2],0,0), ('KERNEL',17):('GlobalFree',[2],0,0),
 ('KERNEL',18):('GlobalLock',[2],1,0), ('KERNEL',19):('GlobalUnlock',[2],0,0),
 ('KERNEL',20):('GlobalSize',[2],1,0), ('KERNEL',21):('GlobalHandle',[2],1,0),
 ('KERNEL',23):('LockSegment',[2],0,0), ('KERNEL',24):('UnlockSegment',[2],0,0),
 ('KERNEL',30):('WaitEvent',[2],0,0), ('KERNEL',48):('GetModuleUsage',[2],0,0),
 ('KERNEL',49):('GetModuleFileName',[2,4,2],0,0),
 ('KERNEL',56):('Throw',[4,2],0,0), ('KERNEL',60):('FindResource',[2,4,4],0,0),
 ('KERNEL',61):('LoadResource',[2,2],0,0), ('KERNEL',62):('LockResource',[2],1,0),
 ('KERNEL',63):('FreeResource',[2],0,0), ('KERNEL',74):('OpenFile',[4,4,2],0,0),
 ('KERNEL',81):('_lclose',[2],0,0), ('KERNEL',82):('_lread',[2,4,2],0,0),
 ('KERNEL',84):('_llseek',[2,4,2],1,0), ('KERNEL',86):('_lwrite',[2,4,2],0,0),
 ('KERNEL',88):('lstrcpy',[4,4],1,0), ('KERNEL',90):('lstrlen',[4],0,0),
 ('KERNEL',91):('InitTask',[],0,0), ('KERNEL',95):('LoadLibrary',[4],0,0),
 ('KERNEL',96):('FreeLibrary',[2],0,0), ('KERNEL',102):('DOS3Call',[],0,0),
 ('KERNEL',131):('GetDOSEnvironment',[],1,0), ('KERNEL',137):('FatalAppExit',[2,4],0,0),
 ('KERNEL',166):('WinExec',[4,2],0,0),
 ('USER',1):('MessageBox',[2,4,4,2],0,0), ('USER',5):('InitApp',[2],0,0),
 ('USER',6):('PostQuitMessage',[2],0,0), ('USER',23):('SetFocus',[2],0,0),
 ('USER',41):('CreateWindow',[4,4,4,2,2,2,2,2,2,2,4],0,0),
 ('USER',42):('ShowWindow',[2,2],0,0), ('USER',57):('RegisterClass',[4],0,0),
 ('USER',60):('GetActiveWindow',[],0,0), ('USER',107):('DefWindowProc',[2,2,2,4],1,0),
 ('USER',108):('GetMessage',[4,2,2,2],0,0), ('USER',109):('PeekMessage',[4,2,2,2,2],0,0),
 ('USER',110):('PostMessage',[2,2,2,4],0,0), ('USER',111):('SendMessage',[2,2,2,4],1,0),
 ('USER',113):('TranslateMessage',[4],0,0), ('USER',114):('DispatchMessage',[4],1,0),
 ('USER',118):('RegisterWindowMessage',[4],0,0), ('USER',124):('UpdateWindow',[2],0,0),
 ('USER',403):('UnregisterClass',[4,2],0,0), ('USER',420):('wsprintf',[4,4],0,1),
 ('USER',430):('lstrcmp',[4,4],0,0), ('USER',431):('AnsiUpper',[4],1,0),
 ('MMSYSTEM',401):('waveOutGetNumDevs',[],0,0),
 ('MMSYSTEM',402):('waveOutGetDevCaps',[2,4,2],0,0),
 ('MMSYSTEM',404):('waveOutOpen',[4,2,4,4,4,4],0,0),
 ('MMSYSTEM',405):('waveOutClose',[2],0,0),
 ('MMSYSTEM',406):('waveOutPrepareHeader',[2,4,2],0,0),
 ('MMSYSTEM',407):('waveOutUnprepareHeader',[2,4,2],0,0),
 ('MMSYSTEM',408):('waveOutWrite',[2,4,2],0,0),
 ('MMSYSTEM',409):('waveOutPause',[2],0,0), ('MMSYSTEM',410):('waveOutRestart',[2],0,0),
 ('MMSYSTEM',411):('waveOutReset',[2],0,0), ('MMSYSTEM',416):('waveOutSetVolume',[2,4],0,0),
 ('MMSYSTEM',420):('waveOutGetID',[2,4],0,0),
 ('MMSYSTEM',602):('timeSetEvent',[2,2,4,4,2],0,0),
 ('MMSYSTEM',604):('timeGetDevCaps',[4,2],0,0), ('MMSYSTEM',605):('timeBeginPeriod',[2],0,0),
 ('MMSYSTEM',606):('timeEndPeriod',[2],0,0), ('MMSYSTEM',607):('timeGetTime',[],1,0),
}
EQUATES = {('KERNEL',113):3, ('KERNEL',114):8, ('KERNEL',178):0x413}
REG_CALLS = {('KERNEL',91),('KERNEL',102),('KERNEL',56)}   # -register: no stack args


class Win16Emu:
    def __init__(self, verbose=True, trace=False):
        self.v = verbose; self.trace = trace
        self.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.md = Cs(CS_ARCH_X86, CS_MODE_16) if Cs else None
        self.argclean_tables = {}   # {module_name: {ordinal(str/int): clean_bytes}} precomputed
        #: {module_name: set(ordinals)} whose exports touch DS-relative globals and
        #: therefore must go through the DS-switching trampoline; every other
        #: inter-module import is resolved directly to native code (no Python).
        self.ds_sensitive_tables = {}
        self.mapped = set(); self.next_idx = FIRST_DYN
        self.free_by_size = {}     # nslots -> [free descriptor indices] (reclaimed selectors)
        self.local_free = {}       # dgroup sel -> {size -> [free offsets]} (reclaimed local heap)
        self.imports = []; self.import_map = {}
        self.modules = {}          # NAME -> info
        self.hmod = {}             # module handle -> info
        self.heaps = {}            # dgroup sel -> {cur,limit}
        self.local_sizes = {}; self.global_sizes = {}
        self.resources = {}        # hRsrc -> (info, res); hGlobal(sel) -> data
        self.wndproc = None; self.ngn_hwnd = None
        self.wave_fmt = None; self.pcm = bytearray()
        self.on_block = None       # optional callback(bytes) for streaming PCM as it is produced
        self.trap = None; self.handlers = {}
        self._next_hmod = 0x0100; self._next_hwnd = 0x2000; self._time = 0
        self.files = {}; self._next_file = 0x0020; self._next_hrsrc = 0x8000
        self._setup_gdt(); self._setup_magic(); self._setup_stack()
        self._install_hooks(); self._register_handlers(); self._setup_psp()

    def _setup_psp(self):
        # fake PSP + empty environment for the CRT task startup
        self.env_sel,eb=self.alloc(0x100); self.uc.mem_write(eb, b'\x00\x00')
        self.psp_sel,pb=self.alloc(0x100); self.uc.mem_write(pb, b'\x00'*0x100)
        self.ww(self.psp_sel, 0x2c, self.env_sel)   # PSP[0x2c] = environment selector

    # ---- memory / selectors ----
    def _map_slot(self, idx, n=1):
        for k in range(idx, idx+n):
            if k not in self.mapped:
                self.uc.mem_map(k*0x10000, 0x10000); self.mapped.add(k)
    def _set_desc(self, idx, access):
        self.uc.mem_write(GDT_LIN + idx*8, _desc(idx*0x10000, 0xFFFF, access))
    def alloc(self, nbytes, code=False):
        n = max(1, (nbytes + 0xFFFF)//0x10000)
        pool = self.free_by_size.get(n)
        if pool:                                # reuse a reclaimed selector of the same size
            idx = pool.pop()
        else:
            idx = self.next_idx; self.next_idx += n
            self._map_slot(idx, n)
        for k in range(idx, idx+n): self._set_desc(k, ACC_CODE if code else ACC_DATA)
        return (idx<<3, idx*0x10000)

    def _free_sel(self, sel, nbytes):
        n = max(1, (nbytes + 0xFFFF)//0x10000)
        self.free_by_size.setdefault(n, []).append((sel & 0xFFFF) >> 3)
    def lin(self, sel, off): return (sel>>3)*0x10000 + (off & 0xFFFF)
    def r8(self,s,o): return self.uc.mem_read(self.lin(s,o),1)[0]
    def rw(self,s,o): return struct.unpack('<H', self.uc.mem_read(self.lin(s,o),2))[0]
    def rd(self,s,o): return struct.unpack('<I', self.uc.mem_read(self.lin(s,o),4))[0]
    def ww(self,s,o,v): self.uc.mem_write(self.lin(s,o), struct.pack('<H', v & 0xFFFF))
    def wd(self,s,o,v): self.uc.mem_write(self.lin(s,o), struct.pack('<I', v & 0xFFFFFFFF))
    def wr_at(self,lin_addr,v32): self.uc.mem_write(lin_addr, struct.pack('<I', v32 & 0xFFFFFFFF))
    def read_cstr_far(self, fp, maxlen=260):
        s,o = (fp>>16)&0xFFFF, fp&0xFFFF; out=b''
        for _ in range(maxlen):
            c=self.r8(s,o); o=(o+1)&0xFFFF
            if c==0: break
            out+=bytes([c])
        return out
    def res_arg(self, fp):
        """MAKEINTRESOURCE-aware: segment 0 -> integer id, else string."""
        if (fp>>16)&0xFFFF == 0: return fp & 0xFFFF
        return self.read_cstr_far(fp).decode('latin1')

    # ---- setup ----
    def _setup_gdt(self):
        self._map_slot(0); self.uc.mem_write(GDT_LIN, b'\x00'*8*GDT_MAX)
        self.uc.reg_write(UC_X86_REG_GDTR, (0, GDT_LIN, GDT_MAX*8-1, 0))
        self.uc.reg_write(UC_X86_REG_CR0, self.uc.reg_read(UC_X86_REG_CR0) | 1)
    def _setup_magic(self):
        self._map_slot(MAGIC_IDX); self._set_desc(MAGIC_IDX, ACC_CODE)
        self.magic_sel = MAGIC_IDX<<3; self.magic_base = MAGIC_IDX*0x10000
        self.uc.mem_write(self.magic_base, b'\xCC'*0x10000)
    def _setup_stack(self):
        self._map_slot(STACK_IDX); self._set_desc(STACK_IDX, ACC_DATA)
        self.stack_sel = STACK_IDX<<3; self.sp = 0xFFF0
    def _install_hooks(self):
        self.uc.hook_add(UC_HOOK_CODE, self._hk_code, begin=self.magic_base, end=self.magic_base+0xFFFF)
        self.uc.hook_add(UC_HOOK_MEM_READ_UNMAPPED|UC_HOOK_MEM_WRITE_UNMAPPED|UC_HOOK_MEM_FETCH_UNMAPPED, self._hk_badmem)
        self.uc.hook_add(UC_HOOK_INSN_INVALID, self._hk_badinsn)
        if self.trace:
            self.uc.hook_add(UC_HOOK_CODE, self._hk_trace)
    def _hk_trace(self, uc, address, size, ud):
        cs=uc.reg_read(UC_X86_REG_CS)
        try:
            code=uc.mem_read(address,size)
            for i in self.md.disasm(bytes(code), uc.reg_read(UC_X86_REG_IP)):
                print(f"    {cs:04x}:{i.address:04x} {i.mnemonic} {i.op_str}"); break
        except: pass
    def _hk_code(self, uc, address, size, ud):
        off = address - self.magic_base
        if off == RET_SENTINEL_OFF: self.trap = ('return', 0)
        elif off >= IMPORT_STUB_BASE: self.trap = ('import', (off-IMPORT_STUB_BASE)//IMPORT_STUB_STRIDE)
        else: self.trap = ('magic?', off)
        uc.emu_stop()
    def _hk_badmem(self, uc, access, address, size, value, ud):
        ip=uc.reg_read(UC_X86_REG_CS)<<16 | uc.reg_read(UC_X86_REG_IP)
        print(f"  !! bad mem access={access} addr=0x{address:x} size={size} at CS:IP=0x{ip:08x}")
        self.trap=('badmem',address); uc.emu_stop(); return False
    def _hk_badinsn(self, uc, ud):
        print(f"  !! invalid insn at CS:IP={uc.reg_read(UC_X86_REG_CS):04x}:{uc.reg_read(UC_X86_REG_IP):04x}")
        self.trap=('badinsn',0); uc.emu_stop(); return False

    # ---- stack ----
    def push16(self,v): self.sp=(self.sp-2)&0xFFFF; self.ww(self.stack_sel,self.sp,v)
    def push32(self,v): self.push16((v>>16)&0xFFFF); self.push16(v&0xFFFF)

    # ---- module loading ----
    def load(self, path, run_init=True):
        ne = ne16.NEFile(path)
        info={'ne':ne,'segsel':{},'name':ne.module,'path':path}
        for s in ne.segments:
            size=max(s.minalloc or len(s.data), len(s.data), 1)
            if s.idx==ne.auto_ds: size=0x10000
            sel,base=self.alloc(size, code=not s.is_data)
            if s.data: self.uc.mem_write(base, bytes(s.data))
            info['segsel'][s.idx]=sel
            if self.v: print(f"  [{ne.module}] seg{s.idx} {'DATA' if s.is_data else 'CODE'} -> sel {sel:#06x} base 0x{base:x}")
        info['dgroup']=info['segsel'].get(ne.auto_ds, 0)
        if info['dgroup']:
            static=(len(ne.segments[ne.auto_ds-1].data)+1)&~1
            self.heaps[info['dgroup']]={'cur':max(static,0x10),'limit':0xFFF0}
        # module handle
        h=self._next_hmod; self._next_hmod+=8
        info['hmodule']=h; self.hmod[h]=info
        self.modules[ne.module]=info
        pre=self.argclean_tables.get(ne.module)
        if pre is not None:
            info['argclean']={int(k):v for k,v in pre.items()}
        else:
            info['argclean']=self._extract_argclean(ne)
        self._apply_relocs(ne, info)
        info['entry']=((ne.csip>>16)&0xFFFF, ne.csip & 0xFFFF)
        if run_init and (ne.flags & 0x8000):   # library
            self.init_dll(info)
        return info

    def _extract_argclean(self, ne):
        ac={}
        if not self.md: return ac        # capstone unavailable; rely on argclean_tables
        code=bytes(ne.segments[0].data)
        for name,ordn in ne.exports.items():
            loc=ne.entries.get(ordn)
            if not loc or loc[0]!=1: continue
            off=loc[1]
            for i in self.md.disasm(code[off:off+0x600], off):
                if i.mnemonic=='retf':
                    ac[ordn]=int(i.op_str,0) if i.op_str else 0; break
                if i.mnemonic=='ret':
                    ac[ordn]=int(i.op_str,0) if i.op_str else 0; break
        return ac

    def _import_stub(self, mod, key):
        k=(mod,key)
        if k not in self.import_map:
            self.import_map[k]=len(self.imports); self.imports.append(k)
        return self.magic_sel, IMPORT_STUB_BASE + self.import_map[k]*IMPORT_STUB_STRIDE

    def _resolve(self, ne, info, r):
        kind=r['rtype']&3
        if kind==ne16.RK_INTERNAL:
            segn=r['t1']&0xFF
            if segn==0xFF:
                seg,off=ne.entries[r['t2']]; return (info['segsel'][seg]<<16)|off
            return (info['segsel'][segn]<<16)|r['t2']
        mod=ne.modules[r['t1']-1].upper()
        if kind==ne16.RK_IMPORDINAL: key=r['t2']
        else:
            nm,_=ne._pstr(ne.impnam_off+r['t2']); key=('NAME',nm.decode('latin1'))
        if (mod,key) in EQUATES: return EQUATES[(mod,key)]
        # inter-module import into an already-loaded module: resolve DS-safe
        # exports directly to native code (the emulated far call runs in Unicorn
        # with no Python round-trip); DS-sensitive ones use the trampoline stub.
        tinfo=self.modules.get(mod)
        if tinfo is not None:
            tne=tinfo['ne']
            ordn=key if isinstance(key,int) else tne.exports.get(key[1].upper())
            if ordn is not None and ordn not in self.ds_sensitive_tables.get(mod, ()):
                loc=tne.entries.get(ordn)
                if loc:
                    seg,off=loc
                    return (tinfo['segsel'][seg]<<16)|off
        sel,off=self._import_stub(mod,key); return (sel<<16)|off

    def _apply_relocs(self, ne, info):
        for s in ne.segments:
            if not s.relocs: continue
            base=self.lin(info['segsel'][s.idx],0)
            for r in s.relocs:
                val=self._resolve(ne,info,r); atype=r['atype']
                additive=bool(r['rtype']&ne16.RK_ADDITIVE); target=r['srcoff']
                def wa(o):
                    if atype==ne16.RA_LOBYTE:
                        cur=self.uc.mem_read(base+o,1)[0] if additive else 0
                        self.uc.mem_write(base+o, bytes([(val+cur)&0xFF]))
                    elif atype==ne16.RA_OFFSET:
                        cur=struct.unpack('<H',self.uc.mem_read(base+o,2))[0] if additive else 0
                        self.uc.mem_write(base+o, struct.pack('<H',(val+cur)&0xFFFF))
                    elif atype==ne16.RA_SEGMENT:
                        cur=struct.unpack('<H',self.uc.mem_read(base+o,2))[0] if additive else 0
                        self.uc.mem_write(base+o, struct.pack('<H',(((val>>16)&0xFFFF)+cur)&0xFFFF))
                    elif atype==ne16.RA_FAR:
                        self.uc.mem_write(base+o, struct.pack('<HH', val&0xFFFF,(val>>16)&0xFFFF))
                if additive: wa(target)
                else:
                    seen=0
                    while target!=0xFFFF and seen<100000:
                        nxt=struct.unpack('<H',self.uc.mem_read(base+target,2))[0]
                        wa(target); target=nxt; seen+=1

    # ---- import dispatch ----
    def dispatch_import(self, idx):
        mod,key=self.imports[idx]
        if mod in self.modules:      # inter-module -> real loaded code
            return self._call_loaded(mod,key)
        name,argsz,ret32,cdecl=SIGS.get((mod,key),(str(key),[],0,0))
        off=4; vals=[]
        if cdecl:                       # cdecl: args pushed right-to-left -> arg1 nearest SP
            for sz in argsz:
                if sz==2: vals.append(self.rw(self.stack_sel,(self.sp+off)&0xFFFF)); off+=2
                else: vals.append(self.rd(self.stack_sel,(self.sp+off)&0xFFFF)); off+=4
        else:                           # pascal: args pushed left-to-right -> arg1 deepest
            vr=[]
            for sz in reversed(argsz):
                if sz==2: vr.append(self.rw(self.stack_sel,(self.sp+off)&0xFFFF)); off+=2
                else: vr.append(self.rd(self.stack_sel,(self.sp+off)&0xFFFF)); off+=4
            vals=list(reversed(vr))
        # preserve caller regs across the handler (handlers may run nested emulated
        # code via call_far, e.g. LoadLibrary->init, PostMessage->wndproc, which
        # clobber DS/ES/SI/DI/BP; Win16 pascal requires these preserved)
        sregs={r:self.uc.reg_read(r) for r in
               (UC_X86_REG_DS,UC_X86_REG_ES,UC_X86_REG_SI,UC_X86_REG_DI,UC_X86_REG_BP)}
        h=self.handlers.get((mod,name))
        if h:
            res=h(*vals); result=res[0] if isinstance(res,tuple) else res
        else:
            if self.v: print(f"    [stub] {mod}.{name}{tuple(hex(v) for v in vals)} -> 0"); result=0
        for r,val in sregs.items(): self.uc.reg_write(r,val)
        ret_ip=self.rw(self.stack_sel,self.sp); ret_cs=self.rw(self.stack_sel,(self.sp+2)&0xFFFF)
        clean=0 if cdecl else sum(argsz)
        self.sp=(self.sp+4+clean)&0xFFFF
        self.uc.reg_write(UC_X86_REG_AX,result&0xFFFF)
        if ret32: self.uc.reg_write(UC_X86_REG_DX,(result>>16)&0xFFFF)
        self.uc.reg_write(UC_X86_REG_CS,ret_cs); self.uc.reg_write(UC_X86_REG_IP,ret_ip)

    def _call_loaded(self, mod, key):
        info=self.modules[mod]; ne=info['ne']
        ordn=key if isinstance(key,int) else ne.exports[key[1].upper()]
        seg,off=ne.entries[ordn]; tsel=info['segsel'][seg]
        nclean=info['argclean'].get(ordn,0)
        raw=bytes(self.uc.mem_read(self.lin(self.stack_sel,(self.sp+4)&0xFFFF), nclean)) if nclean else b''
        caller_ds=self.uc.reg_read(UC_X86_REG_DS)
        outer_sp=self.sp
        if self.trace: print(f"    -> {mod}.{key if isinstance(key,int) else key[1]} (clean {nclean})")
        res=self._call_raw(tsel, off, raw, info['dgroup'])
        self.sp=outer_sp
        ret_ip=self.rw(self.stack_sel,self.sp); ret_cs=self.rw(self.stack_sel,(self.sp+2)&0xFFFF)
        self.sp=(self.sp+4+nclean)&0xFFFF
        self.uc.reg_write(UC_X86_REG_AX,res&0xFFFF); self.uc.reg_write(UC_X86_REG_DX,(res>>16)&0xFFFF)
        self.uc.reg_write(UC_X86_REG_DS,caller_ds)
        self.uc.reg_write(UC_X86_REG_CS,ret_cs); self.uc.reg_write(UC_X86_REG_IP,ret_ip)

    # ---- driver ----
    def _run(self, max_iters=2_000_000):
        it=0
        while True:
            it+=1
            if it>max_iters: print("  !! iteration cap"); return False
            self.trap=None; ip=self.uc.reg_read(UC_X86_REG_IP)
            self.uc.reg_write(UC_X86_REG_SP,self.sp)
            try:
                self.uc.emu_start(ip, 0xFFFFFFFE, 0, 30_000_000)
            except UcError as e:
                print(f"  UcError {e} at CS:IP={self.uc.reg_read(UC_X86_REG_CS):#06x}:{self.uc.reg_read(UC_X86_REG_IP):#06x}")
                return False
            self.sp=self.uc.reg_read(UC_X86_REG_SP)
            k=self.trap[0] if self.trap else None
            if k=='return': return True
            if k=='import': self.dispatch_import(self.trap[1]); continue
            if k is None:   # instruction cap hit with no trap -> spinning in internal code
                cs=self.uc.reg_read(UC_X86_REG_CS); ipn=self.uc.reg_read(UC_X86_REG_IP)
                print(f"  !! SPIN at CS:IP={cs:#06x}:{ipn:#06x}")
                if self.md:
                    try:
                        code=bytes(self.uc.mem_read((cs>>3)*0x10000+ipn, 32))
                        for i in self.md.disasm(code, ipn):
                            print(f"       {i.address:04x} {i.mnemonic} {i.op_str}")
                    except: pass
                return False
            print(f"  stopped: {self.trap}"); return False

    def _call_raw(self, sel, off, raw, dgroup):
        if raw:
            self.sp=(self.sp-len(raw))&0xFFFF
            self.uc.mem_write(self.lin(self.stack_sel,self.sp), raw)
        self.push16(self.magic_sel); self.push16(RET_SENTINEL_OFF)
        self.cur_dgroup=dgroup
        self.uc.reg_write(UC_X86_REG_DS,dgroup); self.uc.reg_write(UC_X86_REG_ES,dgroup)
        self.uc.reg_write(UC_X86_REG_SS,self.stack_sel); self.uc.reg_write(UC_X86_REG_SP,self.sp)
        self.uc.reg_write(UC_X86_REG_CS,sel); self.uc.reg_write(UC_X86_REG_IP,off)
        self._run()
        return (self.uc.reg_read(UC_X86_REG_DX)<<16)|self.uc.reg_read(UC_X86_REG_AX)

    def call_far(self, sel, off, args=(), dgroup=None, ret32=False, setregs=None):
        self.cur_dgroup=dgroup if dgroup is not None else sel
        for v,sz in args:
            if sz==2: self.push16(v)
            else: self.push32(v)
        self.push16(self.magic_sel); self.push16(RET_SENTINEL_OFF)
        if dgroup is not None:
            self.uc.reg_write(UC_X86_REG_DS,dgroup); self.uc.reg_write(UC_X86_REG_ES,dgroup)
        self.uc.reg_write(UC_X86_REG_SS,self.stack_sel); self.uc.reg_write(UC_X86_REG_SP,self.sp)
        if setregs:
            for r,val in setregs.items(): self.uc.reg_write(r,val)
        self.uc.reg_write(UC_X86_REG_CS,sel); self.uc.reg_write(UC_X86_REG_IP,off)
        ok=self._run()
        ax=self.uc.reg_read(UC_X86_REG_AX); dx=self.uc.reg_read(UC_X86_REG_DX)
        return (((dx<<16)|ax) if ret32 else ax), ok

    def call_export(self, info, name, args=(), ret32=False):
        seg,off=info['ne'].loc_of(name); sel=info['segsel'][seg]
        if self.v: print(f"  -> call {info['name']}.{name} sel {sel:#06x}:{off:#06x} args={args}")
        return self.call_far(sel, off, args=args, dgroup=info['dgroup'], ret32=ret32)

    def init_dll(self, info):
        seg,off=info['entry']; sel=info['segsel'][seg]; ne=info['ne']
        if self.v: print(f"  -> init {info['name']} sel {sel:#06x}:{off:#06x} (heap {ne.heap})")
        regs={UC_X86_REG_DI:info['hmodule'], UC_X86_REG_CX:ne.heap or 0,
              UC_X86_REG_ES:0, UC_X86_REG_SI:0}
        ax,ok=self.call_far(sel, off, args=(), dgroup=info['dgroup'], setregs=regs)
        if self.v: print(f"     {info['name']} init AX={ax:#06x} ok={ok}")
        return ax,ok

    # ---- handlers ----
    def _register_handlers(self):
        h=self.handlers
        h[('KERNEL','GetVersion')]=lambda:(0x0A03,None)
        h[('KERNEL','LocalInit')]=lambda seg,s,e:(1,None)
        h[('KERNEL','LocalAlloc')]=self._LocalAlloc
        h[('KERNEL','LocalReAlloc')]=self._LocalReAlloc
        h[('KERNEL','LocalFree')]=self._LocalFree
        h[('KERNEL','LocalSize')]=lambda p:(self.local_sizes.get((self.cur_dgroup,p),0),None)
        h[('KERNEL','GlobalAlloc')]=self._GlobalAlloc
        h[('KERNEL','GlobalReAlloc')]=self._GlobalReAlloc
        h[('KERNEL','GlobalFree')]=self._GlobalFree
        h[('KERNEL','GlobalLock')]=lambda hm:((hm<<16)|0,None)
        h[('KERNEL','GlobalUnlock')]=lambda hm:(0,None)
        h[('KERNEL','GlobalSize')]=lambda hm:(self.global_sizes.get(hm,0),None)
        h[('KERNEL','GlobalHandle')]=lambda hm:((hm<<16)|hm,None)
        h[('KERNEL','LockSegment')]=lambda s:(s if s!=0xFFFF else self.cur_dgroup,None)
        h[('KERNEL','UnlockSegment')]=lambda s:(0,None)
        h[('KERNEL','GetModuleUsage')]=lambda hm:(1,None)
        h[('KERNEL','GetModuleFileName')]=self._GetModuleFileName
        h[('KERNEL','InitTask')]=self._InitTask
        h[('KERNEL','WaitEvent')]=lambda t:(0,None)
        h[('KERNEL','WinExec')]=self._WinExec
        h[('KERNEL','DOS3Call')]=self._DOS3Call
        h[('KERNEL','GetDOSEnvironment')]=lambda:((self.stack_sel<<16)|0,None)
        h[('KERNEL','lstrlen')]=lambda fp:(len(self.read_cstr_far(fp)),None)
        h[('KERNEL','lstrcpy')]=self._lstrcpy
        h[('KERNEL','LoadLibrary')]=self._LoadLibrary
        h[('KERNEL','FreeLibrary')]=lambda hm:(0,None)
        h[('KERNEL','OpenFile')]=self._OpenFile
        h[('KERNEL','_lread')]=self._lread
        h[('KERNEL','_llseek')]=self._llseek
        h[('KERNEL','_lclose')]=self._lclose
        h[('KERNEL','_lwrite')]=lambda hf,b,n:(n,None)
        h[('KERNEL','FindResource')]=self._FindResource
        h[('KERNEL','LoadResource')]=self._LoadResource
        h[('KERNEL','LockResource')]=self._LockResource
        h[('KERNEL','FreeResource')]=self._FreeResource
        h[('KERNEL','FatalAppExit')]=self._FatalAppExit
        h[('KERNEL','FatalExit')]=lambda c:(0,None)
        # USER
        h[('USER','InitApp')]=lambda a:(1,None)
        h[('USER','RegisterClass')]=self._RegisterClass
        h[('USER','UnregisterClass')]=lambda a,b:(1,None)
        h[('USER','CreateWindow')]=self._CreateWindow
        h[('USER','ShowWindow')]=lambda hw,c:(1,None)
        h[('USER','UpdateWindow')]=lambda hw:(0,None)
        h[('USER','DefWindowProc')]=lambda hw,m,wp,lp:(0,None)
        h[('USER','GetMessage')]=lambda *a:(0,None)   # WM_QUIT -> exit loop
        h[('USER','PeekMessage')]=lambda *a:(0,None)
        h[('USER','TranslateMessage')]=lambda p:(0,None)
        h[('USER','DispatchMessage')]=lambda p:(0,None)
        h[('USER','PostMessage')]=self._PostMessage
        h[('USER','SendMessage')]=self._PostMessage
        h[('USER','PostQuitMessage')]=lambda c:(0,None)
        h[('USER','GetActiveWindow')]=lambda:(0,None)
        h[('USER','SetFocus')]=lambda hw:(0,None)
        h[('USER','MessageBox')]=self._MessageBox
        h[('USER','RegisterWindowMessage')]=lambda fp:(0xC000+(sum(self.read_cstr_far(fp))&0x3FF),None)
        h[('USER','AnsiUpper')]=self._AnsiUpper
        h[('USER','wsprintf')]=self._wsprintf
        h[('USER','lstrcmp')]=lambda a,b:((self.read_cstr_far(a)>self.read_cstr_far(b))-(self.read_cstr_far(a)<self.read_cstr_far(b)),None)
        # MMSYSTEM
        h[('MMSYSTEM','waveOutGetNumDevs')]=lambda:(1,None)
        h[('MMSYSTEM','waveOutGetDevCaps')]=lambda *a:(0,None)
        h[('MMSYSTEM','waveOutOpen')]=self._waveOutOpen
        h[('MMSYSTEM','waveOutClose')]=lambda hw:(0,None)
        h[('MMSYSTEM','waveOutPrepareHeader')]=lambda hw,fp,cb:(0,None)
        h[('MMSYSTEM','waveOutUnprepareHeader')]=lambda hw,fp,cb:(0,None)
        h[('MMSYSTEM','waveOutWrite')]=self._waveOutWrite
        h[('MMSYSTEM','waveOutPause')]=lambda hw:(0,None)
        h[('MMSYSTEM','waveOutRestart')]=lambda hw:(0,None)
        h[('MMSYSTEM','waveOutReset')]=lambda hw:(0,None)
        h[('MMSYSTEM','waveOutSetVolume')]=lambda hw,v:(0,None)
        h[('MMSYSTEM','waveOutGetID')]=lambda hw,fp:(0,None)
        h[('MMSYSTEM','timeSetEvent')]=lambda d,r,fp,u,f:(1,None)
        h[('MMSYSTEM','timeGetDevCaps')]=lambda fp,cb:(0,None)
        h[('MMSYSTEM','timeBeginPeriod')]=lambda p:(0,None)
        h[('MMSYSTEM','timeEndPeriod')]=lambda p:(0,None)
        h[('MMSYSTEM','timeGetTime')]=self._timeGetTime

    # heap  (with reclamation -- honoring the engine's free calls avoids leaking
    #        GDT selectors / local-heap space over a long session)
    def _LocalAlloc(self, flags, size):
        heap=self.heaps.get(self.cur_dgroup)
        if heap is None: heap={'cur':0x1000,'limit':0xFFF0}; self.heaps[self.cur_dgroup]=heap
        size=((size+1)&~1) or 2
        pool=self.local_free.get(self.cur_dgroup,{}).get(size)
        if pool:
            p=pool.pop()
        elif heap['cur']+size<=heap['limit']:
            p=heap['cur']; heap['cur']+=size
        else:
            print("    !! LocalAlloc OOM"); return (0,None)
        self.local_sizes[(self.cur_dgroup,p)]=size
        if flags&0x40: self.uc.mem_write(self.lin(self.cur_dgroup,p), b'\x00'*size)
        return (p,None)
    def _LocalFree(self, p):
        sz=self.local_sizes.pop((self.cur_dgroup,p),None)
        if sz: self.local_free.setdefault(self.cur_dgroup,{}).setdefault(sz,[]).append(p)
        return (0,None)
    def _LocalReAlloc(self, p, size, flags):
        old=self.local_sizes.get((self.cur_dgroup,p),0)
        if size<=old: return (p,None)
        np=self._LocalAlloc(flags,size)[0]
        if np and old:
            self.uc.mem_write(self.lin(self.cur_dgroup,np), self.uc.mem_read(self.lin(self.cur_dgroup,p),old))
            self._LocalFree(p)
        return (np,None)
    def _GlobalAlloc(self, flags, size):
        sel,base=self.alloc(size if size else 1); self.global_sizes[sel]=size
        if flags&0x40: self.uc.mem_write(base, b'\x00'*(((size or 1)+0xFFFF)//0x10000*0x10000))
        return (sel,None)
    def _GlobalFree(self, hm):
        sz=self.global_sizes.pop(hm,None)
        if sz is not None: self._free_sel(hm, sz or 1)
        return (0,None)
    def _FreeResource(self, hg):
        sz=self.global_sizes.pop(hg,None)
        if sz is not None: self._free_sel(hg, sz or 1)
        self.resources.pop(hg,None)
        return (0,None)
    def _GlobalReAlloc(self, hm, size, flags):
        old=self.global_sizes.get(hm,0)
        if size<=old: self.global_sizes[hm]=size; return (hm,None)
        ns,nb=self.alloc(size); self.global_sizes[ns]=size
        if old:
            self.uc.mem_write(nb, self.uc.mem_read(self.lin(hm,0),old))
            self.global_sizes.pop(hm,None); self._free_sel(hm, old)
        return (ns,None)
    def _lstrcpy(self, dst, src):
        s=self.read_cstr_far(src)+b'\x00'
        ds,do=(dst>>16)&0xFFFF,dst&0xFFFF; self.uc.mem_write(self.lin(ds,do), s)
        return (dst,None)
    def _GetModuleFileName(self, hm, fp, cb):
        info=self.hmod.get(hm); name=(info['path'] if info else 'C:\\FB.DLL').encode('latin1')[:cb-1]+b'\x00'
        ds,do=(fp>>16)&0xFFFF,fp&0xFFFF; self.uc.mem_write(self.lin(ds,do), name)
        return (len(name)-1,None)
    def _InitTask(self):
        # -register: return task setup. AX=1 success.
        self.uc.reg_write(UC_X86_REG_AX,1)
        self.uc.reg_write(UC_X86_REG_CX,0x2000)   # stack size
        self.uc.reg_write(UC_X86_REG_DX,1)        # nCmdShow
        self.uc.reg_write(UC_X86_REG_DI, self.cur_dgroup)  # hInstance
        self.uc.reg_write(UC_X86_REG_SI,0)        # hPrevInstance
        self.uc.reg_write(UC_X86_REG_BX,0x80)     # cmdline offset (empty)
        self.uc.reg_write(UC_X86_REG_ES,self.psp_sel)  # PSP selector
        return (1,None)
    def _DOS3Call(self):
        ax=self.uc.reg_read(UC_X86_REG_AX); ah=(ax>>8)&0xFF
        if ah==0x30: self.uc.reg_write(UC_X86_REG_AX,0x0005)
        elif ah==0x19: self.uc.reg_write(UC_X86_REG_AX,(ax&0xFF00)|2)
        else: self.uc.reg_write(UC_X86_REG_AX, ax&0xFF00)
        self.uc.reg_write(UC_X86_REG_EFLAGS, self.uc.reg_read(UC_X86_REG_EFLAGS)&~1)
        return (self.uc.reg_read(UC_X86_REG_AX),None)
    def _WinExec(self, cmd, show):
        c=self.read_cstr_far(cmd).decode('latin1','replace')
        if self.v: print(f"    WinExec('{c}') -> stubbed (33)")
        return (33,None)
    def _FatalAppExit(self, code, fp):
        print(f"    !! FatalAppExit: {self.read_cstr_far(fp).decode('latin1','replace')}")
        return (0,None)
    def _MessageBox(self, hw, txt, cap, style):
        print(f"    MessageBox: [{self.read_cstr_far(cap).decode('latin1','replace')}] {self.read_cstr_far(txt).decode('latin1','replace')}")
        return (1,None)
    def _timeGetTime(self):
        self._time+=10; return (self._time,None)
    def _AnsiUpper(self, fp):
        seg=(fp>>16)&0xFFFF; off=fp&0xFFFF
        if seg==0:                    # MAKEINTRESOURCE-style single char in low byte
            c=off&0xFF; u=ord(chr(c).upper()) if 32<=c<127 else c
            return ((fp & 0xFFFF0000)|u, None)
        out=bytearray()               # uppercase the string in place
        for i in range(1024):
            c=self.r8(seg,(off+i)&0xFFFF)
            if c==0: break
            out.append(ord(chr(c).upper()) if 97<=c<=122 else c)
        if out: self.uc.mem_write(self.lin(seg,off), bytes(out))
        return (fp, None)

    # library + resources
    def _LoadLibrary(self, fp):
        name=self.read_cstr_far(fp).decode('latin1').strip()
        base=name.split('\\')[-1]
        if '.' not in base: base+='.DLL'
        key=base.upper().rsplit('.',1)[0]
        for m in self.modules:
            if m.upper()==key:
                if self.v: print(f"    LoadLibrary('{name}') -> already loaded h={self.modules[m]['hmodule']:#x}")
                return (self.modules[m]['hmodule'],None)
        path=os.path.join(
            getattr(self, 'bin_dir', os.path.join(os.path.dirname(__file__), 'bin')),
            base,
        )
        if not os.path.exists(path):
            print(f"    !! LoadLibrary('{name}') NOT FOUND ({path})"); return (2,None)
        if self.v: print(f"    LoadLibrary('{name}') -> loading {base}")
        info=self.load(path, run_init=True)
        return (info['hmodule'],None)
    def _FindResource(self, hm, lpName, lpType):
        info=self.hmod.get(hm)
        if not info: return (0,None)
        def canon(x):
            x=str(x).upper()
            return x[1:] if x.startswith('#') else x
        nm=canon(self.res_arg(lpName)); tp=canon(self.res_arg(lpType))
        for r in info['ne'].resources:
            if canon(r['type'])==tp and canon(r['id'])==nm:
                hr=self._next_hrsrc; self._next_hrsrc+=1; self.resources[hr]=(info,r)
                return (hr,None)
        if self.v: print(f"    FindResource(h={hm:#x} type={tp!r} id={nm!r} lpName={lpName:#010x} lpType={lpType:#010x}) NOT FOUND")
        return (0,None)
    def _LoadResource(self, hm, hr):
        ent=self.resources.pop(hr, None)     # transient FindResource handle; consume it
        if not ent: return (0,None)
        info,r=ent; data=info['ne'].resource_data(r)
        sel,base=self.alloc(len(data) or 1); self.uc.mem_write(base, bytes(data))
        self.global_sizes[sel]=len(data); self.resources[sel]=data
        return (sel,None)
    def _LockResource(self, hg):
        return ((hg<<16)|0, None)   # far ptr sel:0

    # file I/O (serve from bin/)
    def _OpenFile(self, lpName, lpOF, style):
        name=self.read_cstr_far(lpName).decode('latin1')
        base=name.replace('/','\\').split('\\')[-1]
        path=os.path.join(
            getattr(self, 'bin_dir', os.path.join(os.path.dirname(__file__), 'bin')),
            base,
        )
        if not os.path.exists(path) and os.path.exists(name): path=name
        if not os.path.exists(path):
            if self.v: print(f"    OpenFile('{name}') -> NOT FOUND");
            return (0xFFFF,None)   # HFILE_ERROR
        data=open(path,'rb').read()
        h=self._next_file; self._next_file+=1; self.files[h]=[data,0]
        # minimal OFSTRUCT: cBytes, fFixedDisk, nErrCode(0), reserved, szPathName@+8
        if lpOF:
            s,o=(lpOF>>16)&0xFFFF,lpOF&0xFFFF
            self.uc.mem_write(self.lin(s,o), bytes([0x88,1,0,0,0,0,0,0])+path.encode('latin1')[:120]+b'\x00')
        if self.v: print(f"    OpenFile('{base}') -> h={h} ({len(data)} bytes)")
        return (h,None)
    def _lread(self, h, lpBuf, n):
        f=self.files.get(h)
        if not f: return (0,None)
        chunk=f[0][f[1]:f[1]+n]; f[1]+=len(chunk)
        s,o=(lpBuf>>16)&0xFFFF,lpBuf&0xFFFF
        if chunk: self.uc.mem_write(self.lin(s,o), chunk)
        return (len(chunk),None)
    def _llseek(self, h, off, origin):
        f=self.files.get(h)
        if not f: return (0xFFFFFFFF,None)
        if off>=0x80000000: off-=0x100000000
        f[1]= off if origin==0 else (f[1]+off if origin==1 else len(f[0])+off)
        return (f[1],None)
    def _lclose(self, h): self.files.pop(h,None); return (0,None)

    def _wsprintf(self, lpOut, lpFmt):
        fmt=self.read_cstr_far(lpFmt).decode('latin1')
        ao=12   # varargs after retIP(2)+retCS(2)+lpOut(4)+lpFmt(4)
        def word():
            nonlocal ao; v=self.rw(self.stack_sel,(self.sp+ao)&0xFFFF); ao+=2; return v
        def dword():
            nonlocal ao; v=self.rd(self.stack_sel,(self.sp+ao)&0xFFFF); ao+=4; return v
        out=''; i=0
        while i<len(fmt):
            c=fmt[i]
            if c!='%': out+=c; i+=1; continue
            j=i+1; spec=''
            while j<len(fmt) and fmt[j] in "-+ 0123456789.lh#*": spec+=fmt[j]; j+=1
            conv=fmt[j] if j<len(fmt) else '%'; lng='l' in spec
            if conv in 'diu':
                v=dword() if lng else word()
                if conv=='d' and not lng and v>=0x8000: v-=0x10000
                out+=str(v)
            elif conv in 'xX':
                out+=format(dword() if lng else word(), conv)
            elif conv=='c': out+=chr(word()&0xFF)
            elif conv=='s': out+=self.read_cstr_far(dword()).decode('latin1','replace')
            elif conv=='%': out+='%'
            i=j+1
        s,o=(lpOut>>16)&0xFFFF,lpOut&0xFFFF
        self.uc.mem_write(self.lin(s,o), out.encode('latin1','replace')+b'\x00')
        return (len(out),None)

    # window / message
    def _caller(self):
        return f"{self.rw(self.stack_sel,(self.sp+2)&0xFFFF):04x}:{self.rw(self.stack_sel,self.sp):04x}"
    def _RegisterClass(self, fp):
        # WNDCLASS: style(2) lpfnWndProc(4) cbClsExtra(2) cbWndExtra(2) hInstance(2) hIcon(2) hCursor(2) hbrBackground(2) lpszMenuName(4) lpszClassName(4)
        s,o=(fp>>16)&0xFFFF,fp&0xFFFF
        self.wndproc=self.rd(s,(o+2)&0xFFFF)   # far ptr
        if self.v: print(f"    RegisterClass wndproc={self.wndproc:#010x} (caller {self._caller()})")
        return (0xC001,None)
    def _CreateWindow(self, cls, wname, style, x,y,w,hh, parent, menu, inst, param):
        hwnd=self._next_hwnd; self._next_hwnd+=4
        self.ngn_hwnd=hwnd
        if self.v: print(f"    CreateWindow -> hwnd {hwnd:#06x} (caller {self._caller()})")
        if self.wndproc and getattr(self,'dispatch_wm_create',False):
            self._send_to_wndproc(hwnd, 0x0001, 0, 0)   # WM_CREATE
        return (hwnd,None)
    def _PostMessage(self, hw, msg, wp, lp):
        if self.wndproc and hw==self.ngn_hwnd:
            if self.v: print(f"    PostMessage(hwnd,{msg:#x},{wp:#x},{lp:#010x}) -> wndproc")
            self._send_to_wndproc(hw, msg, wp, lp)
        return (1,None)
    def _send_to_wndproc(self, hwnd, msg, wp, lp):
        sel=(self.wndproc>>16)&0xFFFF; off=self.wndproc&0xFFFF
        ngn=self.modules.get('FB_NGN')
        dg=ngn['dgroup'] if ngn else self.cur_dgroup
        # WndProc(hwnd, msg, wParam, lParam) FAR PASCAL
        save_sp=self.sp
        self.call_far(sel, off, args=((hwnd,2),(msg,2),(wp,2),(lp,4)), dgroup=dg, ret32=True)
        self.sp=save_sp

    def _waveOutOpen(self, lphwo, devid, lpfmt, cb, inst, fdw):
        s,o=(lpfmt>>16)&0xFFFF,lpfmt&0xFFFF
        tag=self.rw(s,o); ch=self.rw(s,(o+2)&0xFFFF); rate=self.rd(s,(o+4)&0xFFFF)
        bits=self.rw(s,(o+14)&0xFFFF)
        self.wave_fmt=(rate,bits,ch)
        if self.v: print(f"    waveOutOpen: {rate} Hz, {bits}-bit, {ch}ch (tag {tag})")
        if fdw & 0x00000001: return (0,None)   # WAVE_FORMAT_QUERY
        ls,lo=(lphwo>>16)&0xFFFF,lphwo&0xFFFF
        if lphwo: self.ww(ls,lo,0x0BED)        # fake hWaveOut
        return (0,None)
    def _waveOutWrite(self, hw, lphdr, cb):
        s,o=(lphdr>>16)&0xFFFF,lphdr&0xFFFF
        lpdata=self.rd(s,o); length=self.rd(s,(o+4)&0xFFFF)
        ds,do=(lpdata>>16)&0xFFFF,lpdata&0xFFFF
        chunk=bytes(self.uc.mem_read(self.lin(ds,do), length))
        self.pcm+=chunk
        if self.on_block:
            try: self.on_block(chunk)
            except Exception: pass
        if self.v: print(f"    waveOutWrite: {length} bytes (total {len(self.pcm)})")
        # mark WHDR_DONE (dwFlags at hdr+16), clear WHDR_INQUEUE
        self.wd(s,(o+16)&0xFFFF, (self.rd(s,(o+16)&0xFFFF)|0x01)&~0x10)
        return (0,None)


if __name__=='__main__':
    import sys
    BIN=os.path.join(os.path.dirname(os.path.abspath(__file__)),'bin')
    emu=Win16Emu(verbose=True, trace=('-t' in sys.argv))
    print("Loading FB_SPCH.DLL ..."); spch=emu.load(os.path.join(BIN,'FB_SPCH.DLL'))
    print("Loading FB_TIMER.DLL ..."); tmr=emu.load(os.path.join(BIN,'FB_TIMER.DLL'))
    print("Loading FB_NGN.EXE ..."); ngn=emu.load(os.path.join(BIN,'FB_NGN.EXE'), run_init=False)
    print(f"\nmodules: {list(emu.modules)}   imports: {len(emu.imports)}")

    print("\n== call FB_NGN WinMain(0x260e) directly (bypass CRT startup) ==")
    emu.stack_sel = ngn['dgroup']; emu.sp = 0xFFF0
    emu.dispatch_wm_create = True
    csel = ngn['segsel'][1]
    cmd_sel, cmd_base = emu.alloc(16); emu.uc.mem_write(cmd_base, b'\x00')
    hInst = ngn['dgroup']
    ax,ok = emu.call_far(csel, 0x260e,
                         args=((hInst,2),(0,2),((cmd_sel<<16)|0,4),(1,2)),
                         dgroup=ngn['dgroup'], ret32=False)
    print(f"WinMain -> AX={ax:#06x} ok={ok}")
    print(f"wave_fmt={emu.wave_fmt}  captured PCM={len(emu.pcm)} bytes  ngn_hwnd={emu.ngn_hwnd}")
    # verify the WM_CREATE init ran: SETSPEECHENGINEHANDLE should set FB_SPCH [0xfcc]
    fcc = emu.rw(spch['dgroup'], 0xfcc)
    print(f"FB_SPCH [0xfcc] (engine hwnd) = {fcc:#06x}  {'<-- init OK, inter-module link works!' if fcc else '(still 0)'}")
