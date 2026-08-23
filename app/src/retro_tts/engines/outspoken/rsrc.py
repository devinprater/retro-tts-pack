# -*- coding: utf-8 -*-
"""Read a Macintosh resource fork.

Needed in three places, which is why it is a module rather than a snippet:
extracting `DRVR`/`TALK`/`RULZ` from outSPOKEN, unpacking the MacinTalk
components, and -- eventually -- letting the add-on accept whatever the user
drops into `rom/` instead of demanding pre-extracted files.

Handles a bare resource fork, a MacBinary wrapper (`.bin`), and an AppleDouble
resource file, because users have all three and can rarely tell them apart.

    py -3 tools/rsrc.py "C:/path/to/MacinTalk 2.rsrc"
    py -3 tools/rsrc.py <file> thng          # one type
    py -3 tools/rsrc.py <file> snd  128 out.bin
"""
import os
import struct
import sys


class Resource(object):
    """One resource, plus where its reference entry sits in the map.

    `map_entry` is the offset from the start of the resource MAP to this
    resource's twelve-byte reference-list entry -- which is exactly what the
    Resource Manager's `RsrcMapEntry` returns, and **MacinTalk Pro needs it**.
    Pro does not load its 800 KB unit database into a Handle; it asks for the
    map entry, extracts the 24-bit data offset from it, adds the data-area base
    from the map's own header copy, and reads that many bytes out of the FILE.
    That is how it ran on a Mac with 8 MB of RAM.
    """
    __slots__ = ("type", "id", "name", "attrs", "data", "map_entry")

    def __init__(self, rtype, rid, name, attrs, data, map_entry=0):
        self.type, self.id, self.name = rtype, rid, name
        self.attrs, self.data = attrs, data
        self.map_entry = map_entry

    def __repr__(self):
        return "<%s %d %r %d bytes>" % (self.type, self.id, self.name,
                                        len(self.data))


def _unwrap(raw):
    """Return just the resource fork from whatever container this is."""
    # MacBinary: byte 0 is zero, byte 1 is a 1..63 name length, 74 and 82 zero.
    if len(raw) > 128 and raw[0] == 0 and 1 <= raw[1] <= 63 \
            and raw[74] == 0 and raw[82] == 0:
        dlen, rlen = struct.unpack(">II", raw[83:91])
        start = 128 + ((dlen + 127) // 128) * 128
        if start + rlen <= len(raw):
            return raw[start:start + rlen]
    # AppleDouble / AppleSingle: magic 0x00051607 or 0x00051600.
    if len(raw) > 26 and raw[:4] in (b"\x00\x05\x16\x07", b"\x00\x05\x16\x00"):
        n = struct.unpack(">H", raw[24:26])[0]
        for i in range(n):
            eid, off, ln = struct.unpack(">III", raw[26 + i * 12:38 + i * 12])
            if eid == 2:                       # resource fork
                return raw[off:off + ln]
    return raw


def parse(raw):
    """-> list of Resource, in map order."""
    fork = _unwrap(raw)
    if len(fork) < 16:
        raise ValueError("too short to be a resource fork")
    dOff, mOff, dLen, mLen = struct.unpack(">IIII", fork[:16])
    if mOff + mLen > len(fork) or dOff + dLen > len(fork):
        raise ValueError("resource header points outside the file "
                         "(data@%d+%d map@%d+%d, file %d)"
                         % (dOff, dLen, mOff, mLen, len(fork)))
    m = fork[mOff:mOff + mLen]
    tlOff, nlOff = struct.unpack(">HH", m[24:28])
    tl = m[tlOff:]
    out = []
    for i in range(struct.unpack(">H", tl[:2])[0] + 1):
        rtype, cnt, rOff = struct.unpack(">4sHH", tl[2 + i * 8:10 + i * 8])
        for j in range(cnt + 1):
            e = tl[rOff + j * 12:rOff + j * 12 + 12]
            rid, nOff = struct.unpack(">hH", e[:4])
            attrs_do = struct.unpack(">I", e[4:8])[0]
            attrs, do = attrs_do >> 24, attrs_do & 0x00FFFFFF
            size = struct.unpack(">I", fork[dOff + do:dOff + do + 4])[0]
            data = fork[dOff + do + 4:dOff + do + 4 + size]
            name = ""
            if nOff != 0xFFFF:
                p = mOff + nlOff + nOff
                name = fork[p + 1:p + 1 + fork[p]].decode("mac-roman", "replace")
            # Offset from the start of the map to this reference entry: the
            # type list sits at tlOff within the map, the reference list at
            # rOff within the type list, and entries are twelve bytes.
            out.append(Resource(rtype.decode("mac-roman", "replace"),
                                rid, name, attrs, data,
                                tlOff + rOff + j * 12))
    return out


def fork_bytes(raw):
    """Just the resource fork, out of whatever container it arrived in.

    MacinTalk Pro reads its own resource fork as a FILE -- it walks the map and
    seeks to byte offsets -- so the emulator has to be handed the fork itself,
    not only the resources parsed out of it.
    """
    return _unwrap(raw)


def load(path):
    return parse(open(path, "rb").read())


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 1
    rs = load(args[0])
    if len(args) >= 3:                          # extract one
        want_t, want_id = args[1], int(args[2])
        for r in rs:
            if r.type == want_t and r.id == want_id:
                out = args[3] if len(args) > 3 else "%s_%d.bin" % (want_t, want_id)
                open(out, "wb").write(r.data)
                print("wrote %s (%d bytes)" % (out, len(r.data)))
                return 0
        print("no %s %d" % (want_t, want_id))
        return 1
    filt = args[1] if len(args) > 1 else None
    total, byt = 0, 0
    for r in rs:
        if filt and r.type != filt:
            continue
        total += 1
        byt += len(r.data)
        print("  %-4s %6d  %8d bytes  %r" % (r.type, r.id, len(r.data), r.name))
    print("\n  %d resources, %d bytes" % (total, byt))
    return 0


if __name__ == "__main__":
    sys.exit(main())
