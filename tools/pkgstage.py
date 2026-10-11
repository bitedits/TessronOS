#!/usr/bin/env python3
"""Packages (.tpk) built into the system volume, as installing them from
the バージョン sheet of システム環境設定 would put them.

  tools/pkgstage.py --box BOX.xtad --out DIR PKG.tpk [PKG.tpk ...]

Each package's objects are written into DIR in the TADjs form they are
kept in ({uuid}.json, {uuid}_N.xtad, {uuid}_N.bin, {uuid}.ico), and the
program box's record 0 (BOX.xtad) is written into DIR with a virtual
object of each package's program added after the ones it has, one row
further down each; a program the box links already is not added again.
DIR is then given to tools/mktsfs with --over, so that its program box
replaces the one of etc/def.

The virtual object's id is made from the program's UUID, so the same
packages give the same volume every time.
"""

import argparse
import json
import os
import re
import struct
import sys
import uuid

MAGIC = b"TSPK"
HEAD = 16
ENTRY = 64
NAME_MAX = 48
FILE_RE = re.compile(r"^([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})"
                     r"(\.json|\.ico|_([0-9]+)\.(xtad|bin)|_([0-9]+)_([0-9]+)\.[a-z0-9]+)$")
LINK_RE = re.compile(r'<link\b[^>]*/>')
ROW = 32                        # from one virtual object of the box to the next
LINK_NS = uuid.UUID("01a0d8c4-5213-7000-8000-000000000000")


def entries(path):
    """[(name, bytes)] of a package"""
    data = open(path, "rb").read()
    if len(data) < HEAD or data[:4] != MAGIC:
        sys.exit("pkgstage: %s is not a package" % path)
    fmt, n = struct.unpack_from("<HH", data, 4)
    if fmt != 1:
        sys.exit("pkgstage: %s is of format %d, not 1" % (path, fmt))
    out = []
    for i in range(n):
        name, off, size, _rt, _z = struct.unpack_from("<%dsIIII" % NAME_MAX, data, HEAD + i * ENTRY)
        name = name.rstrip(b"\0").decode("ascii")
        if off + size > len(data):
            sys.exit("pkgstage: %s: %s runs past the end" % (path, name))
        out.append((name, data[off:off + size]))
    return out


def link_xml(prog, top):
    vid = uuid.uuid5(LINK_NS, prog)
    return ('<link id="%s_0.xtad" vobjid="%s" vobjleft="8" vobjtop="%d" vobjright="248" '
            'vobjbottom="%d" height="25" chsz="14" frcol="#000000" chcol="#000000" '
            'tbcol="#ffffff" bgcol="#ffffff" pictdisp="true" namedisp="true" framedisp="true"/>'
            % (prog, vid, top, top + 25))


def main():
    p = argparse.ArgumentParser(prog="pkgstage")
    p.add_argument("--box", required=True, help="the program box's record 0 (etc/def)")
    p.add_argument("--out", required=True, help="where the objects are written")
    p.add_argument("pkgs", nargs="+")
    a = p.parse_args()

    os.makedirs(a.out, exist_ok=True)
    for name in os.listdir(a.out):
        os.remove(os.path.join(a.out, name))

    box = open(a.box, encoding="utf-8").read()
    links = LINK_RE.findall(box)
    bottom = 0
    for l in links:
        m = re.search(r'vobjbottom="(-?[0-9]+)"', l)
        if m:
            bottom = max(bottom, int(m.group(1)))
    added = []
    for path in a.pkgs:
        ent = entries(path)
        meta = None
        for name, data in ent:
            if name == "package.json":
                meta = json.loads(data.decode("utf-8"))
                continue
            if not FILE_RE.match(name):
                sys.exit("pkgstage: %s: %s is not a file of an object" % (path, name))
            with open(os.path.join(a.out, name), "wb") as f:
                f.write(data)
        if meta is None or "program" not in meta:
            sys.exit("pkgstage: %s has no package.json" % path)
        prog = meta["program"]
        if ('id="%s_0.xtad"' % prog) not in box:
            top = bottom + ROW - 25
            bottom = top + 25
            added.append(link_xml(prog, top))
        print("pkgstage: %s %s (%s, %d objects)"
              % (meta.get("id", "?"), meta.get("version", "?"), os.path.basename(path),
                 len(meta.get("objects", []))))

    if added:
        i = box.rindex("</figure>")
        box = box[:i] + "".join(l + "\n" for l in added) + box[i:]
    with open(os.path.join(a.out, os.path.basename(a.box)), "w", encoding="utf-8", newline="\n") as f:
        f.write(box)
    return 0


if __name__ == "__main__":
    sys.exit(main())
