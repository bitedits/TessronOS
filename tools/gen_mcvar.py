"""The table of variants of マイクロカード's 異形字ゆらぎ同一視 (application/mcard/mc_var.c).

  python tools/gen_mcvar.py

Old forms and the forms of China and Taiwan, each followed by the Japanese
form it is written as today, from the list below; and the compatibility
ideographs of Unicode, each as the unified ideograph it decomposes to.
A letter the table gives is made never to be a variant itself.
"""

import io
import os
import unicodedata

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# old forms, forms of China and Taiwan, and other variants, each followed by the letter it stands for
PAIRS = """
亞亜 惡悪 壓圧 圍囲 醫医 爲為 壹壱 隱隠 榮栄 營営 衞衛 驛駅 圓円 鹽塩 艷艶 應応 歐欧 毆殴 櫻桜 奧奥
假仮 價価 畫画 會会 囘回 壞壊 懷懐 繪絵 擴拡 殼殻 覺覚 學学 嶽岳 樂楽 渴渇 卷巻 陷陥 寬寛 歡歓 罐缶
觀観 關関 顏顔 歸帰 氣気 龜亀 僞偽 戲戯 犧犠 舊旧 據拠 擧挙 峽峡 挾挟 狹狭 曉暁 區区 驅駆 勳勲 徑径
惠恵 揭掲 溪渓 經経 螢蛍 輕軽 繼継 鷄鶏 藝芸 缺欠 儉倹 劍剣 圈圏 檢検 權権 獻献 縣県 險険 顯顕 驗験
嚴厳 效効 廣広 恆恒 鑛鉱 號号 國国 黑黒 濟済 碎砕 齋斎 劑剤 雜雑 參参 慘惨 棧桟 蠶蚕 贊賛 殘残 絲糸
齒歯 兒児 辭辞 濕湿 實実 舍舎 寫写 釋釈 壽寿 收収 從従 澁渋 獸獣 縱縦 肅粛 處処 緖緒 敍叙 獎奨 將将
燒焼 稱称 證証 乘乗 剩剰 壤壌 孃嬢 條条 淨浄 疊畳 穰穣 讓譲 釀醸 囑嘱 觸触 寢寝 愼慎 眞真 盡尽 圖図
粹粋 醉酔 隨随 髓髄 數数 樞枢 聲声 靜静 齊斉 攝摂 竊窃 專専 戰戦 淺浅 潛潜 纖繊 踐践 錢銭 禪禅 雙双
壯壮 搜捜 插挿 爭争 總総 聰聡 莊荘 裝装 騷騒 增増 臟臓 藏蔵 屬属 續続 墮堕 體体 對対 帶帯 滯滞 臺台
瀧滝 擇択 澤沢 單単 擔担 膽胆 團団 彈弾 斷断 癡痴 遲遅 晝昼 蟲虫 鑄鋳 廳庁 聽聴 敕勅 鎭鎮 遞逓 鐵鉄
轉転 點点 傳伝 黨党 盜盗 燈灯 當当 鬪闘 德徳 獨独 讀読 屆届 繩縄 貳弐 惱悩 腦脳 霸覇 廢廃 拜拝 賣売
麥麦 發発 髮髪 拔抜 蠻蛮 祕秘 濱浜 甁瓶 拂払 佛仏 竝並 變変 邊辺 邉辺 辨弁 瓣弁 辯弁 舖舗 步歩 穗穂
寶宝 豐豊 沒没 飜翻 每毎 萬万 滿満 默黙 譯訳 藥薬 與与 豫予 餘余 譽誉 搖揺 樣様 謠謡 來来 賴頼 亂乱
覽覧 龍竜 兩両 獵猟 綠緑 壘塁 淚涙 勵励 禮礼 隸隷 靈霊 齡齢 戀恋 爐炉 勞労 樓楼 錄録 灣湾 髙高 﨑崎
濵浜 嶋島 嶌島 槗橋 瀨瀬 冨富 曻昇 㐂喜 塚塚 黃黄 淸清 晴晴 靑青 卽即 旣既 槪概 卑卑 器器 突突 臭臭
類類 戾戻 祿禄 渚渚 都都 著著 者者 朗朗 憎憎 層層 社社 祉祉 祈祈 祐祐 神神 祝祝 福福 禍禍 禎禎 穀穀
飯飯 館館 莖茎 搖揺 檜桧 槇槙 籠篭 蘆芦 螢蛍 驒騨 鷗鴎 麴麹 攪撹 頰頬 鄕郷 卿卿 吞呑 噓嘘 姸妍 屛屏
幷并 搔掻 摑掴 瘦痩 繫繋 醬醤 鹼鹸 蠟蝋 詮詮 遙遥 遥遥 遞逓 隆隆 響響 頻頻 祖祖 視視 謁謁
饰飾 东東 车車 马馬 鸟鳥 鱼魚 门門 见見 贝貝 长長 书書 语語 说説 读読 们們 这這 时時 国国 学学
发発 对対 动動 电電 话話 汉漢 样様 实実 关関 观観 认認 识識 记記 讯訊 论論 设設 计計 饭飯 饮飲 馆館
钱銭 银銀 铁鉄 钟鐘 问問 间間 闻聞 开開 归帰 华華 业業 产産 卫衛 历歴 县県 广広 应応 张張 录録 总総
恶悪 爱愛 气気 热熱 乐楽 习習 乡郷 买買 卖売 亲親 伟偉 传伝 处処 备備 复復 头頭 夺奪 尽尽 层層 岁歳
带帯 师師 帮幇 并並 庆慶 库庫 废廃 异異 弃棄 强強 彻徹 态態 怀懐 战戦 户戸 扫掃 报報 拥擁 择択 换換
据拠 摄摂 数数 无無 显顕 晓暁 杂雑 权権 条条 来来 杨楊 极極 构構 枪槍 标標 树樹 桥橋 机機 欢歓 毕畢
沟溝 泽沢 济済 测測 涨漲 渐漸 湾湾 满満 灭滅 灯灯 炉炉 烟煙 爷爺 狮獅 猎猟 环環 现現 画画 畅暢 疗療
皱皺 盐塩 监監 盘盤 着着 码碼 础礎 确確 礼礼 种種 积積 称称 稳穏 穷窮 窃窃 竞競 笔筆 签簽 简簡 粮糧
紧緊 红紅 约約 级級 纪紀 纯純 纳納 纸紙 线線 练練 组組 细細 织織 终終 经経 结結 绘絵 给給 络絡 统統
继継 续続 维維 综綜 绿緑 编編 缘縁 网網 罗羅 职職 联聯 肃粛 胜勝 脑脳 脚脚 舍舎 艺芸 节節 范範
荣栄 药薬 获獲 营営 虽雖 衔銜 补補 装装 视視 览覧 触触 誉誉 计計 讨討 让譲 议議 讲講 许許 证証 评評
识識 诉訴 词詞 译訳 试試 诗詩 诚誠 话話 询詢 该該 详詳 语語 误誤 请請 调調 谈談 谢謝 谱譜 贞貞 负負
贡貢 财財 责責 败敗 货貨 质質 购購 贴貼 贵貴 费費 贸貿 资資 赏賞 赛賽 赵趙 践践 跃躍 车車 轨軌 转転
轮輪 软軟 轻軽 载載 较較 辈輩 边辺 达達 过過 运運 还還 进進 远遠 连連 迟遅 适適 选選 递逓 逻邏 遗遺
邮郵 邻隣 酱醤 释釈 针針 钢鋼 钩鈎 铃鈴 链鏈 锁鎖 镇鎮 长長 闭閉 闲閑 阅閲 队隊 阳陽 阴陰 阵陣
阶階 际際 陆陸 陈陳 险険 随随 隐隠 难難 雾霧 静静 韩韓 页頁 顶頂 项項 顺順 须須 顾顧 预預 领領 频頻
题題 颜顔 风風 飞飛 饱飽 馀余 驱駆 验験 骨骨 鲁魯 鲜鮮 鸡鶏 鹤鶴 麦麦 黄黄 齐斉 齿歯 龙竜 龟亀
"""

pairs = {}
for w in PAIRS.split():
    if len(w) != 2:
        raise SystemExit("bad pair %r" % w)
    a, b = w[0], w[1]
    if a == b:
        continue
    pairs[ord(a)] = ord(b)

# compatibility ideographs as the unified ones they stand for
for r in (range(0xF900, 0xFB00), range(0x2F800, 0x2FA20)):
    for c in r:
        ch = chr(c)
        n = unicodedata.normalize('NFKC', ch)
        if len(n) == 1 and n != ch and unicodedata.name(ch, '').startswith('CJK COMPATIBILITY IDEOGRAPH'):
            pairs.setdefault(c, ord(n))

# the letter a variant stands for is never itself a variant
for k in list(pairs):
    v = pairs[k]
    seen = 0
    while v in pairs and seen < 4:
        v = pairs[v]
        seen += 1
    pairs[k] = v
    if v == k:
        del pairs[k]

out = io.StringIO()
out.write('''/*
 *----------------------------------------------------------------------
 *    TessronOS
 *
 *    Copyright (C) 2026 satromi
 *    This software is distributed under the T-License 2.2.
 *----------------------------------------------------------------------
 */

/*
 *	mc_var.c
 *	マイクロカード: the variants of kanji and the letters they stand for,
 *	for 異形字ゆらぎ同一視 (design 11.18)
 *
 *	Old forms (舊字體) and the forms of China and Taiwan, each the
 *	Japanese form it is written as today, and the compatibility
 *	ideographs of Unicode each the unified ideograph it is. In the order
 *	of the variants, for a binary search. Made by a script from a list
 *	of pairs and Unicode's own decompositions; a letter the table gives
 *	is never a variant itself.
 */

#include "mc_db.h"

EXPORT CONST UINT mc_var_tab[][2] = {
''')
for k in sorted(pairs):
    out.write('\t{ 0x%05X, 0x%05X },\t/* %s %s */\n' % (k, pairs[k], chr(k), chr(pairs[k])))
out.write('};\n\nEXPORT CONST INT mc_var_n = (INT)( sizeof(mc_var_tab) / sizeof(mc_var_tab[0]) );\n')
io.open(os.path.join(ROOT, 'application', 'mcard', 'mc_var.c'), 'w', encoding='utf-8',
        newline='\n').write(out.getvalue())
print(len(pairs))
