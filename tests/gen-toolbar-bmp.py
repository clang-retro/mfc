#!/usr/bin/env python3
"""Writes a 4-bit toolbar bitmap: N 16x15 images on the classic gray.

Usage: gen-toolbar-bmp.py out.bmp [count]
The images are simple shapes so screenshots show which button is which.
"""
import struct
import sys

out = sys.argv[1]
count = int(sys.argv[2]) if len(sys.argv) > 2 else 6
W, H = 16 * count, 15
# The 16 VGA colors; index 7 is the light gray LR_LOADMAP3DCOLORS maps.
palette = [
    (0, 0, 0), (128, 0, 0), (0, 128, 0), (128, 128, 0), (0, 0, 128), (128, 0, 128),
    (0, 128, 128), (192, 192, 192), (128, 128, 128), (255, 0, 0), (0, 255, 0),
    (255, 255, 0), (0, 0, 255), (255, 0, 255), (0, 255, 255), (255, 255, 255),
]
pix = [[7] * W for _ in range(H)]


def rect(x0, y0, x1, y1, c, fill=None):
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            edge = x in (x0, x1) or y in (y0, y1)
            if edge:
                pix[y][x] = c
            elif fill is not None:
                pix[y][x] = fill


for i in range(count):
    ox = i * 16
    kind = i % 6
    if kind == 0:  # new: a page
        rect(ox + 3, 1, ox + 11, 13, 0, 15)
    elif kind == 1:  # open: a folder
        rect(ox + 1, 4, ox + 14, 12, 0, 11)
    elif kind == 2:  # save: a disk
        rect(ox + 2, 2, ox + 13, 13, 0, 4)
        rect(ox + 5, 3, ox + 10, 6, 0, 15)
    elif kind == 3:  # cut: two circles
        rect(ox + 2, 9, ox + 6, 13, 0)
        rect(ox + 9, 9, ox + 13, 13, 0)
        for d in range(7):
            pix[2 + d][ox + 4 + d // 2] = 0
            pix[2 + d][ox + 11 - d // 2] = 0
    elif kind == 4:  # copy: two pages
        rect(ox + 1, 1, ox + 8, 10, 0, 15)
        rect(ox + 6, 4, ox + 14, 13, 0, 15)
    else:  # paste: a clipboard
        rect(ox + 2, 2, ox + 11, 13, 0, 3)
        rect(ox + 7, 6, ox + 14, 14, 0, 15)

row = (W * 4 + 31) // 32 * 4
data = bytearray()
for y in range(H - 1, -1, -1):
    line = bytearray()
    for x in range(0, W, 2):
        hi = pix[y][x]
        lo = pix[y][x + 1] if x + 1 < W else 0
        line.append(hi << 4 | lo)
    line += b"\0" * (row - len(line))
    data += line
pal = b"".join(struct.pack("<BBBB", b, g, r, 0) for (r, g, b) in palette)
info = struct.pack("<IiiHHIIiiII", 40, W, H, 1, 4, 0, len(data), 2835, 2835, 16, 0)
off = 14 + len(info) + len(pal)
header = struct.pack("<HIHHI", 0x4D42, off + len(data), 0, 0, off)
with open(out, "wb") as f:
    f.write(header + info + pal + data)
