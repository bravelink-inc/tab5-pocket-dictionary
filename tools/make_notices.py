#!/usr/bin/env python3
"""make_notices.py - write the sources and licences of the SD-card data next to it.

The SD dictionaries and quiz lists are made by the reader from the official data, so this file
is what travels with them: who made the data, its licence (with the URL), what was changed,
and the full text of the Japanese WordNet and WordNet 3.0 licences.

Usage: make_notices.py --jmdict JMdict_e.gz --kanjidic kanjidic2.xml.gz [-o data/sd/dict]
       -> <outdir>/出典とライセンス.txt
"""
import argparse, datetime, gzip, os, re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
NAME = "出典とライセンス.txt"
CC = "https://creativecommons.org/licenses/by-sa/4.0/"


def find_date(path, pattern, limit=2000):
    """Return the first date matching pattern in the first lines of a (gzipped) XML file."""
    opener = gzip.open if open(path, "rb").read(2) == b"\x1f\x8b" else open
    with opener(path, "rt", encoding="utf-8", errors="replace") as f:
        for i, line in enumerate(f):
            m = re.search(pattern, line)
            if m: return m.group(1)
            if i > limit: break
    return "不明"


def read(path):
    with open(path, encoding="utf-8") as f:
        return f.read().strip()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--jmdict", required=True)
    ap.add_argument("--kanjidic", required=True)
    ap.add_argument("--wnja-license", default=os.path.join(ROOT, "third_party", "wnja", "license.txt"))
    ap.add_argument("--wordnet-license", default=os.path.join(ROOT, "third_party", "wnja", "princeton_wordnet_license.txt"))
    ap.add_argument("-o", "--outdir", default=os.path.join(ROOT, "data", "sd", "dict"))
    a = ap.parse_args()

    jm = find_date(a.jmdict, r"JMdict created: (\d{4}-\d{2}-\d{2})")
    kd = find_date(a.kanjidic, r"<date_of_creation>([^<]+)</date_of_creation>")
    today = datetime.date.today().isoformat()
    text = f"""Tab5 電子辞書　SD カードのデータの出典とライセンス
（{today} に、使う人が公式のデータから作成。作成ツール: https://github.com/bravelink-inc/tab5-pocket-dictionary の tools/、MIT ライセンス）

このフォルダの辞書（.pdc）とテストのデータ（.txt）は、下の元データを変換したものです。
人に渡すときは、このファイルも一緒に渡し、それぞれのライセンスの条件を守ってください。
どのデータも無保証です。


■ 10_jmdict_waei.pdc（JMdict 和英）、20_jmdict_eiwa.pdc（JMdict 英和・逆引き）

元データ: JMdict（JMdict_e.gz、{jm} 作成）
This package uses the JMdict/EDICT and KANJIDIC dictionary files. These files are the property of
the Electronic Dictionary Research and Development Group, and are used in conformance with the
Group's licence.
Copyright: James William Breen and The Electronic Dictionary Research and Development Group
ライセンス: Creative Commons 表示 - 継承 4.0 国際（CC BY-SA 4.0）{CC}
EDRDG のライセンス: https://www.edrdg.org/edrdg/licence.html
JMdict の説明: https://www.edrdg.org/wiki/index.php/JMdict-EDICT_Dictionary_Project
変更点: 読みをローマ字（ヘボン式・訓令式）にした検索キーを足した。各項目の漢字表記と読みを 3 つまで
見出しにまとめ、語義に品詞などの略号を付けて 1 つの文字列にした。よく使う語が先に並ぶよう並べ替えた。
逆引き（20_jmdict_eiwa.pdc）は、英語の訳語ごとにその訳語を持つ日本語の項目を 20 件まで集めて作った。
最後に本書の辞書ファイル形式 PDC1 に変換した。
この 2 つの .pdc ファイルも CC BY-SA 4.0 です。


■ 30_kanjidic2.pdc（KANJIDIC2 漢字辞典）、kanji_quiz.txt（漢字の読みテスト）

元データ: KANJIDIC2（kanjidic2.xml.gz、{kd} 作成）
These files are the property of the Electronic Dictionary Research and Development Group, and are
used in conformance with the Group's licence.
Copyright: James William Breen and The Electronic Dictionary Research and Development Group
ライセンス: CC BY-SA 4.0　{CC}
EDRDG のライセンス: https://www.edrdg.org/edrdg/licence.html
KANJIDIC の説明: https://www.edrdg.org/wiki/index.php/KANJIDIC_Project
使った項目: 音読み、訓読み、英語の意味、名乗り、画数、部首番号、学年、JLPT、頻度順位
（SKIP コードなど、個別の条件が付いた項目は使っていない）
変更点: 音読み・訓読み・英語の意味をローマ字や小文字にした検索キーを足し、語義を 1 つの文字列にまとめ、
頻度順に並べ替えて PDC1 形式にした。kanji_quiz.txt は、学年のある常用漢字だけを選び、読みだけを残した。
この 2 つのファイルも CC BY-SA 4.0 です。


■ 40_wnjpn_kokugo.pdc（日本語 WordNet（語義・類語））

元データ: 日本語 WordNet 1.1（wnjpn-ok.tab.gz、wnjpn-def.tab.gz）。日本語 WordNet は Princeton
WordNet 3.0 をもとに作られている。
読み: JMdict（上記、CC BY-SA 4.0）の読みを使い、JMdict に無い語は pykakasi（GPL-3.0）で推定した。
JMdict のデータを含むので、このファイルは CC BY-SA 4.0 で、下の 2 つの条文の条件も守る必要がある。
変更点: 語ごとに、その語が属する概念の日本語の定義を語義にまとめ、同じ概念の語を 6 語まで類語として添えた。
読みをローマ字にした検索キーを付け、PDC1 形式にした。

--- Japanese WordNet の条文 ---
{read(a.wnja_license)}

--- WordNet 3.0 の条文 ---
{read(a.wordnet_license)}


■ ngsl_levels.txt（英単語テストの基礎・標準・発展）

元データ: New General Service List 1.2（Browne, C., Culligan, B., and Phillips, J.）
https://www.newgeneralservicelist.com/
ライセンス: CC BY-SA 4.0　{CC}
変更点: 見出し語を小文字にし、頻度順位を 3 段階に分け、本体の英和辞書（EJDict-hand）に無い語を除いた。
このファイルも CC BY-SA 4.0 です。
"""
    os.makedirs(a.outdir, exist_ok=True)
    out = os.path.join(a.outdir, NAME)
    with open(out, "w", encoding="utf-8", newline="\r\n") as f:
        f.write(text)
    print(f"{out}: JMdict {jm}, KANJIDIC2 {kd}")


if __name__ == "__main__":
    main()
