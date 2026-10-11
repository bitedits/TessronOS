#!/usr/bin/env python3
"""The 電子手帳 objects of etc/xtad, written as the program keeps them.

  python tools/mktecho.py [ETC_XTAD]
  python tools/mktecho.py --table tests/ktest/ktest_techo_hol.h

A 電子手帳 object is a text (design 11.23): record 0 is an xmlTAD
document whose paragraphs are cut into three parts by tab formats --

  《タブ書式》 予定表 《タブ書式》 住所録 《タブ書式》 休日 《タブ書式》

予定表: "西暦/月/日<tab>予定", a paragraph for each plan.
住所録: よみ, 郵便番号, 住所1, 住所2, 住所3, 氏名, 電話, FAX, メモ1, メモ2
        cut by tabs, a paragraph for each person.
休日:   the weekdays that are holidays ("日"), then dates "西暦/月/日" where
        '*' is every year, month or day, the year may be "1990-1999" and
        the day "2月" (the second Monday). "祝日" makes the national
        holidays holidays, reckoned by the program from the law's rules.
        After a tab, a note.

This writes the template 手帳用紙 -- Sundays and "祝日" -- and the sample
私の手帳 with a few plans and people, on the same holidays.

With --table it writes instead, as a C header for the kernel's test, the
national holidays of 1990 to 2050 as lines of dates in the same form:
the rules as the law has had them, the days that came once, the days of
the equinoxes and the days off in lieu (振替休日) and between two holidays
(国民の休日) each as a date of its own. The test checks that the program's
own reckoning makes the same days.
"""

import datetime
import json
import os
import shutil
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
XTAD = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "etc", "xtad")
ICON = "019a0f91-066f-706f-93da-ffe8150d6207.ico"   # 画用紙's

TEMPLATE = "01a0d8c4-53c4-7e55-8f66-7a8b9cad53c4"
SAMPLE = "01a0d8c4-53c6-7a77-8b88-9cadbecf53c6"
FIRST, LAST = 1990, 2050
WD = "月火水木金土日"          # datetime's weekday(): Monday 0

# The national holidays that come by rule: (years, month, day or "nW", name)
RULES = [
    ("*", 1, "1", "元日"),
    ("1990-1999", 1, "15", "成人の日"),
    ("2000-2050", 1, "2月", "成人の日"),
    ("*", 2, "11", "建国記念の日"),
    ("2020-2050", 2, "23", "天皇誕生日"),
    ("1990-2006", 4, "29", "みどりの日"),
    ("2007-2050", 4, "29", "昭和の日"),
    ("*", 5, "3", "憲法記念日"),
    ("2007-2050", 5, "4", "みどりの日"),
    ("*", 5, "5", "こどもの日"),
    ("1996-2002", 7, "20", "海の日"),
    ("2003-2019", 7, "3月", "海の日"),
    ("2022-2050", 7, "3月", "海の日"),
    ("2016-2019", 8, "11", "山の日"),
    ("2022-2050", 8, "11", "山の日"),
    ("1990-2002", 9, "15", "敬老の日"),
    ("2003-2050", 9, "3月", "敬老の日"),
    ("1990-1999", 10, "10", "体育の日"),
    ("2000-2019", 10, "2月", "体育の日"),
    ("2022-2050", 10, "2月", "スポーツの日"),
    ("*", 11, "3", "文化の日"),
    ("*", 11, "23", "勤労感謝の日"),
    ("1990-2018", 12, "23", "天皇誕生日"),
]

# Days that came once
ONCE = [
    ("1990/11/12", "即位礼正殿の儀"),
    ("1993/6/9", "皇太子徳仁親王の結婚の儀"),
    ("2019/5/1", "天皇の即位の日"),
    ("2019/10/22", "即位礼正殿の儀"),
    ("2020/7/23", "海の日"),
    ("2020/7/24", "スポーツの日"),
    ("2020/8/10", "山の日"),
    ("2021/7/22", "海の日"),
    ("2021/7/23", "スポーツの日"),
    ("2021/8/8", "山の日"),
]


def equinox(y, spring):
    base = 20.8431 if spring else 23.2488
    return int(base + 0.242194 * (y - 1980) - int((y - 1980) / 4))


def rule_days(y):
    """The holidays of a year the rules and the single days give: {date: name}"""
    out = {}
    for years, m, d, name in RULES:
        if years != "*":
            a, b = (int(v) for v in years.split("-"))
            if not a <= y <= b:
                continue
        if d[-1] in WD:
            n, w = int(d[:-1]), WD.index(d[-1])
            first = datetime.date(y, m, 1)
            day = 1 + (w - first.weekday()) % 7 + 7 * (n - 1)
            out[datetime.date(y, m, day)] = name
        else:
            out[datetime.date(y, m, int(d))] = name
    out[datetime.date(y, 3, equinox(y, True))] = "春分の日"
    out[datetime.date(y, 9, equinox(y, False))] = "秋分の日"
    for s, name in ONCE:
        yy, mm, dd = (int(v) for v in s.split("/"))
        if yy == y:
            out[datetime.date(yy, mm, dd)] = name
    return out


def extra_days(y):
    """振替休日 and 国民の休日 of a year: {date: name}"""
    base = rule_days(y)
    out = {}
    for day in sorted(base):
        if day.weekday() != 6:
            continue
        nxt = day + datetime.timedelta(days=1)
        if y >= 2007:
            while nxt in base:
                nxt += datetime.timedelta(days=1)
        elif nxt in base:
            continue
        out[nxt] = "振替休日"
    for day in sorted(base):
        mid, after = day + datetime.timedelta(days=1), day + datetime.timedelta(days=2)
        if after in base and mid not in base and mid.weekday() != 6 and mid not in out:
            # 5/4 of 1990-2006 and the days between two holidays from 2009 on
            out[mid] = "国民の休日"
    return {d: n for d, n in out.items() if d.year == y}


def table_lines():
    lines = []
    for years, m, d, name in RULES:
        lines.append("%s/%d/%s\t%s" % (years, m, d, name))
    for s, name in ONCE:
        lines.append("%s\t%s" % (s, name))
    for y in range(FIRST, LAST + 1):
        lines.append("%d/3/%d\t春分の日" % (y, equinox(y, True)))
        lines.append("%d/9/%d\t秋分の日" % (y, equinox(y, False)))
    for y in range(FIRST, LAST + 1):
        for day, name in sorted(extra_days(y).items()):
            lines.append("%d/%d/%d\t%s" % (day.year, day.month, day.day, name))
    return lines


def holiday_lines():
    return ["日", "祝日\t国民の祝日、振替休日、国民の休日"]


def c_table(path):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("/*\n *----------------------------------------------------------------------\n *    TessronOS\n *\n"
                " *    Copyright (C) 2026 satromi\n *    This software is distributed under the T-License 2.2.\n"
                " *----------------------------------------------------------------------\n */\n\n")
        f.write("/*\n *\tktest_techo_hol.h\n *\tThe national holidays of %d to %d as dated lines of the 休日 part,\n"
                " *\twritten by tools/mktecho.py --table (design 11.23)\n */\n\n" % (FIRST, LAST))
        f.write("LOCAL CONST char *CONST kt_hol_table[] = {\n")
        for l in table_lines():
            f.write('\t"%s",\n' % l.replace("\\", "\\\\").replace('"', '\\"').replace("\t", "\\t"))
        f.write("};\n")


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


TAB_FORMAT = '<tab-format R="0" P="0" left="0" right="0" indent="0" ntabs="1" tabs="112"/>'


def part(rows):
    if not rows:
        return "<p>" + TAB_FORMAT + "</p>\n"
    out = []
    for i, r in enumerate(rows):
        out.append("<p>" + (TAB_FORMAT if i == 0 else "") + "<tab/>".join(esc(c) for c in r.split("\t")) + "</p>\n")
    return "".join(out)


def record(name, plans, people):
    return ('<tad version="1.0" encoding="UTF-8" filename="%s">\n<document>\n'
            '<paper type="doc" length="842" width="595" binding="0" imposition="0" top="57" bottom="57" left="57" right="57"/>\n'
            '<docmargin top="57" bottom="57" left="57" right="57"/>\n' % esc(name)
            + part(plans) + part(["\t".join(p) for p in people]) + part(holiday_lines())
            + "<p>" + TAB_FORMAT + "</p>\n</document>\n</tad>\n")


def meta(name, x, y):
    return {
        "name": name,
        "relationship": [],
        "linktype": False,
        "makeDate": "2026-10-04T00:00:00Z",
        "updateDate": "2026-10-04T00:00:00Z",
        "accessDate": "2026-10-04T00:00:00Z",
        "periodDate": None,
        "refCount": 1,
        "recordCount": 1,
        "editable": True,
        "deletable": True,
        "readable": True,
        "maker": "TRON User",
        "window": {
            "pos": {"x": x, "y": y},
            "width": 400,
            "height": 306,
            "resizable": True,
            "scrollable": False,
            "backgroundColor": "#ffffff"
        },
        "applist": {
            "tessronos-techo": {"name": "電子手帳", "defaultOpen": True},
            "basic-text-editor": {"name": "基本文章編集", "defaultOpen": False}
        }
    }


def put(uuid, name, plans, people, x, y):
    with open(os.path.join(XTAD, uuid + ".json"), "w", encoding="utf-8", newline="\n") as f:
        f.write(json.dumps(meta(name, x, y), ensure_ascii=False, indent=2) + "\n")
    with open(os.path.join(XTAD, uuid + "_0.xtad"), "w", encoding="utf-8", newline="\n") as f:
        f.write(record(name, plans, people))
    shutil.copyfile(os.path.join(XTAD, ICON), os.path.join(XTAD, uuid + ".ico"))


PLANS = [
    "2026/10/1\t10:00 企画会議 第2会議室",
    "2026/10/1\t15:30 見積書を送る",
    "2026/10/5\t12:00 田村さんと昼食",
    "2026/10/12\t体育の日の家族旅行(箱根)",
    "2026/10/20\t09:30 歯医者",
    "2026/10/20\t",
    "2026/10/20\t19:00 英会話",
    "2026/10/31\t町内会の秋祭り",
    "2026/11/3\t文化祭の手伝い",
    "2026/11/14\t13:00 渋谷駅南口で待ち合わせ",
]

# A person: よみ, 〒, 住所1, 住所2, 住所3, 氏名, 電話, FAX, メモ1, メモ2
PEOPLE = [
    ("あおき はるか", "160-0022", "東京都新宿区新宿9-9-9", "青葉ハイツ201", "", "青木 遥",
     "03-0000-1111", "", "大学の同期", ""),
    ("かとう けんじ", "460-0008", "名古屋市中区栄9-9", "", "", "加藤 健二",
     "052-000-2222", "052-000-2223", "取引先 営業部", "毎年年賀状"),
    ("きむら ゆうこ", "980-0021", "仙台市青葉区中央9-9-9", "", "", "木村 優子",
     "022-000-3333", "", "テニス仲間", ""),
    ("たむら しんいち", "530-0001", "大阪市北区梅田9-9-9", "梅田ビル10階", "株式会社タムラ商事", "田村 慎一",
     "06-0000-4444", "06-0000-4445", "取引先 部長", "ゴルフ好き"),
    ("なかむら あきら", "812-0011", "福岡市博多区博多駅前9-9", "", "", "中村 明",
     "092-000-5555", "", "高校の先輩", ""),
    ("はやし みどり", "060-0001", "札幌市中央区北一条西9-9", "", "", "林 みどり",
     "011-000-6666", "", "料理教室", "誕生日 5月3日"),
    ("やまだ たろう", "220-0011", "横浜市西区高島9-9", "みなとマンション502", "", "山田 太郎",
     "045-000-7777", "045-000-7778", "近所", ""),
    ("わたなべ さくら", "730-0011", "広島市中区基町9-9", "", "", "渡辺 さくら",
     "082-000-8888", "", "従妹", ""),
    ("Smith John", "", "1 Main Street", "Springfield", "USA", "John Smith",
     "+1-555-0100", "", "pen pal", ""),
]

if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[1] == "--table":
        c_table(sys.argv[2])
        print("table lines", len(table_lines()))
    else:
        put(TEMPLATE, "手帳用紙", [], [], 100, 70)
        put(SAMPLE, "私の手帳", PLANS, PEOPLE, 100, 70)
