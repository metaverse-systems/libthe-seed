#!/usr/bin/env python3
"""Builder and converter for Compound File (structured storage) packages.

SYNTHETIC: the packages this script writes are not produced by any installer
tool. They exist to give the tests shapes that wixl and osslsigncode do not
write, and are never evidence of what real tools produce. Python standard
library only; the output is deterministic (no clock, no randomness).

Usage:
  make_cfb.py --convert IN OUT
      Rewrite IN with 4,096-byte sectors (format version 4). Same storages,
      streams, class identifiers, state bits and times.
  make_cfb.py --nested IN OUT
      Copy the root streams of IN and add two storages (one level deep:
      osslsigncode 2.14 writes storages nested two deep wrongly) with
      non-zero class identifiers, names that differ only in case, names that order
      differently by raw bytes than case-folded, and a name that is a prefix
      of another.
  make_cfb.py --extra-neighbours IN OUT
      Copy IN and add fourteen streams with long names (the unsigned base of
      --two-neighbours: with SummaryInformation fifteen names sort after the
      signature stream's and eighteen before it).
  make_cfb.py --two-neighbours IN SIGNED OUT
      Like --extra-neighbours for IN, then insert the signature stream of
      SIGNED as the top of the root's search tree: its entry has both a left
      and a right neighbour and each of those has neighbours below it.
  make_cfb.py --blob SEED SIZE OUT
      Write SIZE deterministic bytes made from SEED (a DER-looking header
      followed by chained SHA-256 output).
  make_cfb.py --check FILE
      Parse FILE and verify the layout (allocation tables, directory, tree
      colours, ordering); exit status 1 and a message on a fault.
"""
import hashlib
import struct
import sys

SIG = "\x05DigitalSignature"
ENDOFCHAIN = 0xFFFFFFFE
FREESECT = 0xFFFFFFFF
FATSECT = 0xFFFFFFFD
DIFSECT = 0xFFFFFFFC
NOSTREAM = 0xFFFFFFFF
MAGIC = bytes.fromhex("d0cf11e0a1b11ae1")
MINI_CUTOFF = 4096
MINI_SECTOR = 64


class Node:
    """A storage (children is a list) or a stream (data is bytes)."""

    def __init__(self, name, data=None, clsid=bytes(16), state=0, ctime=0, mtime=0):
        self.name = name
        self.data = data
        self.children = None if data is not None else []
        self.clsid = clsid
        self.state = state
        self.ctime = ctime
        self.mtime = mtime

    @property
    def is_stream(self):
        return self.data is not None


def sort_key(name):
    # The format's ordering: shorter name first, then the upper-cased code
    # units (ASCII a-z only, which is all the names here need).
    up = "".join(chr(ord(c) - 32) if "a" <= c <= "z" else c for c in name)
    return (len(name), [ord(c) for c in up])


def raw_key(name):
    return name.encode("utf-16-le")


# ---------------------------------------------------------------- reading

class Reader:
    def __init__(self, data):
        if data[:8] != MAGIC:
            raise ValueError("not a compound file")
        self.data = data
        (self.version,) = struct.unpack_from("<H", data, 0x1A)
        (shift,) = struct.unpack_from("<H", data, 0x1E)
        self.sector = 1 << shift
        (mshift,) = struct.unpack_from("<H", data, 0x20)
        self.mini = 1 << mshift
        (self.n_fat, self.dir_start, _, self.cutoff, self.minifat_start,
         self.n_minifat, self.difat_start, self.n_difat) = struct.unpack_from(
            "<IIIIIIII", data, 0x2C)
        fat_ids = list(struct.unpack_from("<109I", data, 0x4C))
        s = self.difat_start
        for _ in range(self.n_difat):
            ent = struct.unpack("<%dI" % (self.sector // 4), self.sec(s))
            fat_ids.extend(ent[:-1])
            s = ent[-1]
        fat_ids = [x for x in fat_ids if x < FATSECT][: self.n_fat]
        self.fat = []
        for x in fat_ids:
            self.fat.extend(struct.unpack("<%dI" % (self.sector // 4), self.sec(x)))
        self.minifat = []
        for x in self.chain(self.minifat_start):
            self.minifat.extend(struct.unpack("<%dI" % (self.sector // 4), self.sec(x)))
        dirdata = b"".join(self.sec(x) for x in self.chain(self.dir_start))
        self.entries = []
        for i in range(len(dirdata) // 128):
            self.entries.append(self.parse_entry(dirdata[i * 128:(i + 1) * 128]))
        root = self.entries[0]
        self.ministream = self.read_chain(root["start"], root["size"], False)

    def sec(self, n):
        off = (n + 1) * self.sector
        return self.data[off:off + self.sector]

    def chain(self, start):
        out = []
        seen = set()
        n = start
        while n < FATSECT:
            if n in seen or n >= len(self.fat):
                raise ValueError("broken chain")
            seen.add(n)
            out.append(n)
            n = self.fat[n]
        return out

    def read_chain(self, start, size, mini):
        if mini:
            buf = b""
            n = start
            while n < FATSECT and len(buf) < size:
                buf += self.ministream[n * self.mini:(n + 1) * self.mini]
                n = self.minifat[n]
            return buf[:size]
        return b"".join(self.sec(x) for x in self.chain(start))[:size]

    @staticmethod
    def parse_entry(b):
        (nlen,) = struct.unpack_from("<H", b, 64)
        name = b[:max(nlen - 2, 0)].decode("utf-16-le")
        typ, color = b[66], b[67]
        left, right, child = struct.unpack_from("<III", b, 68)
        clsid = bytes(b[80:96])
        (state,) = struct.unpack_from("<I", b, 96)
        ctime, mtime = struct.unpack_from("<QQ", b, 100)
        start, size = struct.unpack_from("<IQ", b, 116)
        return dict(name=name, type=typ, color=color, left=left, right=right,
                    child=child, clsid=clsid, state=state, ctime=ctime,
                    mtime=mtime, start=start, size=size)

    def siblings(self, first):
        out = []
        def walk(i):
            if i == NOSTREAM:
                return
            e = self.entries[i]
            walk(e["left"])
            out.append(i)
            walk(e["right"])
        walk(first)
        return out

    def tree(self, idx=0):
        e = self.entries[idx]
        if e["type"] in (1, 5):
            node = Node(e["name"] if idx else "Root Entry", clsid=e["clsid"],
                        state=e["state"], ctime=e["ctime"], mtime=e["mtime"])
            for c in self.siblings(e["child"]):
                node.children.append(self.tree(c))
            return node
        mini = e["size"] < self.cutoff
        return Node(e["name"], self.read_chain(e["start"], e["size"], mini),
                    clsid=e["clsid"], state=e["state"], ctime=e["ctime"],
                    mtime=e["mtime"])


# ---------------------------------------------------------------- writing

def assign_tree(children, pick_root=None):
    """Return (root_index, links) for the sorted list of children; links maps
    index -> [left, right, color]. Balanced; the last level is red."""
    links = {}

    def build(lo, hi, depth, forced=None):
        if lo >= hi:
            return None
        mid = forced if forced is not None else (lo + hi) // 2
        links[mid] = [None, None, 0, depth]
        links[mid][0] = build(lo, mid, depth + 1)
        links[mid][1] = build(mid + 1, hi, depth + 1)
        return mid

    root = build(0, len(children), 0, pick_root)
    if links:
        deepest = max(v[3] for v in links.values())
        full = all(v[0] is not None and v[1] is not None
                   for v in links.values() if v[3] < deepest - 1)
        # Colour the deepest level red unless the tree is perfect.
        count = len(children)
        perfect = (count + 1) & count == 0
        for v in links.values():
            v[2] = 1 if (not perfect and v[3] == deepest and deepest > 0) else 0
        assert full
    return root, links


def build_cfb(root, version=3):
    sector = 512 if version == 3 else 4096
    per = sector // 4

    # Directory entries, children of one storage contiguous.
    entries = []  # dict per entry

    def add(node, kind):
        entries.append(dict(node=node, type=kind, left=NOSTREAM, right=NOSTREAM,
                            child=NOSTREAM, color=1, start=ENDOFCHAIN if kind != 2 else 0,
                            size=0))
        return len(entries) - 1

    add(root, 5)
    queue = [(0, root)]
    while queue:
        pid, parent = queue.pop(0)
        kids = sorted(parent.children, key=lambda n: sort_key(n.name))
        ids = [add(k, 2 if k.is_stream else 1) for k in kids]
        pick = None
        if getattr(parent, "top", None) is not None:
            pick = [k.name for k in kids].index(parent.top)
        r, links = assign_tree(kids, pick)
        if r is not None:
            entries[pid]["child"] = ids[r]
        for i, (l, rr, col, _) in links.items():
            entries[ids[i]]["left"] = ids[l] if l is not None else NOSTREAM
            entries[ids[i]]["right"] = ids[rr] if rr is not None else NOSTREAM
            entries[ids[i]]["color"] = 0 if col else 1  # 0 red, 1 black
        for k, i in zip(kids, ids):
            if not k.is_stream:
                queue.append((i, k))
    root_entry = entries[0]
    root_entry["color"] = 1

    # Streams: ordinary sectors or the mini stream.
    big = [e for e in entries if e["type"] == 2 and len(e["node"].data) >= MINI_CUTOFF]
    small = [e for e in entries if e["type"] == 2 and 0 < len(e["node"].data) < MINI_CUTOFF]
    # Mini stream and mini allocation table.
    mini = bytearray()
    minifat = []
    for e in small:
        d = e["node"].data
        n = (len(d) + MINI_SECTOR - 1) // MINI_SECTOR
        e["start"] = len(mini) // MINI_SECTOR
        for i in range(n):
            minifat.append(e["start"] + i + 1 if i + 1 < n else ENDOFCHAIN)
        mini += d + bytes(n * MINI_SECTOR - len(d))
    for e in entries:
        if e["type"] == 2 and len(e["node"].data) == 0:
            e["start"] = ENDOFCHAIN

    def nsec(nbytes):
        return (nbytes + sector - 1) // sector

    n_data = sum(nsec(len(e["node"].data)) for e in big)
    n_mini = nsec(len(mini))
    n_minifat = nsec(len(minifat) * 4)
    n_dir = nsec(len(entries) * 128)
    # FAT sectors: iterate until the table covers itself.
    n_fat = 1
    while True:
        total = n_data + n_mini + n_minifat + n_dir + n_fat
        if n_fat * per >= total:
            break
        n_fat += 1
    assert n_fat <= 109, "this builder does not write a DIFAT"

    fat = [FREESECT] * (n_fat * per)
    sectors = []  # list of sector payloads in file order

    def lay(payload, chain_in=None):
        start = len(sectors)
        n = nsec(len(payload))
        for i in range(n):
            sectors.append(payload[i * sector:(i + 1) * sector].ljust(sector, b"\0"))
            fat[start + i] = start + i + 1 if i + 1 < n else ENDOFCHAIN
        return start if n else ENDOFCHAIN

    for e in big:
        e["start"] = lay(e["node"].data)
    root_entry["start"] = lay(bytes(mini))
    root_entry["size"] = len(mini)
    minifat_start = lay(b"".join(struct.pack("<I", x) for x in minifat)
                        + b"\xff" * ((-len(minifat)) % per * 4)) if minifat else ENDOFCHAIN
    # fix padding of the mini allocation table: unused entries are free
    for e in entries:
        if e["type"] == 2:
            e["size"] = len(e["node"].data)

    dir_bytes = b""
    for e in entries:
        node = e["node"]
        name = "Root Entry" if e["type"] == 5 else node.name
        raw = name.encode("utf-16-le")
        dir_bytes += (raw.ljust(64, b"\0")
                      + struct.pack("<HBB", len(raw) + 2, e["type"], e["color"])
                      + struct.pack("<III", e["left"], e["right"], e["child"])
                      + node.clsid + struct.pack("<I", node.state)
                      + struct.pack("<QQ", node.ctime, node.mtime)
                      + struct.pack("<IQ", e["start"], e["size"]))
    free = bytes(66) + bytes(2) + struct.pack("<III", NOSTREAM, NOSTREAM, NOSTREAM) + bytes(128 - 80)
    pad = (-len(entries)) % (sector // 128)
    dir_bytes += free * pad
    dir_start = lay(dir_bytes)
    fat_ids = []
    for _ in range(n_fat):
        fat_ids.append(len(sectors))
        fat[len(sectors)] = FATSECT
        sectors.append(None)
    assert len(sectors) <= n_fat * per
    for i, fid in enumerate(fat_ids):
        sectors[fid] = struct.pack("<%dI" % per, *fat[i * per:(i + 1) * per])

    header = bytearray(512)
    header[0:8] = MAGIC
    struct.pack_into("<HHHHH", header, 0x18, 0x3E, version, 0xFFFE, 9 if version == 3 else 12, 6)
    struct.pack_into("<IIIIIII", header, 0x28,
                     0 if version == 3 else n_dir, n_fat, dir_start, 0, MINI_CUTOFF,
                     minifat_start, n_minifat if minifat else 0)
    struct.pack_into("<II", header, 0x44, ENDOFCHAIN, 0)
    ids = fat_ids + [FREESECT] * (109 - n_fat)
    struct.pack_into("<109I", header, 0x4C, *ids)
    out = bytes(header).ljust(sector, b"\0") + b"".join(sectors)
    return out


# ---------------------------------------------------------------- modes

def load(path):
    with open(path, "rb") as f:
        return Reader(f.read()).tree()


def find(node, name):
    for c in node.children:
        if c.name == name:
            return c
    return None


def clsid(n):
    return bytes([n]) * 4 + bytes(range(n, n + 12))


def nested_from(base):
    root = base
    t = 0x01DB000000000000  # fixed, non-zero times for the added storages
    sub = Node("Sub", clsid=clsid(0x11), state=0x00000003, ctime=t, mtime=t + 1)
    sub.children = [
        Node("Inner", b"inner stream, first of a case-only pair"),
        Node("INNER", b"inner stream, second of a case-only pair"),
        Node("a", b"folded order puts a before B"),
        Node("B", b"raw byte order puts B before a"),
        Node("Data", b"a name that is a prefix of another"),
        Node("DataExtra", bytes(range(200))),
        Node("big", bytes((i * 7 + 1) % 251 for i in range(5000))),
    ]
    second = Node("Second", clsid=clsid(0x33), state=0x00000001, ctime=t + 4, mtime=t + 5)
    second.children = [Node("inner", b"same name as in Sub, other storage"),
                       Node("x", b"")]
    root.children.append(sub)
    root.children.append(second)
    root.children.append(Node("a", b"root stream a"))
    root.children.append(Node("B", b"root stream B"))
    return root


def extra_from(base):
    # Fourteen streams with names longer than the signature stream's, so that
    # with SummaryInformation fifteen names sort after it, and eighteen before.
    for i in range(14):
        base.children.append(Node("LongNamedExtraStream%02d" % (i + 1),
                                  bytes((j + i) % 256 for j in range(40 + 8 * i))))
    return base


def blob(seed, size):
    out = b""
    i = 0
    while len(out) < size:
        out += hashlib.sha256(("%s:%d" % (seed, i)).encode()).digest()
        i += 1
    out = bytearray(out[:size])
    if size >= 4:
        out[0:4] = bytes([0x30, 0x82, ((size - 4) >> 8) & 255, (size - 4) & 255])
    return bytes(out)


def check(path):
    with open(path, "rb") as f:
        data = f.read()
    r = Reader(data)
    problems = []
    # allocation tables: every chain used once
    used = {}
    def claim(chain, who):
        for s in chain:
            if s in used:
                problems.append("sector %d used by %s and %s" % (s, used[s], who))
            used[s] = who
    claim(r.chain(r.dir_start), "directory")
    if r.minifat_start < FATSECT:
        claim(r.chain(r.minifat_start), "mini allocation table")
    root = r.entries[0]
    if root["start"] < FATSECT:
        claim(r.chain(root["start"]), "mini stream")
    for i, e in enumerate(r.entries):
        if e["type"] == 2 and e["size"] >= r.cutoff:
            claim(r.chain(e["start"]), "entry %d" % i)
        if e["type"] in (1, 5):
            sibs = r.siblings(e["child"])
            keys = [sort_key(r.entries[s]["name"]) for s in sibs]
            if keys != sorted(keys):
                problems.append("entry %d: children not in order" % i)
            # red-black: no red child of red, equal black height
            def bh(j):
                if j == NOSTREAM:
                    return 1
                x = r.entries[j]
                if x["color"] == 0:
                    for c in (x["left"], x["right"]):
                        if c != NOSTREAM and r.entries[c]["color"] == 0:
                            problems.append("red entry %d has a red child" % j)
                a, b = bh(x["left"]), bh(x["right"])
                if a != b:
                    problems.append("entry %d: unequal black height" % j)
                return a + (1 if x["color"] == 1 else 0)
            if e["child"] != NOSTREAM:
                if r.entries[e["child"]]["color"] != 1:
                    problems.append("top of the tree under %d is red" % i)
                bh(e["child"])
    nfat = sum(1 for x in r.fat if x == FATSECT)
    if nfat != r.n_fat:
        problems.append("%d FAT markers, header says %d" % (nfat, r.n_fat))
    if (len(data) // r.sector) - 1 < len([x for x in r.fat if x != FREESECT]):
        problems.append("file shorter than the allocation table says")
    note = ""
    for e in r.entries:
        if e["name"] == SIG and e["left"] != NOSTREAM and e["right"] != NOSTREAM:
            below = [r.entries[e["left"]], r.entries[e["right"]]]
            if all(b["left"] != NOSTREAM or b["right"] != NOSTREAM for b in below):
                note = ", signature entry with a left and a right neighbour, each with subtrees"
    tree = r.tree()
    def count(n):
        return 1 + sum(count(c) for c in (n.children or []))
    print("%s: version %d, %d-byte sectors, %d entries, %d bytes%s: ok" % (
        path, r.version, r.sector, count(tree), len(data), note) if not problems else "\n".join(problems))
    return 1 if problems else 0


def main(argv):
    if len(argv) == 4 and argv[1] == "--convert":
        data = build_cfb(load(argv[2]), 4)
    elif len(argv) == 4 and argv[1] == "--nested":
        data = build_cfb(nested_from(load(argv[2])), 3)
    elif len(argv) == 4 and argv[1] == "--extra-neighbours":
        data = build_cfb(extra_from(load(argv[2])), 3)
    elif len(argv) == 5 and argv[1] == "--two-neighbours":
        base = extra_from(load(argv[2]))
        signed = find(load(argv[3]), SIG)
        if signed is None:
            sys.exit("make_cfb.py: %s has no %s" % (argv[3], SIG.encode()))
        base.children.append(Node(SIG, signed.data))
        base.top = SIG
        data = build_cfb(base, 3)
    elif len(argv) == 5 and argv[1] == "--blob":
        data = blob(argv[2], int(argv[3]))
    elif len(argv) == 3 and argv[1] == "--check":
        return check(argv[2])
    else:
        sys.stderr.write(__doc__)
        return 2
    with open(argv[-1], "wb") as f:
        f.write(data)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
