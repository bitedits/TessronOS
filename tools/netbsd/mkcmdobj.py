#!/usr/bin/env python3
#
# mkcmdobj.py -- the standard commands as program objects of the system volume
#
# Copyright (C) 2026 satromi
# This software is distributed under the T-License 2.2.
#
"""The commands tools/netbsd/cmds.sh builds, made into program objects
linked from the object "bin" (design 5.8.7).

  mkcmdobj.py --bin DIR --box BIN.xtad --ico ICON --out DIR [--origin FILE]

Every file of --bin is one command: its object is written into --out in
the form etc/def keeps objects in ({uuid}.json, {uuid}_0.xtad, {uuid}.ico,
{uuid}_1.bin, the program being record 1). The record 0 of "bin"
(--box) is written into --out too, with a virtual object of each command
added after the ones it has, in rows of four; a command it links already
is not added again. --out is given to tools/mktsfs with --over, so that
this "bin" replaces the one of etc/def.

The UUIDs are made from the commands' names, so the same commands give
the same volume every time. --origin names a file of lines
"name<TAB>where it comes from" for the text of record 0.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import sys
import uuid

STAMP = "2026-10-10T00:00:00Z"
ACCESS = {
    "owner": "01a0d680-3aa0-75b3-bd7d-075e14217cd2",
    "group": "01a0d680-3aa1-7922-ae79-ba5cbbaa2037",
    "mode": "rwxr-xr-x",
    "attr": ["perm"],
    "acl": [],
    "records": [],
}
LINK_NS = uuid.UUID("01a0d8c4-5480-7000-8000-000000000000")
LINK_RE = re.compile(r'<link\b[^>]*/>')
UUID_RE = re.compile(r'id="([0-9a-f-]{36})_0\.xtad"')
COLS = 4
COL_W = 248
ROW = 32


def cmd_uuid(name):
    """A version 7 UUID whose time is fixed and whose rest is the name's hash"""
    h = bytearray(hashlib.sha1(("tessronos-command:" + name).encode("utf-8")).digest()[:16])
    h[0:6] = bytes.fromhex("01a0d8c45480")
    h[6] = 0x70 | (h[6] & 0x0F)
    h[8] = 0x80 | (h[8] & 0x3F)
    return str(uuid.UUID(bytes=bytes(h)))


def xml(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def link_xml(target, left, top):
    vid = uuid.uuid5(LINK_NS, target)
    return ('<link id="%s_0.xtad" vobjid="%s" vobjleft="%d" vobjtop="%d" vobjright="%d" '
            'vobjbottom="%d" height="25" chsz="14" frcol="#000000" chcol="#000000" '
            'tbcol="#ffffff" bgcol="#ffffff" pictdisp="true" namedisp="true" framedisp="true"/>'
            % (target, vid, left, top, left + COL_W - 8, top + 25))


def meta(name):
    return {
        "name": name,
        "relationship": [],
        "linktype": False,
        "makeDate": STAMP,
        "updateDate": STAMP,
        "accessDate": STAMP,
        "periodDate": None,
        "refCount": 1,
        "recordCount": 2,
        "editable": False,
        "deletable": False,
        "readable": True,
        "maker": "TessronOS",
        "applist": {},
        "tessronos": {
            "program": {
                "id": "cmd-" + name,
                "kind": "command",
                "name": name,
                "version": "R1.100",
                "menu": False,
                "builtin": False,
                "base": [],
                "posix": True,
            },
            "exec": {
                "record": 1,
                "type": "application/x-elf",
                "arch": "aarch64",
                "abi": "tessronos-1",
            },
            "records": [{"n": 0, "rt": 1}, {"n": 1, "rt": 9}],
            "access": ACCESS,
        },
    }


def write(path, data):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(data)


def main():
    p = argparse.ArgumentParser(description="make the commands into program objects")
    p.add_argument("--bin", required=True, help="directory of the built commands")
    p.add_argument("--box", required=True, help="record 0 of the object bin")
    p.add_argument("--ico", required=True, help="icon of a command")
    p.add_argument("--out", required=True, help="directory to write the objects into")
    p.add_argument("--origin", help="lines of name, a tab, where the command comes from")
    a = p.parse_args()

    origin = {}
    if a.origin:
        with open(a.origin, encoding="utf-8") as f:
            for line in f:
                line = line.rstrip("\n")
                if "\t" in line:
                    k, v = line.split("\t", 1)
                    origin[k] = v

    names = sorted(n for n in os.listdir(a.bin) if os.path.isfile(os.path.join(a.bin, n)))
    if not names:
        sys.exit("mkcmdobj: no command in %s" % a.bin)
    if os.path.isdir(a.out):
        shutil.rmtree(a.out)
    os.makedirs(a.out)

    for n in names:
        u = cmd_uuid(n)
        write(os.path.join(a.out, u + ".json"),
              json.dumps(meta(n), ensure_ascii=False, indent=2) + "\n")
        text = origin.get(n, "")
        write(os.path.join(a.out, u + "_0.xtad"),
              '<tad version="1.0" encoding="UTF-8" filename="%s">\n<document>\n<p>%s</p>\n'
              '<p>%s実行形式はレコード1</p>\n</document>\n</tad>\n'
              % (xml(n), xml(n), xml(text + "。" if text else "")))
        shutil.copyfile(os.path.join(a.bin, n), os.path.join(a.out, u + "_1.bin"))
        shutil.copyfile(a.ico, os.path.join(a.out, u + ".ico"))

    # bin: its own virtual objects first, then the commands it does not link yet
    with open(a.box, encoding="utf-8") as f:
        box = f.read()
    have = set(UUID_RE.findall(box))
    nold = len(LINK_RE.findall(box))
    top0 = 8 + ROW * ((nold + COLS - 1) // COLS)
    add = []
    k = 0
    for n in names:
        u = cmd_uuid(n)
        if u in have:
            continue
        add.append(link_xml(u, 8 + COL_W * (k % COLS), top0 + ROW * (k // COLS)))
        k += 1
    end = box.rfind("</figure>")
    if end < 0:
        sys.exit("mkcmdobj: %s has no figure" % a.box)
    box = box[:end] + "".join(s + "\n" for s in add) + box[end:]
    write(os.path.join(a.out, os.path.basename(a.box)), box)
    print("mkcmdobj: %d commands in %s" % (len(names), a.out))


if __name__ == "__main__":
    main()
