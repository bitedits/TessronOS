#!/usr/bin/env python3
"""A BTRON CD-ROM image for the tests (design 11.9).

    python3 tools/tests/mkbtcd.py OUT.iso [--part OUT.img] [--block 2048] [--be] [--json]

The image is an ISO 9660 disc whose one file, HDA2.IMG, is the image of
a hard disk partition of the BTRON file system, and whose volume
partition descriptor (type 3, system identifier "BTRON SPECIFIED FILE
SYSTEM") names that file's extent. The file system is made here in the
semi-standard form (low byte first; --be: the standard form, high byte
first, its TAD too), the extended form 2, with the 実身 the tests look
for:

  root (file ID 0)   a figure of 仮身 to the five below
  はじめに            a text, a 仮身 to 図１ in it
  図１                a figure: two rectangles and a 仮身 to はじめに;
                     its 実行機能付箋 names 基本図形編集
  資料               a figure of 仮身 (仮身一覧): はじめに, 大きな実身,
                     多段の実身, a link file to another file system and
                     one to 図１ of this one
  大きな実身          a text of 9000 characters in runs of blocks that are
                     not next to each other (connection indexes), and 130
                     records of an unknown type: index level 1
  多段の実身          a text through index level 2, its record indexes
                     where the extended form 2 lets them start (ridxofs)
  住所カード          a マイクロカード実身: type 31 records, the format
                     first, and a link record

With --json, what each holds is written to OUT.iso.json too, for the
tests to compare.
Nothing here comes from a real product: the file system is made from
its published layout.
"""

import argparse
import json
import struct
import sys

STIME0 = 1157932800          # a time of 2021, as seconds from 1985

TS_TEXT, TS_TEXTEND, TS_FIG, TS_FIGEND = 0xFFE1, 0xFFE2, 0xFFE3, 0xFFE4
TS_VOBJ, TS_FPRIM = 0xFFE6, 0xFFB0


# ---------------------------------------------------------------- TAD, in the file system's byte order

ORDER = "<"                  # "<" the semi-standard form, ">" the standard form


def words(*ws):
    return b"".join(struct.pack(ORDER + "H", w & 0xFFFF) for w in ws)


def dword(v):
    """A 32-bit number: its high word first in the standard form"""
    return struct.pack(ORDER + "I", v & 0xFFFFFFFF)


def seg(sid, body):
    if len(body) >= 0xFFFF:
        return struct.pack(ORDER + "HHI", sid, 0xFFFF, len(body)) + body
    return struct.pack(ORDER + "HH", sid, len(body)) + body


def fusen(sid, sub, attr, body=b""):
    return seg(sid, words((sub << 8) | attr) + body)


def tron_codes(s):
    """A string as TRON characters of plane 1 (JIS X 0208; ASCII as its full width forms)"""
    out = []
    for ch in s:
        if ch == " ":
            ch = "　"
        elif 0x21 <= ord(ch) <= 0x7E:
            ch = chr(ord(ch) + 0xFEE0)
        if ch == "\n":
            out.append(0x000A)
            continue
        if ch == "－":
            ch = "−"
        b = ch.encode("euc_jp")
        out.append(((b[0] & 0x7F) << 8) | (b[1] & 0x7F))
    return out


def tron(s):
    return words(*tron_codes(s))


def text_start():
    return seg(TS_TEXT, words(0, 0, 0, 0, 0, 0, 0, 0, -120, -120, 0x21, 0))


def fig_start(w=400, h=300):
    return seg(TS_FIG, words(0, 0, w, h, 0, 0, w, h, -120, -120, 0, 0))


BLACK, WHITE = 0x10000000, 0x10FFFFFF                          # COLOR: RGB


def vobj(l, t, r, b):
    """A closed 仮身: black frame and letters on a white band and ground"""
    return seg(TS_VOBJ, words(l, t, r, b, b - t, 0x8000 | 280) + dword(BLACK) + dword(BLACK) + dword(WHITE)
               + dword(WHITE) + words(0))


def rect(l, t, r, b):
    """A rectangle drawn with a line of pattern 1, not filled (pattern 0)"""
    return fusen(TS_FPRIM, 0, 0, words(0x0001, 1, 0, 0, l, t, r, b))


def text_tad(*parts):
    """A text: strings as characters, bytes as segments"""
    body = b""
    for p in parts:
        body += tron(p) if isinstance(p, str) else p
    return text_start() + body + seg(TS_TEXTEND, b"")


def figure_tad(*parts, w=400, h=300):
    return fig_start(w, h) + b"".join(parts) + seg(TS_FIGEND, b"")


def exec_rec(appl_mid, name, kind, win):
    """A 実行機能付箋 record of one entry: the application and the window it opens"""
    nm = tron_codes(name)[:16]
    kd = tron_codes(kind)[:16]
    data = words(0, 0, *win) + bytes(8)
    head = bytes(0x18) + words(0x8000, appl_mid, 0x8000) + words(*(nm + [0] * (16 - len(nm))))
    head += words(*(kd + [0] * (16 - len(kd)))) + words(len(data))
    assert len(head) == 0x60
    return head + data


# ---------------------------------------------------------------- the file system

class Fs:
    def __init__(self, bsize=2048, le=True, nfmax=256, nlb=1200, name="試験ＣＤ", locat="ＣＤ－ＲＯＭ"):
        self.bsize, self.le, self.nfmax, self.nlb = bsize, le, nfmax, nlb
        self.e = "<" if le else ">"
        self.name, self.locat = name, locat
        self.img = bytearray(bsize * nlb)
        self.used = [False] * nlb
        self.files = {}              # fid -> what the tests are told
        self.refs = {}
        # where the tables go
        self.nbmp_words = (nlb + 31) // 32
        sys_bytes = 128 + 2 * self.nbmp_words * 4
        self.ssys = (sys_bytes + bsize - 1) // bsize
        self.sfidt = (nfmax * 4 + bsize - 1) // bsize
        self.sfnmt = self.sfidt
        for b in range(1 + self.ssys + self.sfidt + self.sfnmt):
            self.used[b] = True
        self.root_blk = 1 + self.ssys + self.sfidt + self.sfnmt
        self.fid_blk = {}
        self.hashes = {}
        self.next_free = self.root_blk

    # numbers in the file system's order
    def h(self, v):
        return struct.pack(self.e + "H", v & 0xFFFF)

    def w(self, v):
        return struct.pack(self.e + "I", v & 0xFFFFFFFF)

    def a24(self, v):
        b = struct.pack(">I", v)[1:]
        return b[::-1] if self.le else b

    def tc(self, s, n=20):
        c = tron_codes(s)[:n]
        return b"".join(self.h(x) for x in c + [0] * (n - len(c)))

    def put(self, blk, off, data):
        at = blk * self.bsize + off
        self.img[at:at + len(data)] = data

    def alloc(self, n, gap=False):
        """n blocks as runs: one run, or (gap) runs of two with a block left out between"""
        runs = []
        while n > 0:
            while self.used[self.next_free]:
                self.next_free += 1
            start = self.next_free
            k = min(n, 2 if gap else 255)
            for b in range(start, start + k):
                assert not self.used[b], "out of blocks"
                self.used[b] = True
            runs.append([start, k])
            n -= k
            self.next_free = start + k + (1 if gap else 0)
        return runs

    def header_block(self, fid):
        if fid == 0:
            self.used[self.root_blk] = True
            return self.root_blk
        return self.alloc(1)[0][0]

    def add_file(self, fid, name, records, atype=0, level=None, ridxofs=384, gap=False, ftype=0x1007):
        """records: (type, sub, bytes) or ("link", target fid, attrs)"""
        hb = self.header_block(fid)
        self.fid_blk[fid] = hb
        idx = []                     # the 16-byte record indexes, in order
        cur, pos = None, 0
        total = 0
        nlink = 0
        frags = []
        for r in records:
            if r[0] == "link":
                _, target, attrs = r
                attrs = list(attrs) + [0] * (5 - len(attrs))
                idx.append(b"\0\x80" + self.h(0) + self.h(target) + b"".join(self.h(a) for a in attrs))
                self.refs[target] = self.refs.get(target, 0) + 1
                nlink += 1
                continue
            rt, sub, data = r
            size = len(data)
            total += size
            if size == 0:
                idx.append(b"\0" + bytes([0x80 | rt]) + self.h(sub) + self.h(0) + self.h(0) + self.w(0) + b"\0" + self.a24(0))
                continue
            if cur is not None and pos + 4 + size <= self.bsize:
                off = pos + 4                    # after two separator codes of 0
                runs = [[cur, 1]]
                pos = off + size
            else:
                n = (size + self.bsize - 1) // self.bsize
                off = 0
                runs = self.alloc(n, gap)
                last = runs[-1]
                cur = last[0] + last[1] - 1
                pos = size - (n - 1) * self.bsize
                if pos == self.bsize:
                    cur, pos = None, 0
            # the bytes, run by run
            at, left, start = 0, size, off
            for b0, cnt in runs:
                n = min(cnt * self.bsize - start, left)
                self.put(b0, start, data[at:at + n])
                at += n
                left -= n
                start = 0
            first, more = runs[0], runs[1:]
            idx.append(b"\0" + bytes([0x80 | rt]) + self.h(sub) + self.h(0) + self.h(off) + self.w(size)
                       + bytes([first[1]]) + self.a24(first[0]))
            for i in range(0, len(more), 4):
                part = more[i:i + 4]
                e = b"".join(bytes([c]) + self.a24(b) for b, c in part)
                idx.append(e + bytes(16 - len(e)))
            if cur is not None:
                # the free space after the last record of the block: its separator, its size
                free = self.bsize - pos
                if free >= 4:
                    self.put(cur, pos, self.h(free - 2))
                    frags = [(free, cur, pos + 2)]
        # the header block: index level 0, or through index blocks
        hdr_cap = (self.bsize - ridxofs) // 16
        if level is None:
            level = 0 if len(idx) <= hdr_cap else 1
        per = self.bsize // 16
        blocks_index = []
        if level == 0:
            assert len(idx) <= hdr_cap
            area = b"".join(idx)
        else:
            # the index blocks: a connection index never left alone at a block's start
            chunks, i = [], 0
            while i < len(idx):
                j = min(i + per, len(idx))
                while j < len(idx) and idx[j][0] != 0 and j - i > 1:
                    j -= 1
                chunks.append(idx[i:j])
                i = j
            ents = []
            for ch in chunks:
                b = self.alloc(1)[0][0]
                blocks_index.append(b)
                self.put(b, 0, b"".join(ch))
                nrec = sum(1 for e in ch if e[0] == 0 and e[1] != 0)
                ents.append((nrec, b))
            if level == 1:
                area = b"".join(self.w(n) + self.w(b) for n, b in ents)
            else:
                per8 = self.bsize // 8
                area = b""
                for i in range(0, len(ents), per8):
                    part = ents[i:i + per8]
                    b = self.alloc(1)[0][0]
                    blocks_index.append(b)
                    self.put(b, 0, b"".join(self.w(n) + self.w(bb) for n, bb in part))
                    area += self.w(sum(n for n, _ in part)) + self.w(b)
            assert len(area) <= self.bsize - ridxofs
        nblk = 1 + len(blocks_index)
        nrec = sum(1 for e in idx if e[0] == 0 and e[1] != 0)
        fh = (self.w(0x54726F6E) + self.h(ftype) + self.h(atype) + bytes(28) + bytes(28) + self.h(0) + self.h(0)
              + self.h(nlink) + self.h(level) + self.w(total) + self.w(nblk) + self.w(nrec) + bytes(8)
              + self.w(0xFFFFFFFF) + self.w(STIME0 + 100 + fid) + self.w(STIME0 + 50 + fid) + self.w(STIME0 + fid)
              + self.tc(name) + bytes(24) + self.w(0) + self.h(ridxofs) + bytes(8) + self.h(fid) + self.w(0x82DDE96B))
        assert len(fh) == 192
        self.put(hb, 0, fh)
        ft = b"".join(self.h(sz) + self.h(off) + self.w(b) for sz, b, off in frags)
        self.put(hb, 192, ft[:ridxofs - 192])
        self.put(hb, ridxofs, area)
        self.hashes[fid] = self.short_name(name)
        self.files[fid] = {"name": name, "level": level, "records": len(records)}

    def add_link_file(self, fid, name, target_fid, fsname):
        """A link file: its header and, where the fragment table would be, where its target is"""
        hb = self.alloc(1)[0][0]
        self.fid_blk[fid] = hb
        fh = (self.w(0x54726F6E) + self.h(0x0000) + self.h(0) + bytes(56) + self.h(0) + self.h(0)
              + self.h(0) + self.h(0) + self.w(0) + self.w(1) + self.w(0) + bytes(8)
              + self.w(0xFFFFFFFF) + self.w(STIME0) + self.w(STIME0) + self.w(STIME0)
              + self.tc(name) + bytes(24) + self.w(0) + bytes(10) + self.h(fid) + self.w(0x82DDE96B))
        assert len(fh) == 192
        loc = self.h(target_fid) + self.w(STIME0) + bytes(4) + self.tc(fsname) + self.tc("ＣＤ－ＲＯＭ")
        assert len(loc) == 90
        self.put(hb, 0, fh + loc)
        self.hashes[fid] = self.short_name(name)
        self.files[fid] = {"name": name, "link_file": True}

    def short_name(self, name):
        raw = self.tc(name)
        v = 0
        for i in range(10):
            v ^= struct.unpack(">I", raw[4 * i:4 * i + 4])[0]
            v = ((v >> 1) | ((v & 1) << 31)) & 0xFFFFFFFF
        return v

    def finish(self, fmt=0x6402):
        bs = self.bsize
        # the file ID table and the short names
        for fid, hb in self.fid_blk.items():
            ref = 1 if fid == 0 else min(self.refs.get(fid, 0), 255)
            self.put(1 + self.ssys, fid * 4, bytes([ref]) + self.a24(hb))
            self.put(1 + self.ssys + self.sfidt, fid * 4, self.w(self.hashes[fid]))
        # the bitmap of the blocks used: the low bit first in the semi-standard form
        bm = bytearray(self.nbmp_words * 4)
        for b, u in enumerate(self.used):
            if u:
                bm[b // 8] |= (1 << (b % 8)) if self.le else (0x80 >> (b % 8))
        nfree = self.used.count(False)
        did = (0xFE, 0x52) if self.le else (0x52, 0xFE)
        fm = struct.pack(self.e + "H", fmt)
        sh = (bytes(did) + fm + self.h(self.ssys) + self.h(self.sfidt) + self.h(self.sfnmt) + self.h(self.nbmp_words)
              + bytes(12) + self.h(bs) + self.h(self.nfmax) + self.h(0x0021) + self.h(0)
              + self.w(self.nlb) + self.w(nfree) + self.w(STIME0 + 999) + self.w(STIME0)
              + self.tc(self.name) + self.tc(self.locat))
        assert len(sh) == 128
        self.put(1, 0, sh + bytes(bm))
        return bytes(self.img)


def build_fs(bsize, le):
    global ORDER
    ORDER = "<" if le else ">"
    fs = Fs(bsize=bsize, le=le)
    F_INTRO, F_FIG, F_DOCS, F_BIG, F_DEEP, F_CARD, F_LINKX, F_LINKS = 1, 2, 3, 4, 5, 6, 7, 8
    root = figure_tad(vobj(10, 10, 200, 34), vobj(10, 40, 200, 64), vobj(10, 70, 200, 94),
                      vobj(10, 100, 200, 124), vobj(10, 130, 200, 154), vobj(10, 160, 200, 184))
    fs.add_file(0, fs.name, [(1, 0, root), ("link", F_INTRO, [0]), ("link", F_FIG, [0]), ("link", F_DOCS, [0]),
                             ("link", F_BIG, [0]), ("link", F_DEEP, [0]), ("link", F_CARD, [0])])
    intro = text_tad("ＢＴＲＯＮのＣＤ－ＲＯＭの試験です。\n", "図はこちら：", vobj(0, 0, 160, 24), "\n終わり。")
    fs.add_file(F_INTRO, "はじめに", [(1, 0, intro), ("link", F_FIG, [0])], atype=1)
    fig = figure_tad(rect(10, 20, 110, 80), rect(150, 40, 250, 140), vobj(20, 200, 200, 224))
    fs.add_file(F_FIG, "図１", [(1, 0, fig), ("link", F_INTRO, [0]),
                               (8, 0, exec_rec(0x0002, "基本図形編集", "図形", (30, 40, 630, 440)))], atype=2)
    docs = figure_tad(vobj(10, 10, 200, 34), vobj(10, 40, 200, 64), vobj(10, 70, 200, 94),
                      vobj(10, 100, 200, 124), vobj(10, 130, 200, 154))
    fs.add_file(F_DOCS, "資料", [(1, 0, docs), ("link", F_INTRO, [0]), ("link", F_BIG, [0]), ("link", F_DEEP, [0]),
                               ("link", F_LINKX, [0]), ("link", F_LINKS, [0]),
                               (8, 0, exec_rec(0x0001, "仮身一覧", "仮身一覧", (60, 60, 560, 460)))], atype=6)
    line = "あいうえおかきくけこさしすせそたちつてと"          # 20 characters a line, then a new line
    big = text_tad(*([line + "\n"] * 450))                   # 9450 characters
    extra = [(5, 7, ("未知%03d" % i).encode("utf-8") + bytes([i % 256]) * 9) for i in range(130)]
    fs.add_file(F_BIG, "大きな実身", [(1, 0, big)] + extra, atype=1, gap=True)
    deep = text_tad("段を重ねた索引の実身。")
    fs.add_file(F_DEEP, "多段の実身", [(1, 0, deep), (5, 3, b"KEEP-ME-AS-IT-IS")], atype=1, level=2, ridxofs=256)
    # マイクロカード実身's records are of B-right/V: the low byte first in either form
    fmt_rec = struct.pack("<4H", 0x2343, 0x2352, 0x2344, 0x0100) + bytes(56)
    fs.add_file(F_CARD, "住所カード", [(31, 1, fmt_rec), (31, 2, b"\x01\x00" + bytes(30)), ("link", F_INTRO, [0])],
                atype=1)
    fs.add_link_file(F_LINKX, "よそのディスク", 9, "ほかの区画")
    fs.add_link_file(F_LINKS, "図１への連結", F_FIG, fs.name)
    img = fs.finish()
    want = {
        "fsname": fs.name, "bsize": bsize, "le": le,
        "files": {str(k): v for k, v in fs.files.items()},
        "big_chars": len(line) * 450, "big_extra": len(extra),
    }
    return img, want


def build_many(n, bsize=2048, le=True):
    """A root of one 仮身 to a cabinet of n 仮身, each to a text of its own: n + 2 files"""
    global ORDER
    ORDER = "<" if le else ">"
    fs = Fs(bsize=bsize, le=le, nfmax=n + 16, nlb=2 * n + 64, name="多数のＣＤ")
    fs.add_file(0, fs.name, [(1, 0, figure_tad(vobj(10, 10, 200, 34))), ("link", 1, [0])])
    fs.add_file(1, "多数の実身", [(1, 0, figure_tad(*[vobj(10, 10 + 30 * i, 200, 34 + 30 * i) for i in range(n)],
                                                 h=30 * n + 40))]
                + [("link", 2 + i, [0]) for i in range(n)], atype=6)
    for i in range(n):
        fs.add_file(2 + i, "文章%d" % (i + 1), [(1, 0, text_tad("文章%d番の中身。" % (i + 1)))], atype=1)
    img = fs.finish()
    want = {"fsname": fs.name, "bsize": bsize, "le": le, "files": {str(k): v for k, v in fs.files.items()}}
    return img, want


# ---------------------------------------------------------------- ISO 9660

def both16(v):
    return struct.pack("<H", v) + struct.pack(">H", v)


def both32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


def dir_rec(name, extent, size, isdir):
    nm = name if isinstance(name, bytes) else name.encode("ascii")
    ln = 33 + len(nm)
    ln += ln & 1
    r = bytes([ln, 0]) + both32(extent) + both32(size) + bytes([121, 1, 1, 0, 0, 0, 0])
    r += bytes([2 if isdir else 0, 0, 0]) + both16(1) + bytes([len(nm)]) + nm
    return r + bytes(ln - len(r))


def pad(s, n):
    s = s.encode("ascii") if isinstance(s, str) else s
    return s + b" " * (n - len(s))


def joliet(s):
    return s.encode("utf-16-be")


# The files of the ISO 9660 side beside the partition: their primary names, their Joliet names, the bytes
ISO_README = b"This disc holds a BTRON partition and these files.\r\n"
ISO_MEMO = "ISO 9660 の側のメモです。\n".encode("utf-8")


def build_iso(part, bootable=False):
    B = 2048
    # 16 PVD, 17 VPD, 18 Joliet's SVD, 19 the end, 20 and 21 the path tables,
    # 22 and 23 the primary's root and DOCS, 24 and 25 Joliet's, 26 and 27 the two files
    ROOT, DOCS, JROOT, JDOCS, README, MEMO, IMG = 22, 23, 24, 25, 26, 27, 28
    nimg = (len(part) + B - 1) // B
    total = IMG + nimg
    img = ("HDA2.IMG;1", IMG, len(part))
    root = (dir_rec(b"\0", ROOT, B, True) + dir_rec(b"\1", ROOT, B, True) + dir_rec("DOCS", DOCS, B, True)
            + dir_rec(img[0], IMG, len(part), False) + dir_rec("README.TXT;1", README, len(ISO_README), False))
    docs = dir_rec(b"\0", DOCS, B, True) + dir_rec(b"\1", ROOT, B, True) + dir_rec("MEMO.TXT;1", MEMO, len(ISO_MEMO), False)
    jroot = (dir_rec(b"\0", JROOT, B, True) + dir_rec(b"\1", JROOT, B, True) + dir_rec(joliet("文書"), JDOCS, B, True)
             + dir_rec(joliet("HDA2.IMG;1"), IMG, len(part), False)
             + dir_rec(joliet("はじめにお読みください.txt;1"), README, len(ISO_README), False))
    jdocs = (dir_rec(b"\0", JDOCS, B, True) + dir_rec(b"\1", JROOT, B, True)
             + dir_rec(joliet("メモ.txt;1"), MEMO, len(ISO_MEMO), False))

    def vd(kind, root_rec, esc=b""):
        d = bytes([kind]) + b"CD001" + bytes([1, 0]) + pad("TESSRONOS", 32) + pad("BTRONCD", 32) + bytes(8)
        d += both32(total) + esc + bytes(32 - len(esc)) + both16(1) + both16(1) + both16(B) + both32(10)
        d += struct.pack("<I", 20) + struct.pack("<I", 0) + struct.pack(">I", 21) + struct.pack(">I", 0)
        d += root_rec
        d += pad("", 128) * 4 + pad("", 37) * 3 + (b"2021010100000000\0") * 2 + (b"0" * 16 + b"\0") * 2 + bytes([1, 0])
        return d + bytes(B - len(d))
    pvd = vd(1, dir_rec(b"\0", ROOT, B, True))
    svd = vd(2, dir_rec(b"\0", JROOT, B, True), b"%/E")
    vpd = bytes([3]) + b"CD001" + bytes([1, 0])
    vpd += b"BTRON SPECIFIED FILE SYSTEM" + bytes(5)
    vpd += (b"BOOTABLE VOLUME" + bytes(17)) if bootable else bytes(32)
    vpd += both32(IMG) + both32(nimg)
    vpd += bytes(B - len(vpd))
    term = bytes([255]) + b"CD001" + bytes([1]) + bytes(B - 7)
    lpt = bytes([1, 0]) + struct.pack("<I", ROOT) + struct.pack("<H", 1) + b"\0\0"
    mpt = bytes([1, 0]) + struct.pack(">I", ROOT) + struct.pack(">H", 1) + b"\0\0"
    out = bytearray(B * 16) + pvd + vpd + svd + term
    for blk in (lpt, mpt, root, docs, jroot, jdocs, ISO_README, ISO_MEMO):
        out += blk + bytes(B - len(blk))
    assert len(out) == IMG * B
    out += part + bytes(nimg * B - len(part))
    return bytes(out)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("out")
    p.add_argument("--part", help="the file system's image alone, too")
    p.add_argument("--block", type=int, default=2048)
    p.add_argument("--be", action="store_true", help="the standard form: the high byte first")
    p.add_argument("--json", action="store_true", help="what the image holds, as OUT.json")
    p.add_argument("--many", type=int, default=0, help="a cabinet of so many texts instead")
    a = p.parse_args()
    part, want = build_many(a.many, a.block, not a.be) if a.many > 0 else build_fs(a.block, not a.be)
    iso = build_iso(part)
    with open(a.out, "wb") as f:
        f.write(iso)
    if a.part:
        with open(a.part, "wb") as f:
            f.write(part)
    if a.json:
        want["iso_bytes"] = len(iso)
        want["part_offset"] = 28 * 2048
        with open(a.out + ".json", "w", encoding="utf-8") as f:
            json.dump(want, f, ensure_ascii=False, indent=1)
    return 0


if __name__ == "__main__":
    sys.exit(main())
