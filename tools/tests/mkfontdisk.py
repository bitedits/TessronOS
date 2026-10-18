#!/usr/bin/env python3
"""Make the USB disk the test of システム環境設定's 書体 plugs in
(tools/tests/ui_sysenv_font.steps, docs/tessronos 11.6).

  mkfontdisk.py OUTDIR

OUTDIR/font.img is a FAT32 volume of 40MB with no partition table, holding
the files every volume tools/mkfat.py makes and, in its root, the face
etc/font/NotoSerifJP-Regular.otf (SIL Open Font License, etc/font/OFL.txt):
ファイル変換 takes it in as an object whose record is the font file, which
is then dropped on the list of faces to be registered.
"""

import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TOOLS = os.path.dirname(HERE)
TOP = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)

from mkfat import fat32_format    # noqa: E402

SECTOR = 512


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    font = os.path.join(TOP, "etc", "font", "NotoSerifJP-Regular.otf")
    nsect = 40 * 1024 * 1024 // SECTOR
    img = bytearray(nsect * SECTOR)
    fat32_format(img, 0, nsect, ["NotoSerifJP-Regular.otf=" + font])
    with open(os.path.join(out, "font.img"), "wb") as f:
        f.write(img)


if __name__ == "__main__":
    main()
