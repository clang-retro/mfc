#!/usr/bin/env python3
"""mkhlp.py - writes the small help files that hlptest reads.

Usage: python3 mkhlp.py OUTDIR

Writes OUTDIR/feature.hlp (a WinHelp 3.1 file whose text is compressed
with |Phrases) and OUTDIR/feature95.hlp (a WinHelp 4.0 file whose text
is compressed with |PhrIndex and |PhrImage, Hall compression). Both have
the same three topics: LZ77-compressed topic blocks, a region that does
not scroll, a table, a picture (|bm0, run-length packed, with a
hotspot), jumps, a popup, a macro hotspot, a browse sequence, keywords
and [MAP] numbers. The layout follows helpdeco's helpfile.txt.

Part of clang-retro MFC, an independent implementation of the MFC API.
SPDX-License-Identifier: AGPL-3.0-only
"""
import os
import struct
import sys

# The values WinHelp adds up for each character of a context name.
HASH_TABLE = bytes.fromhex(
    "00d1d2d3d4d5d6d7d8d9dadbdcdddedf"
    "e0e1e2e3e4e5e6e7e8e9eaebecedeeef"
    "f00bf2f3f4f5f6f7f8f9fafbfcfd0cff"
    "0a0102030405060708090a0b0c0d0e0f"
    "101112131415161718191a1b1c1d1e1f"
    "202122232425262728292a0b0c0d0e0d"
    "101112131415161718191a1b1c1d1e1f"
    "202122232425262728292a2b2c2d2e2f"
    "505152535455565758595a5b5c5d5e5f"
    "606162636465666768696a6b6c6d6e6f"
    "707172737475767778797a7b7c7d7e7f"
    "808182830b85868788898a8b8c8d8e8f"
    "909192939495969798999a9b9c9d9e9f"
    "a0a1a2a3a4a5a6a7a8a9aaabacadaeaf"
    "b0b1b2b3b4b5b6b7b8b9babbbcbdbebf"
    "c0c1c2c3c4c5c6c7c8c9cacbcccdcecf")


def context_hash(name):
    """Returns the hash WinHelp keeps of a context name."""
    if not name:
        return 1
    h = 0
    for c in name.encode("cp1252"):
        v = HASH_TABLE[c]
        if v >= 128:
            v -= 256
        h = (h * 43 + v) & 0xFFFFFFFF
    return h


def lz77(data):
    """Compresses data with WinHelp's LZ77: a flag byte for each eight
    items, a literal byte or a 12-bit distance and 4-bit length."""
    out = bytearray()
    i = 0
    n = len(data)
    while i < n:
        flags = 0
        items = bytearray()
        for bit in range(8):
            if i >= n:
                break
            best_len, best_dist = 0, 0
            start = max(0, i - 4096)
            for j in range(i - 1, start - 1, -1):
                k = 0
                while k < 18 and i + k < n and data[j + k] == data[i + k]:
                    k += 1
                if k > best_len:
                    best_len, best_dist = k, i - j
                    if k == 18:
                        break
            if best_len >= 3:
                flags |= 1 << bit
                items += struct.pack("<H", ((best_len - 3) << 12) | (best_dist - 1))
                i += best_len
            else:
                items.append(data[i])
                i += 1
        out.append(flags)
        out += items
    return bytes(out)


def cushort(v):
    """A compressed unsigned short."""
    return bytes([v * 2]) if v < 128 else struct.pack("<H", v * 2 + 1)


def cshort(v):
    """A compressed signed short."""
    return bytes([(v + 64) * 2]) if 0 <= v + 64 < 128 else struct.pack("<H", (v + 16384) * 2 + 1)


def culong(v):
    """A compressed unsigned long."""
    return struct.pack("<H", v * 2) if v < 32768 else struct.pack("<I", v * 2 + 1)


def clong(v):
    """A compressed signed long."""
    return struct.pack("<H", (v + 16384) * 2) if 0 <= v + 16384 < 32768 else struct.pack("<I", (v + 67108864) * 2 + 1)


def btree(entries, structure, page_size=2048, directory=False):
    """Makes a B+ tree of one leaf page from its entries, in order."""
    page = struct.pack("<hhhh", 0, len(entries), -1, -1) + b"".join(entries)
    assert len(page) <= page_size, "one page is enough for these files"
    page = struct.pack("<H", page_size - len(page)) + page[2:] + bytes(page_size - len(page))
    head = struct.pack("<HHH", 0x293B, 0x0402 if directory else 0x0002, page_size)
    head += structure.encode().ljust(16, b"\0")
    head += struct.pack("<hhhhhhi", 0, 0, 0, -1, 1, 1, len(entries))
    return head + page


class Topic:
    """A topic: its context name, title and records."""

    def __init__(self, name, title, nonscroll, scroll, browse=None):
        self.name, self.title = name, title
        self.nonscroll, self.scroll = nonscroll, scroll
        self.browse = browse  # (previous, next) context names


def para(parts, bits=0, values=b"", table_cell=None):
    """A paragraph's format and text: parts are strings (text) and
    bytes (formatting commands, as LinkData1 has them)."""
    return (bits, values, parts, table_cell)


# The commands of the text.
def font(n):
    """Changes to a |FONT descriptor."""
    return b"\x80" + struct.pack("<H", n)


PAR = b"\x82"
TAB = b"\x83"
END_LINK = b"\x89"


def jump(name):
    """Starts a jump's hotspot."""
    return b"\xE3" + struct.pack("<I", context_hash(name))


def popup(name):
    """Starts a popup's hotspot."""
    return b"\xE2" + struct.pack("<I", context_hash(name))


def macro(text):
    """Starts a macro's hotspot."""
    m = text.encode() + b"\0"
    return b"\xC8" + struct.pack("<H", len(m)) + m


def picture(number):
    """A picture (bmc) of a |bmN file."""
    data = struct.pack("<hh", 0, number)
    return b"\x86" + bytes([3]) + clong(len(data)) + data


def topics():
    """The topics of the test files."""
    return [
        Topic("main", "Feature Test",
              [para([font(1), "Feature Test", PAR])],
              [para([font(0), "This topic is the topic of the tests. Jump to the ", jump("second"),
                     "second topic", END_LINK, ", see a ", popup("pop"), "popup", END_LINK,
                     ", or run a ", macro("JumpId(`',`second')"), "macro", END_LINK, ".", PAR],
                    bits=0x0012, values=cshort(6) + cshort(12)),
               ("table", [(0, ["Name", PAR]), (1, ["Value", PAR])]),
               ("table", [(0, ["Alpha", PAR]), (1, ["One", PAR])]),
               para([picture(0), PAR]),
               para([font(2), "code sample", PAR], bits=0x0400)],
              browse=(None, "second")),
        Topic("second", "Second Topic",
              [],
              [para([font(0), "The second topic of the tests. Back to the ", jump("main"), "first topic",
                     END_LINK, ".", PAR])],
              browse=("main", None)),
        Topic("pop", "Popup", [], [para([font(0), "The text of the popup topic.", PAR])]),
    ]


PHRASES = ["topic", "the ", "second", "tests"]


def phrase_compress(text, hall):
    """Compresses LinkData2 with the phrases: |Phrases codes (pairs of
    bytes) or Hall's (one byte for the first phrases, runs of literal
    bytes, spaces and zeros)."""
    out = bytearray()
    i = 0
    literal = bytearray()

    def flush():
        """Writes the pending literal bytes (Hall)."""
        nonlocal literal
        while literal:
            run = literal[:32]
            out.append(((len(run) - 1) << 3) | 3)
            out.extend(run)
            literal = literal[32:]

    while i < len(text):
        for n, p in enumerate(PHRASES):
            b = p.encode()
            if text[i:i + len(b)] == b:
                if hall:
                    flush()
                    out.append(n * 2)
                    i += len(b)
                else:
                    code = n * 2
                    i += len(b)
                    if text[i:i + 1] == b" ":
                        code += 1
                        i += 1
                    out.extend([(code >> 8) + 1, code & 0xFF])
                break
        else:
            if hall and text[i] == 0:
                flush()
                k = 1
                while i + k < len(text) and text[i + k] == 0 and k < 16:
                    k += 1
                out.append(((k - 1) << 4) | 15)
                i += k
            elif hall:
                literal.append(text[i])
                i += 1
            else:
                out.append(text[i])
                i += 1
    if hall:
        flush()
    return bytes(out)


def para_info(bits, values):
    """The start of a paragraph's LinkData1 after the record's sizes."""
    return b"\x00\x80\x00\x00" + struct.pack("<H", bits) + values


def text_of(parts):
    """Splits a paragraph's parts into LinkData1 commands and LinkData2
    strings: before each command, the text up to it."""
    ld1 = bytearray()
    ld2 = bytearray()
    pending = bytearray()
    for p in parts:
        if isinstance(p, str):
            pending += p.encode("cp1252")
        else:
            ld2 += pending + b"\0"
            pending = bytearray()
            ld1 += p
    ld2 += pending + b"\0"
    ld1 += b"\xFF"
    return bytes(ld1), bytes(ld2)


def record(rec):
    """Makes a display (0x20) or table (0x23) record's LinkData1 and
    LinkData2, and its record type."""
    if rec[0] == "table":
        cells = rec[1]
        head = bytes([2, 1]) + struct.pack("<hhhh", 100, 6, 200, 6)
        ld1 = bytearray()
        ld2 = bytearray()
        for col, parts in cells:
            a, b = text_of(parts)
            ld1 += struct.pack("<hh", col, 0) + b"\x80" + para_info(0, b"") + a
            ld2 += b
        ld1 += struct.pack("<h", -1)
        return 0x23, head, bytes(ld1), bytes(ld2)
    bits, values, parts, _ = rec
    a, b = text_of(parts)
    return 0x20, b"", para_info(bits, values) + a, b


def picture_file():
    """|bm0: a 32x16 picture of 16 colors, the left half blue and the
    right red, run-length packed, with a jump's hotspot on its left half."""
    width, height = 32, 16
    row = bytes([0x44] * 8 + [0x11] * 8)  # palette 4 is blue, 1 is red
    raw = row * height
    rle = bytearray()
    i = 0
    while i < len(raw):
        k = 1
        while i + k < len(raw) and raw[i + k] == raw[i] and k < 127:
            k += 1
        rle += bytes([k, raw[i]])
        i += k
    palette = bytearray()
    vga = [(0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0), (0, 0, 255), (128, 0, 128), (0, 128, 128),
           (192, 192, 192), (128, 128, 128), (255, 0, 0), (0, 255, 0), (255, 255, 0), (0, 0, 255),
           (255, 0, 255), (0, 255, 255), (255, 255, 255)]
    vga[1] = (255, 0, 0)
    for r, g, b in vga:
        palette += bytes([b, g, r, 0])
    hotspots = bytes([1]) + struct.pack("<HI", 1, 0)
    hotspots += bytes([0xE3, 0, 0]) + struct.pack("<HHHHI", 0, 0, 16, 16, context_hash("second"))
    hotspots += b"left\0second\0"

    def header(coff, hoff):
        """The picture's header, for the data's and hotspots' places."""
        h = bytes([6, 1]) + culong(96) + culong(96) + cushort(1) + cushort(4) + culong(width) + culong(height)
        h += culong(16) + culong(0) + culong(len(rle)) + culong(len(hotspots)) + struct.pack("<II", coff, hoff)
        return h

    size = len(header(0, 0)) + len(palette)
    pic = header(size, size + len(rle)) + palette + rle + hotspots
    return struct.pack("<HHI", 0x506C, 1, 8) + pic


def build(path, hall):
    """Writes one test file: WinHelp 3.1 with |Phrases, or 4.0 with Hall
    compression."""
    files = {}
    minor = 33 if hall else 21
    tlist = topics()
    # |TOPIC: the TOPICLINKs of one block, then the block compressed.
    links = []  # (record type, LD1 fields, LD2 plain)
    offsets = {}  # context -> TOPICOFFSET
    count = 0
    for t in tlist:
        offsets[t.name] = count
        recs = [record(r) for r in t.nonscroll + t.scroll]
        links.append(("header", t, recs))
        for kind, head, ld1, ld2 in recs:
            count += len(ld2)
    block = bytearray()
    positions = []
    # First pass: the sizes, to know each link's TOPICPOS.
    built = []
    for item in links:
        _, t, recs = item
        title = t.title.encode("cp1252") + b"\0"
        built.append([2, None, title, t])
        for kind, head, ld1, ld2 in recs:
            topic_size = clong(len(ld2))
            built.append([kind, cushort(len(ld2)) + head + ld1, ld2, topic_size])
    # The parts of each link, their sizes known once the positions are.
    def encode(ld2):
        """LinkData2 as stored: compressed by the phrases when shorter."""
        c = phrase_compress(ld2, hall)
        if len(c) < len(ld2):
            stats[0] += 1
        return c if len(c) < len(ld2) else ld2

    stats = [0]
    pos = 12
    for b in built:
        positions.append(pos)
        if b[0] == 2:
            ld1_len = 28
        else:
            ld1_len = len(b[3]) + len(b[1])
        pos += 21 + ld1_len + len(encode(b[2]))
    topic_pos = {}
    k = 0
    for item in links:
        topic_pos[item[1].name] = k
        k += 1 + len(item[2])
    for i, b in enumerate(built):
        nxt = positions[i + 1] if i + 1 < len(built) else -1
        prv = positions[i - 1] if i else -1
        if b[0] == 2:
            t = b[3]
            first = topic_pos[t.name]
            ns = positions[first + 1] if t.nonscroll else -1
            sc = positions[first + 1 + len(t.nonscroll)] if t.scroll else -1
            back = offsets[t.browse[0]] if t.browse and t.browse[0] else -1
            fwd = offsets[t.browse[1]] if t.browse and t.browse[1] else -1
            following = [n for n in tlist[tlist.index(t) + 1:]]
            next_topic = positions[topic_pos[following[0].name]] if following else -1
            ld1 = struct.pack("<iiiiiii", 0, back, fwd, tlist.index(t), ns, sc, next_topic)
        else:
            ld1 = b[3] + b[1]
        stored = encode(b[2])
        link = struct.pack("<IIiiIB", 21 + len(ld1) + len(stored), len(b[2]), prv, nxt, 21 + len(ld1), b[0])
        block += link + ld1 + stored
    assert len(block) < 0x4000
    assert stats[0] > 0, "the phrases compress some of the text"
    compressed = lz77(bytes(block))
    assert len(compressed) <= 4096 - 12
    files["|TOPIC"] = struct.pack("<iii", -1, 12, 0) + compressed
    # |SYSTEM: the version, LZ77 in 4K blocks, and the records.
    sysrec = b""

    def rec(kind, data):
        """A |SYSTEM record."""
        return struct.pack("<HH", kind, len(data)) + data

    sysrec += rec(1, b"Feature Test Help\0")
    sysrec += rec(2, b"Copyright 2026 the clang-retro MFC authors\0")
    sysrec += rec(3, struct.pack("<I", offsets["main"]))
    sysrec += rec(4, b"BrowseButtons()\0")
    win = struct.pack("<H", 0x0002 | 0x0004 | 0x0100)
    win += b"".ljust(10, b"\0") + b"main".ljust(9, b"\0") + b"Feature Window".ljust(51, b"\0")
    win += struct.pack("<hhhhh", 0, 0, 0, 0, 0) + struct.pack("<II", 0xF0FFFF, 0xFFFFFF)
    sysrec += rec(6, win)
    files["|SYSTEM"] = struct.pack("<HHHIH", 0x036C, minor, 1, 0, 4) + sysrec
    # |FONT: three faces, three descriptors (of WinHelp 3.1).
    faces = [b"Arial", b"Arial", b"Courier New"]
    face_len = 20
    descs = b""
    for i, (attr, half) in enumerate([(0, 20), (1, 28), (0, 20)]):
        descs += struct.pack("<BBBH", attr, half, 3 if i < 2 else 1, i) + bytes([0, 0, 0]) + bytes([255, 255, 255])
    face_off = 8
    desc_off = face_off + face_len * len(faces)
    files["|FONT"] = struct.pack("<HHHH", len(faces), 3, face_off, desc_off) + b"".join(
        f.ljust(face_len, b"\0") for f in faces) + descs
    # The phrases.
    if hall:
        image = "".join(PHRASES).encode()
        bits = []
        for p in PHRASES:
            m = len(p) - 1
            bits += [1] * (m >> 4) + [0] + [(m >> b) & 1 for b in range(4)]
        words = bytearray()
        for w in range(0, len(bits), 32):
            v = 0
            for b, x in enumerate(bits[w:w + 32]):
                v |= x << b
            words += struct.pack("<I", v)
        words += struct.pack("<I", 0)  # the phrases' search bits
        files["|PhrIndex"] = struct.pack("<IIIIIIHH", 1, len(PHRASES), len(words), len(image), len(image), 0, 4,
                                         0x4A00) + bytes(words)
        files["|PhrImage"] = image
    else:
        text = "".join(PHRASES).encode()
        offs = [2 * (len(PHRASES) + 1)]
        for p in PHRASES:
            offs.append(offs[-1] + len(p))
        files["|Phrases"] = struct.pack("<HHI", len(PHRASES), 0x0100, len(text)) + b"".join(
            struct.pack("<H", o) for o in offs) + lz77(text)
    # |CONTEXT, |CTXOMAP, |TTLBTREE.
    names = sorted((context_hash(t.name), offsets[t.name]) for t in tlist)
    files["|CONTEXT"] = btree([struct.pack("<II", h, o) for h, o in names], "L4")
    maps = [(100, "main"), (200, "second"), (300, "pop")]
    files["|CTXOMAP"] = struct.pack("<H", len(maps)) + b"".join(struct.pack("<II", m, offsets[n]) for m, n in maps)
    files["|TTLBTREE"] = btree([struct.pack("<I", offsets[t.name]) + t.title.encode() + b"\0" for t in tlist], "Lz")
    # The index: |KWBTREE and |KWDATA.
    keywords = [("alpha", ["main", "second"]), ("beta", ["second"])]
    data = b""
    entries = []
    for kw, ts in keywords:
        entries.append(kw.encode() + b"\0" + struct.pack("<HI", len(ts), len(data)))
        data += b"".join(struct.pack("<I", offsets[t]) for t in ts)
    files["|KWBTREE"] = btree(entries, "F24")
    files["|KWDATA"] = data
    files["|bm0"] = picture_file()
    # The file: the header, the internal files, then the directory.
    out = bytearray(16)
    places = {}
    for name in sorted(files):
        places[name] = len(out)
        body = files[name]
        out += struct.pack("<IIB", len(body) + 9, len(body), 0) + body
    entries = [name.encode() + b"\0" + struct.pack("<I", places[name]) for name in sorted(files)]
    directory = btree(entries, "z4", page_size=1024, directory=True)
    dir_start = len(out)
    out += struct.pack("<IIB", len(directory) + 9, len(directory), 4) + directory
    out[0:16] = struct.pack("<IIiI", 0x00035F3F, dir_start, -1, len(out))
    with open(path, "wb") as f:
        f.write(out)


def main():
    """Writes both files to the directory named on the command line."""
    outdir = sys.argv[1] if len(sys.argv) > 1 else "."
    build(os.path.join(outdir, "feature.hlp"), hall=False)
    build(os.path.join(outdir, "feature95.hlp"), hall=True)


if __name__ == "__main__":
    main()
