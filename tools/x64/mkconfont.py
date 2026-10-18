#!/usr/bin/env python3
"""Make the bitmap font of the x64 screen console (kernel/sysdepend/x64/confont.h).

  mkconfont.py [font] > kernel/sysdepend/x64/confont.h

Each printable ASCII character (0x20-0x7e) is drawn with the given font
(by default etc/font/NotoSansMonoCJKjp-Regular.otf, the monospaced face
of the system's fonts) into a cell of 8 x 16 pixels and cut to one bit a
pixel: 16 bytes a character, the top row first, the leftmost pixel in
bit 7. The console draws it before the kernel has any other font, from
the first line the kernel says to an exception that stops it.

Needs Pillow (python3 -m pip install pillow).
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

W, H = 8, 16
FIRST, LAST = 0x20, 0x7e


def main():
    top = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(top, "etc", "font", "NotoSansMonoCJKjp-Regular.otf")
    font = ImageFont.truetype(path, 14)
    # the baseline at row 12: capitals from row 2, descenders to the last row
    probe = ImageDraw.Draw(Image.new("L", (1, 1)))
    _, cap_top, _, cap_bot = probe.textbbox((0, 0), "H", font=font)
    base = 12
    rows = []
    for code in range(FIRST, LAST + 1):
        img = Image.new("L", (W * 4, H * 2), 0)
        d = ImageDraw.Draw(img)
        ch = chr(code)
        left, _, right, _ = d.textbbox((0, 0), ch, font=font)
        x = (W - (right - left)) // 2 - left
        d.text((W + x, base - cap_bot), ch, font=font, fill=255)
        cell = img.crop((W, 0, 2 * W, H))
        px = cell.load()
        out = []
        for y in range(H):
            b = 0
            for xx in range(W):
                if px[xx, y] >= 96:
                    b |= 0x80 >> xx
            out.append(b)
        rows.append((code, out))

    print("/*")
    print(" *\tconfont.h")
    print(" *\tThe bitmap font of the screen console (fbcon.c): 8 x 16 pixels for each")
    print(" *\tprintable ASCII character, the top row first, the leftmost pixel in bit 7.")
    print(" *\tMade by tools/x64/mkconfont.py from %s." % os.path.relpath(path, top).replace(os.sep, "/"))
    print(" */")
    print()
    print("#define CONFONT_W\t%d" % W)
    print("#define CONFONT_H\t%d" % H)
    print("#define CONFONT_FIRST\t0x%02x" % FIRST)
    print("#define CONFONT_LAST\t0x%02x" % LAST)
    print()
    print("LOCAL const UB confont[CONFONT_LAST - CONFONT_FIRST + 1][CONFONT_H] = {")
    for code, out in rows:
        c = chr(code)
        name = "'\\\\'" if c == "\\" else ("'\\''" if c == "'" else "'%s'" % c)
        print("\t{ %s },\t/* %s */" % (", ".join("0x%02x" % b for b in out), name.replace("*/", "* /")))
    print("};")


if __name__ == "__main__":
    main()
