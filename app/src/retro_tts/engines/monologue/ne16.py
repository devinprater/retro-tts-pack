"""
Minimal NE (16-bit New Executable) parser for the Win16-on-Unicorn loader.
Extracts: module name, segment table (+ data + relocations), entry table
(ordinal -> seg:offset), and imported module/ordinal references.
"""
import struct

# segment flags
SEG_DATA      = 0x0001
SEG_MOVEABLE  = 0x0010
SEG_PRELOAD   = 0x0040
SEG_RELOCINFO = 0x0100

# relocation address (source) types
RA_LOBYTE  = 0
RA_SEGMENT = 2   # 16-bit selector
RA_FAR     = 3   # 32-bit seg:off
RA_OFFSET  = 5   # 16-bit offset

# relocation kinds
RK_INTERNAL = 0
RK_IMPORDINAL = 1
RK_IMPNAME  = 2
RK_OSFIXUP  = 3
RK_ADDITIVE = 4

class Segment:
    def __init__(self, idx, sector, length, flags, minalloc, data, relocs):
        self.idx = idx            # 1-based
        self.sector = sector
        self.length = length
        self.flags = flags
        self.minalloc = minalloc
        self.data = data          # raw bytes (length bytes)
        self.relocs = relocs      # list of dicts
    @property
    def is_data(self): return bool(self.flags & SEG_DATA)
    def __repr__(self):
        return (f"<Seg {self.idx} {'DATA' if self.is_data else 'CODE'} "
                f"len={len(self.data)} minalloc={self.minalloc} "
                f"flags={self.flags:#06x} relocs={len(self.relocs)}>")

class NEFile:
    def __init__(self, path):
        self.data = open(path, 'rb').read()
        d = self.data
        assert d[:2] == b'MZ', "not MZ"
        self.ne = struct.unpack_from('<I', d, 0x3c)[0]
        assert d[self.ne:self.ne+2] == b'NE', "not NE"
        h = self.ne
        self.flags       = struct.unpack_from('<H', d, h+0x0c)[0]
        self.auto_ds     = struct.unpack_from('<H', d, h+0x0e)[0]  # automatic data seg #
        self.heap        = struct.unpack_from('<H', d, h+0x10)[0]
        self.stack       = struct.unpack_from('<H', d, h+0x12)[0]
        self.csip        = struct.unpack_from('<I', d, h+0x14)[0]  # entry CS:IP
        self.sssp        = struct.unpack_from('<I', d, h+0x18)[0]
        self.cseg        = struct.unpack_from('<H', d, h+0x1c)[0]
        self.cmod        = struct.unpack_from('<H', d, h+0x1e)[0]
        self.cbnrestab   = struct.unpack_from('<H', d, h+0x20)[0]
        self.segtab_off  = struct.unpack_from('<H', d, h+0x22)[0] + h
        self.rsrc_off    = struct.unpack_from('<H', d, h+0x24)[0] + h
        self.resnam_off  = struct.unpack_from('<H', d, h+0x26)[0] + h
        self.modref_off  = struct.unpack_from('<H', d, h+0x28)[0] + h
        self.impnam_off  = struct.unpack_from('<H', d, h+0x2a)[0] + h
        self.nonres_off  = struct.unpack_from('<I', d, h+0x2c)[0]  # absolute
        self.enttab_off  = struct.unpack_from('<H', d, h+0x04)[0] + h
        self.enttab_len  = struct.unpack_from('<H', d, h+0x06)[0]
        self.align       = struct.unpack_from('<H', d, h+0x32)[0] or 9  # sector shift
        self._parse_names()
        self._parse_modules()
        self._parse_segments()
        self._parse_entries()
        self._parse_resources()

    # ---- names ----
    def _pstr(self, off):
        ln = self.data[off]
        return self.data[off+1:off+1+ln], off+1+ln

    def _parse_names(self):
        s, off = self._pstr(self.resnam_off)
        self.module = s.decode('latin1')
        off += 2                   # skip module's ordinal word (0)
        # residents map name->ord
        self.exports = {}          # NAME -> ordinal
        while True:
            ln = self.data[off]
            if ln == 0: break
            name = self.data[off+1:off+1+ln].decode('latin1')
            ordv = struct.unpack_from('<H', self.data, off+1+ln)[0]
            if name != self.module:
                self.exports[name] = ordv
            off += 1+ln+2
        # non-resident
        off = self.nonres_off; first = True
        while off < self.nonres_off + self.cbnrestab:
            ln = self.data[off]
            if ln == 0: break
            name = self.data[off+1:off+1+ln].decode('latin1')
            ordv = struct.unpack_from('<H', self.data, off+1+ln)[0]
            if not first:
                self.exports[name] = ordv
            else:
                self.description = name
            off += 1+ln+2; first = False

    def _parse_modules(self):
        self.modules = []
        for k in range(self.cmod):
            nameoff = struct.unpack_from('<H', self.data, self.modref_off + k*2)[0]
            s, _ = self._pstr(self.impnam_off + nameoff)
            self.modules.append(s.decode('latin1'))

    # ---- segments ----
    def _parse_segments(self):
        self.segments = []
        d = self.data
        for k in range(self.cseg):
            base = self.segtab_off + k*8
            sector, length, flags, minalloc = struct.unpack_from('<HHHH', d, base)
            file_off = sector << self.align
            seg_len = length if length else (0x10000 if sector else 0)
            data = d[file_off:file_off+seg_len] if sector else b''
            relocs = []
            if sector and (flags & SEG_RELOCINFO):
                rp = file_off + seg_len
                nrel = struct.unpack_from('<H', d, rp)[0]; rp += 2
                for _ in range(nrel):
                    atype, rtype, srcoff, t1, t2 = struct.unpack_from('<BBHHH', d, rp); rp += 8
                    relocs.append({'atype': atype, 'rtype': rtype, 'srcoff': srcoff,
                                   't1': t1, 't2': t2})
            self.segments.append(Segment(k+1, sector, seg_len, flags, minalloc, data, relocs))

    # ---- entry table: ordinal -> (segment, offset) ----
    def _parse_entries(self):
        self.entries = {}   # ordinal -> (seg, off)
        d = self.data
        off = self.enttab_off
        ordn = 1
        end = self.enttab_off + self.enttab_len
        while off < end:
            cnt = d[off]; ind = d[off+1]; off += 2
            if cnt == 0: break
            if ind == 0x00:
                ordn += cnt  # unused, consume ordinals
            elif ind == 0xFF:
                for _ in range(cnt):
                    flags = d[off]; seg = d[off+3]
                    eoff = struct.unpack_from('<H', d, off+4)[0]
                    self.entries[ordn] = (seg, eoff); off += 6; ordn += 1
            else:
                for _ in range(cnt):
                    flags = d[off]
                    eoff = struct.unpack_from('<H', d, off+1)[0]
                    self.entries[ordn] = (ind, eoff); off += 3; ordn += 1

    # ---- resource table ----
    def _parse_resources(self):
        self.resources = []      # list of {type, id, foff, length}
        d = self.data
        if self.rsrc_off <= self.ne or self.rsrc_off >= self.resnam_off:
            return               # no/empty resource table (rsrc_off == resnam_off means none)
        off = self.rsrc_off
        try:
            shift = struct.unpack_from('<H', d, off)[0]; off += 2
            def rname(v):
                if v & 0x8000: return v & 0x7FFF          # integer id
                s,_ = self._pstr(self.rsrc_off + v)       # string, offset from table start
                return s.decode('latin1')
            while True:
                tid = struct.unpack_from('<H', d, off)[0]
                if tid == 0: break
                count = struct.unpack_from('<H', d, off+2)[0]
                off += 8
                rtype = rname(tid)
                for _ in range(count):
                    rn_off, rn_len, rn_flags, rn_id = struct.unpack_from('<HHHH', d, off)
                    off += 12
                    self.resources.append({'type': rtype, 'id': rname(rn_id),
                                           'foff': rn_off << shift, 'length': rn_len << shift,
                                           'flags': rn_flags})
        except Exception:
            pass

    def resource_data(self, r):
        return self.data[r['foff']:r['foff']+r['length']]

    def ordinal_of(self, name): return self.exports.get(name.upper())
    def loc_of(self, name):
        o = self.ordinal_of(name)
        return self.entries.get(o) if o else None

    def reloc_desc(self, seg, r):
        atype = {0:'LOBYTE',2:'SEG',3:'FAR',5:'OFF'}.get(r['atype'], hex(r['atype']))
        kind = r['rtype'] & 3
        add = ' +ADD' if (r['rtype'] & RK_ADDITIVE) else ''
        if kind == RK_INTERNAL:
            s = r['t1'] & 0xFF
            if s == 0xFF:
                return f"{atype} INTERNAL movable ord {r['t2']}{add}"
            return f"{atype} INTERNAL seg {s}:{r['t2']:#06x}{add}"
        if kind == RK_IMPORDINAL:
            mod = self.modules[r['t1']-1] if 0 < r['t1'] <= len(self.modules) else f"mod{r['t1']}"
            return f"{atype} IMPORT {mod}.{r['t2']}{add}"
        if kind == RK_IMPNAME:
            mod = self.modules[r['t1']-1] if 0 < r['t1'] <= len(self.modules) else f"mod{r['t1']}"
            nm, _ = self._pstr(self.impnam_off + r['t2'])
            return f"{atype} IMPORT {mod}.{nm.decode('latin1')}{add}"
        return f"{atype} OSFIXUP {r['t1']} {r['t2']}{add}"


if __name__ == '__main__':
    import sys
    ne = NEFile(sys.argv[1])
    print(f"module={ne.module}  desc={getattr(ne,'description','')}")
    print(f"segs={ne.cseg} auto_ds={ne.auto_ds} heap={ne.heap} stack={ne.stack} "
          f"CS:IP={ne.csip:#010x} SS:SP={ne.sssp:#010x} align=2^{ne.align}")
    print(f"modules={ne.modules}")
    print("\n== SEGMENTS ==")
    for s in ne.segments:
        print(" ", s)
    print("\n== KEY EXPORTS (ordinal -> seg:off) ==")
    for nm in ['SPEECHVERSION','OPENSPEECH','SAY','CLOSESPEECH','TEXTTOPHONETICS',
               'SPEAKPHONETICS','GETSPEECHENGINEHANDLE','SETSPEECHENGINEHANDLE',
               'CREATEFIFO','GETFIFO','PUTFIFO','PEEKFIFO','TRAVERSEFIFO','GETFIFO']:
        loc = ne.loc_of(nm)
        if loc: print(f"  {nm:<22} ord {ne.ordinal_of(nm):>2} -> seg {loc[0]}:{loc[1]:#06x}")
    # imported ordinal summary
    print("\n== IMPORTED FIXUPS (module.ordinal counts) ==")
    from collections import Counter
    c = Counter()
    for s in ne.segments:
        for r in s.relocs:
            k = r['rtype'] & 3
            if k in (RK_IMPORDINAL, RK_IMPNAME):
                c[ne.reloc_desc(s, r).split(' ',2)[2].split(' +')[0]] += 1
    for k, v in sorted(c.items()):
        print(f"  {k:<22} x{v}")
