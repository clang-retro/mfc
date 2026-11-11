#!/usr/bin/env python3
# gen-builtin-font.py - turns the public domain X11 "fixed" fonts into
# port/src/font_builtin.inc, the bitmap font text falls back on when no
# font file can be found.
#
#   python3 port/tools/gen-builtin-font.py 6x13.bdf 6x13B.bdf > port/src/font_builtin.inc
#
# The .bdf files come from https://gitlab.freedesktop.org/xorg/font/misc-misc
# ("Public domain font. Share and enjoy.").
#
# Part of clang-retro MFC, an independent implementation of the MFC API.
# SPDX-License-Identifier: AGPL-3.0-only
import sys

# Printable ASCII and Latin-1, and the characters of code page 1252 above
# 0x7F that Latin-1 lacks.
CHARS = list(range(0x20, 0x7F)) + list(range(0xA0, 0x100)) + [
    0x20AC, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030,
    0x0160, 0x2039, 0x0152, 0x017D, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
    0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x017E, 0x0178,
    0xFFFD,
]


def read_bdf(path):
    glyphs = {}
    ascent = descent = 0
    width = height = 0
    with open(path, encoding="latin-1") as f:
        lines = iter(f.read().splitlines())
    for line in lines:
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "FONTBOUNDINGBOX":
            width, height = int(parts[1]), int(parts[2])
        elif parts[0] == "FONT_ASCENT":
            ascent = int(parts[1])
        elif parts[0] == "FONT_DESCENT":
            descent = int(parts[1])
        elif parts[0] == "STARTCHAR":
            code = None
            bbx = None
            rows = []
            for line in lines:
                p = line.split()
                if p[0] == "ENCODING":
                    code = int(p[1])
                elif p[0] == "BBX":
                    bbx = [int(v) for v in p[1:5]]
                elif p[0] == "BITMAP":
                    for line in lines:
                        if line.startswith("ENDCHAR"):
                            break
                        rows.append(int(line, 16) << (8 * (4 - len(line) // 2)))
                    break
            glyphs[code] = (bbx, rows)
    return width, ascent, descent, glyphs


def cell(width, ascent, descent, glyph):
    (w, h, xoff, yoff), rows = glyph
    out = [0] * (ascent + descent)
    for i, bits in enumerate(rows):
        y = ascent - (yoff + h) + i
        if 0 <= y < len(out):
            # Bits run from bit 31 down; shift them into place at xoff.
            v = (bits >> 24) >> xoff if xoff >= 0 else (bits >> 24) << -xoff
            out[y] |= v & ((0xFF << (8 - width)) & 0xFF)
    return out


def main():
    fonts = [read_bdf(p) for p in sys.argv[1:]]
    width, ascent, descent = fonts[0][0], fonts[0][1], fonts[0][2]
    print("// font_builtin.inc - made by port/tools/gen-builtin-font.py from the")
    print("// X11 misc-misc fonts 6x13 and 6x13B: \"Public domain font. Share and enjoy.\"")
    print("// Each glyph is one byte per row, its leftmost pixel the top bit.")
    print(f"static const int kBuiltinWidth = {width}, kBuiltinAscent = {ascent}, kBuiltinDescent = {descent};")
    print(f"static const int kBuiltinCount = {len(CHARS)};")
    print("static const unsigned short kBuiltinChars[kBuiltinCount] = {")
    for i in range(0, len(CHARS), 12):
        print("  " + ", ".join(f"0x{c:04X}" for c in CHARS[i:i + 12]) + ",")
    print("};")
    for n, (w, a, d, glyphs) in enumerate(fonts):
        name = ["kBuiltinRegular", "kBuiltinBold"][n]
        print(f"static const unsigned char {name}[kBuiltinCount][{ascent + descent}] = {{")
        for c in CHARS:
            g = glyphs.get(c) or glyphs.get(0xFFFD)
            rows = cell(width, ascent, descent, g)
            print("  {" + ", ".join(f"0x{r:02X}" for r in rows) + "},")
        print("};")


main()
