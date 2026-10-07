#!/usr/bin/env python3
"""Independent checker for Mach-O code signatures.

Reads a Mach-O file (thin, or a universal file with a 32-bit or 64-bit table)
using only the Python standard library, walks the header table and the slice
table, and for every slice that has an LC_CODE_SIGNATURE command recomputes
every page hash and the requirements special slot of the CodeDirectory and
checks the structure facts that a signed program must satisfy. It shares no
code with libthe-seed.

Usage: check_pages.py [--pages] FILE...

Prints one fact per line. With --pages every page hash is printed too (this is
what macho-reference.txt records). Exit status 0 when every file is consistent,
1 when a problem was found, 2 for a usage error.
"""
import hashlib
import struct
import sys

LC_SEGMENT_64 = 0x19
LC_CODE_SIGNATURE = 0x1D
CSMAGIC_EMBEDDED_SIGNATURE = 0xFADE0CC0
CSMAGIC_CODEDIRECTORY = 0xFADE0C02
CSSLOT_CODEDIRECTORY = 0
CSSLOT_REQUIREMENTS = 2
CPU_ARCH_ABI64 = 0x01000000
HASH_SHA256 = 2


class Slice:
    def __init__(self, data, base, size):
        self.data = data
        self.base = base
        self.size = size
        self.bytes = data[base:base + size]
        self.problems = []
        self.facts = []

    def problem(self, text):
        self.problems.append(text)


def read_containers(data):
    """Returns (form, [(cputype, cpusubtype, offset, size, align)])."""
    if len(data) < 4:
        raise ValueError("file shorter than four bytes")
    magic = data[:4]
    if magic == b"\xca\xfe\xba\xbe" or magic == b"\xca\xfe\xba\xbf":
        wide = magic[3] == 0xBF
        if len(data) < 8:
            raise ValueError("truncated universal header")
        (count,) = struct.unpack(">I", data[4:8])
        entry = 32 if wide else 20
        if count > 64 or 8 + count * entry > len(data):
            raise ValueError("universal table does not fit (or is not a universal file)")
        out = []
        for i in range(count):
            at = 8 + i * entry
            if wide:
                cpu, sub, off, size, align, _ = struct.unpack(">iiQQII", data[at:at + 32])
            else:
                cpu, sub, off, size, align = struct.unpack(">iiIII", data[at:at + 20])
            out.append((cpu, sub, off, size, align))
        return ("fat64" if wide else "fat32"), out
    if magic == b"\xcf\xfa\xed\xfe":
        cpu, sub = struct.unpack("<ii", data[4:12])
        return "thin", [(cpu, sub, 0, len(data), 0)]
    raise ValueError("not a little-endian 64-bit Mach-O or universal file")


def walk(sl, out):
    b = sl.bytes
    if len(b) < 32:
        sl.problem("slice shorter than a 64-bit header")
        return None
    (_, cpu, sub, ftype, ncmds, sizeofcmds, flags, _r) = struct.unpack("<8I", b[:32])
    header_end = 32 + sizeofcmds
    out("  header cputype=0x%x cpusubtype=0x%x filetype=%d ncmds=%d sizeofcmds=%d flags=0x%x header_end=%d"
        % (cpu & 0xFFFFFFFF, sub & 0xFFFFFFFF, ftype, ncmds, sizeofcmds, flags, header_end))
    if header_end > len(b):
        sl.problem("load commands run past the end of the slice")
        return None
    at = 32
    cmds = []
    total = 0
    segments = []
    first_content = len(b)
    for i in range(ncmds):
        if at + 8 > header_end:
            sl.problem("command %d header is outside sizeofcmds" % i)
            break
        cmd, size = struct.unpack("<II", b[at:at + 8])
        if size == 0:
            sl.problem("command %d has size zero" % i)
            break
        if size < 8 or size % 4 or at + size > header_end:
            sl.problem("command %d has a bad size %d" % (i, size))
            break
        cmds.append((at, cmd, size))
        total += size
        if cmd == LC_SEGMENT_64 and size >= 72:
            name = b[at + 8:at + 24].split(b"\0")[0].decode("ascii", "replace")
            vmaddr, vmsize, fileoff, filesize, _mp, _ip, nsects, _fl = struct.unpack("<QQQQiiII", b[at + 24:at + 72])
            segments.append((at, name, vmaddr, vmsize, fileoff, filesize))
            if name != "__TEXT" and filesize > 0 and fileoff > 0:
                first_content = min(first_content, fileoff)
            for s in range(nsects):
                so = at + 72 + s * 80
                if so + 80 > at + size:
                    sl.problem("segment %s section table is outside its command" % name)
                    break
                sect_type = struct.unpack("<I", b[so + 64:so + 68])[0] & 0xFF
                (soff,) = struct.unpack("<I", b[so + 48:so + 52])
                if soff != 0 and sect_type not in (0x01, 0x0C, 0x12):  # zerofill kinds
                    first_content = min(first_content, soff)
        at += size
    if len(cmds) == ncmds and total != sizeofcmds:
        sl.problem("sum of command sizes %d differs from sizeofcmds %d" % (total, sizeofcmds))
    out("  free_header_space=%d first_content=%d" % (max(first_content - header_end, 0), first_content))
    return {"cmds": cmds, "segments": segments, "header_end": header_end, "ncmds": ncmds}


def check_signature(sl, info, out, pages):
    b = sl.bytes
    sigs = [c for c in info["cmds"] if c[1] == LC_CODE_SIGNATURE]
    linkedit = [s for s in info["segments"] if s[1] == "__LINKEDIT"]
    if linkedit:
        _, _, vmaddr, vmsize, fo, fs = linkedit[0]
        out("  linkedit fileoff=%d filesize=%d vmsize=%d" % (fo, fs, vmsize))
    if not sigs:
        out("  signature none")
        return
    if len(sigs) != 1:
        sl.problem("%d LC_CODE_SIGNATURE commands" % len(sigs))
    at, _, size = sigs[0]
    if size != 16:
        sl.problem("LC_CODE_SIGNATURE command size is %d" % size)
        return
    dataoff, datasize = struct.unpack("<II", b[at + 8:at + 16])
    out("  codesig dataoff=%d datasize=%d aligned16=%s" % (dataoff, datasize, "yes" if dataoff % 16 == 0 else "no"))
    if dataoff + datasize > len(b):
        sl.problem("signature data runs past the end of the slice")
        return
    if dataoff + datasize != len(b):
        sl.problem("signature data ends %d bytes before the end of the slice" % (len(b) - dataoff - datasize))
    if linkedit and linkedit[0][4] + linkedit[0][5] != len(b):
        sl.problem("__LINKEDIT does not end at the end of the slice")
    if linkedit and linkedit[0][4] + linkedit[0][5] != dataoff + datasize:
        sl.problem("__LINKEDIT does not end where the signature ends")
    blob = b[dataoff:dataoff + datasize]
    if len(blob) < 12:
        sl.problem("signature shorter than a SuperBlob header")
        return
    magic, length, count = struct.unpack(">III", blob[:12])
    if magic != CSMAGIC_EMBEDDED_SIGNATURE:
        sl.problem("SuperBlob magic is 0x%08x" % magic)
        return
    out("  superblob length=%d count=%d" % (length, count))
    if length > datasize:
        sl.problem("SuperBlob length %d exceeds the signature area %d" % (length, datasize))
        return
    if any(blob[length:]):
        sl.problem("bytes after the SuperBlob length are not zero")
    slots = {}
    for i in range(count):
        t, off = struct.unpack(">II", blob[12 + 8 * i:20 + 8 * i])
        slots[t] = off
    if CSSLOT_CODEDIRECTORY not in slots:
        sl.problem("no CodeDirectory slot")
        return
    cd_off = slots[CSSLOT_CODEDIRECTORY]
    cd = blob[cd_off:]
    (cmagic, clen, version, flags, hash_off, ident_off, nspecial, ncode, limit,
     hsize, htype, _plat, psize_log2) = struct.unpack(">IIIIIIIIIBBBB", cd[:40])
    if cmagic != CSMAGIC_CODEDIRECTORY:
        sl.problem("CodeDirectory magic is 0x%08x" % cmagic)
        return
    ident = cd[ident_off:cd.index(b"\0", ident_off)].decode("utf-8", "replace")
    page = 1 << psize_log2 if psize_log2 else limit
    out("  codedirectory version=0x%x flags=0x%x length=%d nSpecialSlots=%d nCodeSlots=%d codeLimit=%d pageSize=%d hashSize=%d hashType=%d identifier=%s"
        % (version, flags, clen, nspecial, ncode, limit, page, hsize, htype, ident))
    if htype != HASH_SHA256 or hsize != 32:
        sl.problem("hash type %d size %d is not SHA-256" % (htype, hsize))
        return
    if limit != dataoff:
        sl.problem("codeLimit %d differs from the signature offset %d" % (limit, dataoff))
    expected = -(-limit // page) if page else 0
    if ncode != expected:
        sl.problem("nCodeSlots %d, expected %d" % (ncode, expected))
    if hash_off + (ncode) * 32 > len(cd) or hash_off < nspecial * 32:
        sl.problem("hash slots do not fit in the CodeDirectory")
        return
    bad = []
    for i in range(ncode):
        want = hashlib.sha256(b[i * page:min((i + 1) * page, limit)]).digest()
        got = cd[hash_off + 32 * i:hash_off + 32 * i + 32]
        if want != got:
            bad.append(i)
        if pages:
            out("  page %d %s" % (i, want.hex()))
    for i in bad:
        sl.problem("page %d hash does not match" % i)
    for n in range(1, nspecial + 1):
        got = cd[hash_off - 32 * n:hash_off - 32 * n + 32]
        if n == 2 and CSSLOT_REQUIREMENTS in slots:
            ro = slots[CSSLOT_REQUIREMENTS]
            (rlen,) = struct.unpack(">I", blob[ro + 4:ro + 8])
            want = hashlib.sha256(blob[ro:ro + rlen]).digest()
            out("  special -2 %s" % ("ok" if want == got else "MISMATCH"))
            if want != got:
                sl.problem("requirements special slot does not match the requirements blob")
        elif any(got):
            out("  special -%d %s (not checked)" % (n, got.hex()))
    out("  pages checked=%d mismatched=%d" % (ncode, len(bad)))


def check_file(path, pages):
    with open(path, "rb") as f:
        data = f.read()
    lines = []
    out = lines.append
    problems = []
    try:
        form, entries = read_containers(data)
    except ValueError as e:
        print("%s: problem: %s" % (path, e))
        return False
    out("file %s form=%s slices=%d length=%d" % (path.split("/")[-1], form, len(entries), len(data)))
    prev_end = 8 + len(entries) * (32 if form == "fat64" else 20) if form != "thin" else 0
    for i, (cpu, sub, off, size, align) in enumerate(entries):
        out("slice %d cputype=0x%x cpusubtype=0x%x offset=%d size=%d align=%d" % (i, cpu & 0xFFFFFFFF, sub & 0xFFFFFFFF, off, size, align))
        sl = Slice(data, off, size)
        if off + size > len(data):
            sl.problem("slice runs past the end of the file")
        elif off < prev_end:
            sl.problem("slice overlaps the table or the previous slice")
        else:
            info = walk(sl, out)
            if info is not None:
                check_signature(sl, info, out, pages)
        prev_end = max(prev_end, off + size)
        problems.extend("slice %d: %s" % (i, p) for p in sl.problems)
    for line in lines:
        print(line)
    for p in problems:
        print("problem: %s" % p)
    print("result %s" % ("ok" if not problems else "FAIL"))
    return not problems


def main(argv):
    pages = False
    paths = []
    for a in argv:
        if a == "--pages":
            pages = True
        elif a.startswith("-"):
            print(__doc__)
            return 2
        else:
            paths.append(a)
    if not paths:
        print(__doc__)
        return 2
    ok = True
    for p in paths:
        ok = check_file(p, pages) and ok
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
