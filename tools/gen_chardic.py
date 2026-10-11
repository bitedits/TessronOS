#!/usr/bin/env python3
"""The dictionary of 文字検索 (design 11.19), made when the system is built.

  tools/gen_chardic.py fetch DIR
      Downloads the published data the dictionary is made from into DIR
      (the default build takes it from ~/chardic): the Unihan database,
      KANJIDIC2, KRADFILE and KanjiVG. Nothing is changed.

  tools/gen_chardic.py make DIR OUT [--font FONT]
      Writes the dictionary, record 1 of the dictionary object
      (SYSDEF_DICT_CHAR), to OUT. A source missing from DIR is left out
      and the header says which were used; with none at all the
      dictionary still has every character with its codes, its Unicode
      name and its class, made from Python's own tables. FONT is the
      system's face: the characters it has no glyph for are marked, so
      that the accessory writes their code instead of an empty box.

What is in it, and from where:

  - the characters: every place of TRON code's first plane (JIS X 0208,
    JIS X 0213, JIS X 0212, GB 2312, KS X 1001, by Python's codecs, as
    tools/gen_jis.py lays them out) and the rest of the Unicode BMP
    apart from the ideographs and the Hangul syllables, which TRON code
    puts in planes 16 and 17 (各国)
  - the Japanese readings, the stroke count, the radical and the grade
    of a kanji: KANJIDIC2 (EDRDG, CC BY-SA 4.0); for a kanji it does not
    have, the stroke count and the radical from Unihan (Unicode License)
  - the elements a kanji is made of: KanjiVG (CC BY-SA 3.0), which gives
    them as a tree and so how many times each occurs and which are the
    first level; KRADFILE (EDRDG, CC BY-SA 4.0) for the elements of the
    kanji KanjiVG does not have and the list of elements to choose from
  - the variants of a kanji (異体字, simplified, traditional, old and new
    forms): Unihan
  - the Big5 code: Python's codec
  - the names of the other characters, their class and script: Python's
    unicodedata (the Unicode Character Database), with Japanese words
    for the commonest of them written here

The dictionary holds data of KANJIDIC2, KRADFILE and KanjiVG, so the
dictionary object is distributed under CC BY-SA 4.0 with those works
named (THIRD_PARTY_NOTICES.md); this script and the accessory are not.

The format (little-endian), read by application/charsearch/cs_dic.c:

  header, 64 bytes
     0  "TSCHDIC1"
     8  UW  entries            12  UW  where the entries start
    16  UW  where the data starts   20  UW  its size
    24  UW  where the elements start (the list to choose from)
    28  UW  how many                32  UW  where JIS X 0208's places start
    36  UW  the sources used (CD_SRC_*)
  an entry, 20 bytes, in the order of the code points:
     0  UW  the code point     4  UW  its data, from where the data starts
     8  UH  its place in the order results are shown in
    10  UH  CD_F_*            12  UB  strokes    13  UB  radical (1..214)
    14  UB  grade             15  UB  script (CD_SC_*)   16  UB  class (CD_CL_*)
    17  UB  the sets that have it (CD_M_*)   18..19  0
  its data: fields of a tag (UB), a length (UH) and that many bytes,
  ending with tag 0
  an element of the list, 8 bytes: UW the element, UH the character
  shown for it, UB strokes, UB 0
  JIS X 0208's places: 94 * 94 bits, row by row, set where JIS X 0208
  has a character (the others of zone A are JIS X 0213's)
"""

import gzip
import io
import os
import re
import struct
import subprocess
import sys
import unicodedata
import zipfile
import xml.etree.ElementTree as ET

SOURCES = {
    "Unihan.zip": "https://www.unicode.org/Public/UCD/latest/ucd/Unihan.zip",
    "kanjidic2.xml.gz": "http://www.edrdg.org/kanjidic/kanjidic2.xml.gz",
    "kradzip.zip": "http://ftp.edrdg.org/pub/Nihongo/kradzip.zip",
    "kanjivg.xml.gz": "https://github.com/KanjiVG/kanjivg/releases/download/r20260714/kanjivg-20260714.xml.gz",
}

# header.sources
SRC_KANJIDIC, SRC_KRAD, SRC_KANJIVG, SRC_UNIHAN, SRC_FONT = 1, 2, 4, 8, 16

# entry.flags
F_KANJI = 0x0001        # an ideograph
F_JOYO = 0x0002         # 常用漢字 (2010)
F_JINMEI = 0x0004       # 人名用漢字
F_GLYPH = 0x0008        # the system's face has it
F_ELEM = 0x0010         # an element of some other kanji
F_HANGUL = 0x0020

# entry.sets
M_X0208, M_X0213_1, M_X0213_2, M_X0212, M_GB, M_KS = 1, 2, 4, 8, 16, 32

# the data's tags
T_ON, T_KUN, T_ELEMS, T_TOP, T_VARS, T_NAME, T_KEYS, T_BIG5, T_WORDS = 1, 2, 3, 4, 5, 6, 7, 8, 9

# the kinds of a variant (T_VARS: kind << 24 | code point)
V_ITAI, V_SIMP, V_TRAD, V_OLD, V_NEW, V_UPPER, V_LOWER, V_REL = 1, 2, 3, 4, 5, 6, 7, 8

# scripts (字種 of 記号検索)
SC_NONE, SC_LATIN, SC_CYRIL, SC_GREEK, SC_ARABIC, SC_HEBREW, SC_INDIC, SC_THAI, SC_HANGUL, \
    SC_CHINA, SC_JAPAN, SC_OTHER = range(12)
# classes (分類)
CL_NONE, CL_CASE, CL_LETTER, CL_DIGIT, CL_MARK, CL_PHON, CL_SHAPE, CL_UNIT = range(8)

X0213_P2_ROWS = [1, 3, 4, 5, 8, 12, 13, 14, 15] + list(range(78, 95))

# KRADFILE writes an element that JIS X 0208 does not have as a kanji that
# contains it; these are the elements themselves (KRADFILE's own list)
KRAD_FORMS = {
    "化": "亻", "个": "\U000201A2", "并": "丷", "刈": "刂", "込": "辶", "尚": "⺌",
    "忙": "忄", "扎": "扌", "汁": "氵", "犯": "犭", "艾": "艹", "邦": "阝", "阡": "阝",
    "老": "耂", "杰": "灬", "礼": "礻", "疔": "疒", "禹": "禸", "初": "衤", "買": "罒",
    "滴": "啇", "乞": "\U00020089", "ノ": "丿", "ハ": "八", "｜": "丨",
}

# Japanese words for the names of the characters most looked for: a word,
# and the English names (words all in the name; | between choices)
JA_WORDS = [
    ("ハート", "HEART"), ("はーと", "HEART"), ("ほし 星", "STAR"), ("まる 丸", "CIRCLE|CIRCLED"),
    ("にじゅうまる 二重丸", "BULLSEYE|DOUBLE CIRCLED"), ("しかく 四角", "SQUARE|SQUARED"),
    ("さんかく 三角", "TRIANGLE"), ("ひしがた 菱形", "DIAMOND|LOZENGE"), ("やじるし 矢印", "ARROW"),
    ("うえ 上", "UP|UPWARDS"), ("した 下", "DOWN|DOWNWARDS"), ("ひだり 左", "LEFT|LEFTWARDS"),
    ("みぎ 右", "RIGHT|RIGHTWARDS"), ("くろ 黒", "BLACK"), ("しろ 白", "WHITE"),
    ("かっこ 括弧", "BRACKET|PARENTHESIS|PARENTHESIZED"), ("おんぷ 音符", "NOTE|NOTES"),
    ("すうじ 数字", "DIGIT|NUMBER|NUMERAL"), ("ろーま ローマ", "ROMAN"), ("ちるど チルド", "TILDE"),
    ("あくせんと アクセント", "ACUTE|GRAVE|CIRCUMFLEX"), ("うむらうと ウムラウト", "DIAERESIS"),
    ("せでぃーゆ セディーユ", "CEDILLA"), ("はんぐる ハングル", "HANGUL"), ("ひらがな 平仮名", "HIRAGANA"),
    ("かたかな 片仮名", "KATAKANA"), ("ぎりしゃ ギリシャ ぎりしあ ギリシア", "GREEK"),
    ("きりる キリル", "CYRILLIC"), ("らてん ラテン", "LATIN"), ("あらびあ アラビア", "ARABIC"),
    ("へぶらい ヘブライ", "HEBREW"), ("たい タイ", "THAI"), ("けいせん 罫線", "BOX DRAWINGS"),
    ("てん 点", "DOT|DOTS"), ("せん 線", "LINE"), ("ばつ", "SALTIRE|MULTIPLICATION X"),
    ("こめじるし 米印", "REFERENCE MARK"), ("ゆうびん 郵便", "POSTAL"), ("でんわ 電話", "TELEPHONE"),
    ("たいよう 太陽", "SUN"), ("つき 月", "MOON"), ("くも 雲", "CLOUD"), ("かさ 傘", "UMBRELLA"),
    ("ゆき 雪", "SNOWMAN|SNOWFLAKE"), ("はな 花", "FLORAL|FLOWER"), ("ゆび 指", "INDEX|FINGER"),
    ("かお 顔", "FACE"), ("すぺーど スペード", "SPADE"), ("だいや ダイヤ", "DIAMOND"),
    ("くらぶ クラブ", "CLUB"), ("えん 円", "YEN"), ("どる ドル", "DOLLAR"), ("ゆーろ ユーロ", "EURO"),
    ("ぽんど ポンド", "POUND"), ("ど 度", "DEGREE"), ("ぱーせんと パーセント", "PERCENT"),
    ("ぷらす プラス", "PLUS"), ("まいなす マイナス", "MINUS"), ("かける", "MULTIPLICATION"),
    ("わる", "DIVISION"), ("いこーる イコール", "EQUALS"), ("るーと ルート", "SQUARE ROOT"),
    ("むげんだい 無限大", "INFINITY"), ("てんじ 点字", "BRAILLE"), ("おんせん 温泉", "HOT SPRINGS"),
    ("ちぇす チェス", "CHESS"), ("しょうぎ 将棋", "SHOGI"), ("ほし", "ASTERISK"),
    ("こてん 句点", "FULL STOP"), ("とうてん 読点", "COMMA"), ("ちょうおん 長音", "PROLONGED SOUND"),
    ("だくてん 濁点", "VOICED SOUND"), ("でんき 電気", "ELECTRIC"), ("とけい 時計", "CLOCK|WATCH"),
    ("かみなり 雷", "LIGHTNING|HIGH VOLTAGE"), ("ほうがく 方角", "COMPASS"),
]

# Scripts by the first words of a name
SCRIPT_WORDS = [
    (SC_LATIN, "LATIN"), (SC_CYRIL, "CYRILLIC ARMENIAN GEORGIAN"), (SC_GREEK, "GREEK COPTIC"),
    (SC_ARABIC, "ARABIC SYRIAC THAANA"), (SC_HEBREW, "HEBREW"),
    (SC_INDIC, "DEVANAGARI BENGALI GURMUKHI GUJARATI ORIYA TAMIL TELUGU KANNADA MALAYALAM SINHALA VEDIC"),
    (SC_THAI, "THAI LAO"), (SC_HANGUL, "HANGUL"),
    (SC_CHINA, "TIBETAN BOPOMOFO CJK IDEOGRAPHIC KANGXI YI"),
    (SC_JAPAN, "HIRAGANA KATAKANA"),
    (SC_OTHER, "ROMAN BRAILLE"),
]

NUMBERS = {"ZERO": "0", "ONE": "1", "TWO": "2", "THREE": "3", "FOUR": "4", "FIVE": "5", "SIX": "6",
           "SEVEN": "7", "EIGHT": "8", "NINE": "9", "TEN": "10", "ELEVEN": "11", "TWELVE": "12",
           "THIRTEEN": "13", "FOURTEEN": "14", "FIFTEEN": "15", "SIXTEEN": "16", "SEVENTEEN": "17",
           "EIGHTEEN": "18", "NINETEEN": "19", "TWENTY": "20", "THIRTY": "30", "FORTY": "40", "FIFTY": "50"}


def say(*a):
    print("gen_chardic:", *a, file=sys.stderr)


def fetch(d):
    os.makedirs(d, exist_ok=True)
    for name, url in SOURCES.items():
        path = os.path.join(d, name)
        if os.path.exists(path) and os.path.getsize(path) > 0:
            say("have", name)
            continue
        say("fetching", url)
        subprocess.check_call(["curl", "-sSfL", "-o", path + ".part", url])
        os.replace(path + ".part", path)


def dec(b, codec):
    try:
        return bytes(b).decode(codec)
    except UnicodeDecodeError:
        return None


def one(s):
    """The code point of a string of one character, else None"""
    return ord(s) if s is not None and len(s) == 1 else None


# ---------------------------------------------------------------- the characters of TRON code's first plane

def first_plane():
    """Every character of plane 1, and JIS X 0208's places"""
    chars = {}
    x0208 = bytearray(94 * 94 // 8 + 1)

    def add(c, m):
        chars[c] = chars.get(c, 0) | m
    for row in range(1, 95):
        for cell in range(1, 95):
            s = dec([row + 0xA0, cell + 0xA0], "euc_jp")
            m = M_X0208
            if s is not None:
                i = (row - 1) * 94 + cell - 1
                x0208[i >> 3] |= 1 << (i & 7)
            else:
                s = dec([row + 0xA0, cell + 0xA0], "euc_jis_2004")
                m = M_X0213_1
            c = one(s)
            if c is not None:
                add(c, m)
            elif s:
                for x in s:
                    if unicodedata.combining(x) == 0:
                        add(ord(x), m)
    for i, row in enumerate(X0213_P2_ROWS):
        for cell in range(1, 95):
            c = one(dec([0x8F, row + 0xA0, cell + 0xA0], "euc_jis_2004"))
            if c is not None:
                add(c, M_X0213_2)
    for row in range(1, 78):
        for cell in range(1, 95):
            c = one(dec([0x8F, row + 0xA0, cell + 0xA0], "euc_jp"))
            if c is not None:
                add(c, M_X0212)
    for codec, m in (("gb2312", M_GB), ("euc_kr", M_KS)):
        for row in range(1, 95):
            for cell in range(1, 95):
                c = one(dec([row + 0xA0, cell + 0xA0], codec))
                if c is not None:
                    add(c, m)
    return chars, bytes(x0208[:(94 * 94 + 7) // 8])


def is_han(c):
    return (0x3400 <= c <= 0x4DBF or 0x4E00 <= c <= 0x9FFF or 0xF900 <= c <= 0xFAFF
            or 0x20000 <= c <= 0x3134F or c == 0x3005 or c == 0x3007)


def bmp_others():
    """The BMP's characters TRON code puts in planes 16 and 17 (各国)"""
    out = set()
    for c in range(0x20, 0x10000):
        if 0xD800 <= c <= 0xF8FF or is_han(c) or 0xAC00 <= c <= 0xD7A3:
            continue
        cat = unicodedata.category(chr(c))
        if cat in ("Cn", "Cc", "Cs", "Co"):
            continue
        out.add(c)
    return out


# ---------------------------------------------------------------- the sources

def read_unihan(d):
    path = os.path.join(d, "Unihan.zip")
    want = {"kTotalStrokes", "kRSUnicode", "kSemanticVariant", "kZVariant", "kSpecializedSemanticVariant",
            "kSimplifiedVariant", "kTraditionalVariant", "kJapaneseOldVariant", "kJapaneseNewVariant",
            "kJoyoKanji", "kJinmeiyoKanji"}
    u = {}
    if not os.path.exists(path):
        return None
    z = zipfile.ZipFile(path)
    for n in z.namelist():
        for line in z.read(n).decode("utf-8").splitlines():
            if not line.startswith("U+"):
                continue
            p = line.split("\t")
            if len(p) < 3 or p[1] not in want:
                continue
            u.setdefault(int(p[0][2:], 16), {})[p[1]] = p[2]
    return u


def cps_of(field):
    """The code points named in a field of Unihan (U+XXXX, perhaps with <sources)"""
    return [int(m, 16) for m in re.findall(r"U\+([0-9A-F]{4,6})", field or "")]


def kata_to_hira(s):
    return "".join(chr(ord(ch) - 0x60) if 0x30A1 <= ord(ch) <= 0x30F6 else ch for ch in s)


def read_kanjidic(d):
    path = os.path.join(d, "kanjidic2.xml.gz")
    if not os.path.exists(path):
        return None
    out = {}
    for _, el in ET.iterparse(gzip.open(path), events=("end",)):
        if el.tag != "character":
            continue
        lit = el.findtext("literal")
        if lit is None or len(lit) != 1:
            el.clear()
            continue
        e = {"on": [], "kun": [], "strokes": 0, "rad": 0, "grade": 0}
        for r in el.iter("rad_value"):
            if r.get("rad_type") == "classical":
                e["rad"] = int(r.text)
        sc = el.find("misc/stroke_count")
        if sc is not None:
            e["strokes"] = int(sc.text)
        g = el.findtext("misc/grade")
        if g:
            e["grade"] = int(g)
        for r in el.iter("reading"):
            if r.get("r_type") == "ja_on":
                h = kata_to_hira(r.text)
                if h not in e["on"]:
                    e["on"].append(h)
            elif r.get("r_type") == "ja_kun":
                if r.text not in e["kun"]:
                    e["kun"].append(r.text)
        out[ord(lit)] = e
        el.clear()
    return out


def read_krad(d):
    path = os.path.join(d, "kradzip.zip")
    if not os.path.exists(path):
        return None, None
    z = zipfile.ZipFile(path)
    krad = {}
    for n in ("kradfile", "kradfile2"):
        if n not in z.namelist():
            continue
        for line in z.read(n).decode("euc_jp", "replace").splitlines():
            if line.startswith("#") or " : " not in line:
                continue
            k, e = line.split(" : ", 1)
            if len(k) != 1:
                continue
            krad[ord(k)] = [KRAD_FORMS.get(x, x) for x in e.split()]
    elems = []
    name = "radkfilex" if "radkfilex" in z.namelist() else "radkfile"
    for line in z.read(name).decode("euc_jp", "replace").splitlines():
        if line.startswith("$"):
            p = line.split()
            if len(p) >= 3 and len(p[1]) == 1:
                elems.append((KRAD_FORMS.get(p[1], p[1]), p[1], int(p[2])))
    return krad, elems


KVG = "{http://kanjivg.tagaini.net}"


def read_kanjivg(d):
    """Each kanji's elements, how many times each occurs, and those of its first level"""
    path = os.path.join(d, "kanjivg.xml.gz")
    if not os.path.exists(path):
        return None
    out = {}

    def walk(g, counts):
        for c in g:
            if c.tag != "g":
                continue
            el = c.get(KVG + "element")
            part = c.get(KVG + "part")
            if el is not None and len(el) == 1 and (part is None or part == "1"):
                counts[el] = counts.get(el, 0) + 1
                orig = c.get(KVG + "original")
                if orig is not None and len(orig) == 1 and orig != el:
                    counts[orig] = counts.get(orig, 0) + 1
            walk(c, counts)

    def top(g, out_list):
        for c in g:
            if c.tag != "g":
                continue
            el = c.get(KVG + "element")
            part = c.get(KVG + "part")
            if el is not None and len(el) == 1:
                if part is None or part == "1":
                    out_list.append(el)
            else:
                top(c, out_list)

    for _, el in ET.iterparse(gzip.open(path), events=("end",)):
        if el.tag != "kanji":
            continue
        m = re.match(r"kvg:kanji_([0-9a-f]+)$", el.get("id", ""))
        root = el.find("g")
        if m is None or root is None or "-" in m.group(1):
            el.clear()
            continue
        cp = int(m.group(1), 16)
        counts = {}
        walk(root, counts)
        counts.pop(chr(cp), None)
        tl = []
        top(root, tl)
        if len(tl) == 1 and tl[0] == chr(cp):
            tl = []
        out[cp] = (counts, tl)
        el.clear()
    return out


def font_cmap(path):
    if not path or not os.path.exists(path):
        return None
    try:
        from fontTools.ttLib import TTFont
    except ImportError:
        say("no fontTools: the characters the face lacks are not marked")
        return None
    f = TTFont(path, lazy=True)
    return set(f.getBestCmap().keys())


# ---------------------------------------------------------------- the characters' classes

def script_of(c, name):
    if is_han(c):
        return SC_CHINA
    if 0xAC00 <= c <= 0xD7A3:
        return SC_HANGUL
    if 0x0250 <= c <= 0x02AF:
        return SC_LATIN
    words = name.split()
    for sc, ws in SCRIPT_WORDS:
        for w in ws.split():
            if w in words[:2]:
                return sc
    if 0x3000 <= c <= 0x30FF:
        return SC_JAPAN
    return SC_OTHER


def class_of(c, name):
    cat = unicodedata.category(chr(c))
    if 0x3300 <= c <= 0x33FF or 0x2100 <= c <= 0x214F and cat == "So" or cat == "Sc" \
            or "DEGREE" in name.split():
        return CL_UNIT
    if cat in ("Lu", "Ll", "Lt") or 0x0250 <= c <= 0x02AF:
        return CL_CASE
    if cat[0] == "N" or any(w in name.split() for w in ("DIGIT", "NUMBER", "NUMERAL", "FRACTION")):
        return CL_DIGIT
    if cat[0] == "M" or cat == "Sk" or 0x02B0 <= c <= 0x02FF or c in (0x309B, 0x309C, 0x30FC):
        return CL_PHON
    if cat in ("Lo", "Lm"):
        return CL_LETTER
    if cat == "So":
        return CL_SHAPE
    return CL_MARK


def ja_words(name):
    """The Japanese words a name is found by"""
    words = set(name.split())
    out = []
    for ja, en in JA_WORDS:
        for alt in en.split("|"):
            if all(w in words for w in alt.split()):
                out.extend(ja.split())
                break
    return out


def keys_of(c, name):
    """The parts of a symbol: what it decomposes to, and its frame and digits by its name"""
    ch = chr(c)
    k = []
    nf = unicodedata.normalize("NFKD", ch)
    if nf != ch:
        k.extend(x for x in nf if not x.isspace())
    words = name.split()
    if "DOUBLE" in words and "CIRCLED" in words:
        k.append("◎")
    elif "NEGATIVE" in words and "CIRCLED" in words:
        k.append("●")
    elif "CIRCLED" in words:
        k.append("○")
    if "PARENTHESIZED" in words:
        k.extend("()")
    if "SQUARED" in words:
        k.append("□")
    if ("DIGIT" in words or "NUMBER" in words) and len(words) > 1:
        for w in words[words.index("DIGIT" if "DIGIT" in words else "NUMBER") + 1:]:
            if w in NUMBERS:
                k.extend(NUMBERS[w])
    seen = []
    for x in k:
        if x not in seen and x != ch:
            seen.append(x)
    return "".join(seen)


# ---------------------------------------------------------------- writing it

def field(tag, data):
    return struct.pack("<BH", tag, len(data)) + data


def utf8(s):
    return s.encode("utf-8")


def make(d, out, font):
    plane1, x0208 = first_plane()
    others = bmp_others()
    uh = read_unihan(d)
    kd = read_kanjidic(d)
    krad, elems = read_krad(d)
    kvg = read_kanjivg(d)
    cmap = font_cmap(font)
    src = (SRC_KANJIDIC if kd else 0) | (SRC_KRAD if krad else 0) | (SRC_KANJIVG if kvg else 0) \
        | (SRC_UNIHAN if uh else 0) | (SRC_FONT if cmap else 0)
    for n, v in (("Unihan", uh), ("KANJIDIC2", kd), ("KRADFILE", krad), ("KanjiVG", kvg)):
        if not v:
            say("no", n, "in", d, "(tools/gen_chardic.py fetch", d + ")")
    uh = uh or {}
    kd = kd or {}
    krad = krad or {}
    kvg = kvg or {}
    elems = elems or []

    chars = sorted(set(plane1) | others | set(kd))
    have = set(chars)
    elem_of = set()

    # each kanji's elements: KanjiVG's counts, and KRADFILE's for what it lacks
    parts = {}
    for c in chars:
        if not is_han(c):
            continue
        counts = {}
        tl = []
        if c in kvg:
            counts = {ord(k): v for k, v in kvg[c][0].items()}
            tl = [ord(x) for x in kvg[c][1]]
        for x in krad.get(c, []):
            if len(x) == 1 and ord(x) != c and ord(x) not in counts:
                counts[ord(x)] = 1
        parts[c] = (counts, tl)
        elem_of.update(counts)

    # the order results are shown in: kanji by their first reading, then strokes; the rest by code
    def first_reading(c):
        e = kd.get(c)
        if e is None:
            return "￿"
        r = e["on"][:1] or [k.replace(".", "").replace("-", "") for k in e["kun"][:1]]
        return r[0] if r else "￿"

    def strokes_of(c):
        if c in kd and kd[c]["strokes"]:
            return kd[c]["strokes"]
        t = uh.get(c, {}).get("kTotalStrokes")
        return int(t.split()[0]) if t else 0

    order = sorted(chars, key=lambda c: (0, first_reading(c), strokes_of(c), c) if is_han(c)
                   else (1, "", 0, c))
    rank = {c: i for i, c in enumerate(order)}

    ents = []
    blob = bytearray()
    nread = nel = 0
    for c in chars:
        ch = chr(c)
        name = unicodedata.name(ch, "")
        flags = 0
        strokes = rad = grade = 0
        sc = script_of(c, name)
        cl = class_of(c, name) if not is_han(c) else CL_MARK
        data = bytearray()
        u = uh.get(c, {})
        if is_han(c):
            flags |= F_KANJI
            e = kd.get(c)
            if e is not None:
                strokes, rad, grade = e["strokes"], e["rad"], e["grade"]
                if e["on"]:
                    data += field(T_ON, utf8(" ".join(e["on"])))
                if e["kun"]:
                    data += field(T_KUN, utf8(" ".join(e["kun"])))
                if e["on"] or e["kun"]:
                    nread += 1
            if strokes == 0:
                strokes = strokes_of(c)
            if rad == 0 and u.get("kRSUnicode"):
                m = re.match(r"(\d+)", u["kRSUnicode"])
                rad = int(m.group(1)) if m else 0
            if "kJoyoKanji" in u or 1 <= grade <= 8:
                flags |= F_JOYO
            if "kJinmeiyoKanji" in u or grade in (9, 10):
                flags |= F_JINMEI
            counts, tl = parts.get(c, ({}, []))
            if counts:
                nel += 1
                data += field(T_ELEMS, b"".join(struct.pack("<I", (k << 8) | min(v, 255))
                                                  for k, v in sorted(counts.items())))
            if tl:
                data += field(T_TOP, b"".join(struct.pack("<I", k) for k in tl))
            vs = []
            for f, kind in (("kSemanticVariant", V_ITAI), ("kZVariant", V_ITAI),
                            ("kSpecializedSemanticVariant", V_ITAI), ("kSimplifiedVariant", V_SIMP),
                            ("kTraditionalVariant", V_TRAD), ("kJapaneseOldVariant", V_OLD),
                            ("kJapaneseNewVariant", V_NEW)):
                for v in cps_of(u.get(f)):
                    if v != c and v <= 0x10FFFF and (kind, v) not in vs:
                        vs.append((kind, v))
            if vs:
                data += field(T_VARS, b"".join(struct.pack("<I", (k << 24) | v) for k, v in vs[:24]))
        else:
            if 0xAC00 <= c <= 0xD7A3:
                flags |= F_HANGUL
            if name:
                data += field(T_NAME, name.encode("ascii"))
            w = ja_words(name)
            if 0x0250 <= c <= 0x02FF or 0x1D00 <= c <= 0x1DBF:
                w.append("IPA")
            if w:
                data += field(T_WORDS, utf8(" ".join(w)))
            k = keys_of(c, name)
            if k:
                data += field(T_KEYS, utf8(k))
            vs = []
            up, lo = ch.upper(), ch.lower()
            if len(up) == 1 and up != ch:
                vs.append((V_UPPER, ord(up)))
            if len(lo) == 1 and lo != ch:
                vs.append((V_LOWER, ord(lo)))
            nf = unicodedata.normalize("NFKC", ch)
            if len(nf) == 1 and nf != ch:
                vs.append((V_REL, ord(nf)))
            if vs:
                data += field(T_VARS, b"".join(struct.pack("<I", (k << 24) | v) for k, v in vs))
        b5 = None
        try:
            b5 = ch.encode("big5")
        except UnicodeEncodeError:
            pass
        if b5 is not None and len(b5) == 2:
            data += field(T_BIG5, b5)
        if c in elem_of:
            flags |= F_ELEM
        if cmap is not None and c in cmap:
            flags |= F_GLYPH
        data += b"\0"
        at = len(blob)
        blob += data
        ents.append(struct.pack("<IIHHBBBBBB2x", c, at, rank[c], flags, strokes, rad, grade, sc, cl,
                                plane1.get(c, 0)))

    el = bytearray()
    nelem = 0
    seen = set()
    for form, shown, n in elems:
        if form in seen or len(form) != 1:
            continue
        seen.add(form)
        disp = form if (cmap is None or ord(form) in cmap or len(shown) != 1) else shown
        el += struct.pack("<IHBx", ord(form), ord(disp) if ord(disp) <= 0xFFFF else ord(shown), n)
        nelem += 1

    head = 64
    ent_off = head
    blob_off = ent_off + len(ents) * 20
    elem_off = blob_off + len(blob)
    elem_off = (elem_off + 3) & ~3
    jis_off = elem_off + len(el)
    out_b = bytearray()
    out_b += b"TSCHDIC1" + struct.pack("<IIIIIIII", len(ents), ent_off, blob_off, len(blob),
                                         elem_off, nelem, jis_off, src)
    out_b += b"\0" * (head - len(out_b))
    out_b += b"".join(ents)
    out_b += blob
    out_b += b"\0" * (elem_off - len(out_b))
    out_b += el
    out_b += x0208
    with open(out, "wb") as f:
        f.write(out_b)
    say("%d characters (%d kanji with readings, %d with elements), %d elements to choose from, %d bytes"
        % (len(ents), nread, nel, nelem, len(out_b)))


def main():
    a = sys.argv[1:]
    if len(a) == 2 and a[0] == "fetch":
        fetch(a[1])
        return 0
    if len(a) >= 3 and a[0] == "make":
        font = None
        if len(a) == 5 and a[3] == "--font":
            font = a[4]
        make(a[1], a[2], font)
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main())
