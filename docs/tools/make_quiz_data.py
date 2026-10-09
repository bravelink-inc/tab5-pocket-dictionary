#!/usr/bin/env python3
"""make_quiz_data.py - build the word/kanji lists that the firmware reads for its quizzes.

  <outdir>/ngsl_levels.txt   word<TAB>level (1: rank 1-1000, 2: 1001-2000, 3: 2001-)
  <outdir>/kanji_quiz.txt    kanji<TAB>grade<TAB>on readings<TAB>kun readings

Both lists are CC BY-SA 4.0 data, so they are not in the repository or the firmware: the reader
makes them and copies them to the SD card's dict folder, next to the dictionaries
(default outdir: data/sd/dict).

Inputs:
  NGSL_12_stats.csv   https://www.newgeneralservicelist.com/s/NGSL_12_stats.csv  (CC BY-SA 4.0)
  kanjidic2.xml.gz    http://www.edrdg.org/kanjidic/kanjidic2.xml.gz              (CC BY-SA 4.0)
Only words that exist in the built-in EJDict are kept, so every quiz question has a definition.
Usage: make_quiz_data.py --ngsl NGSL_12_stats.csv --kanjidic kanjidic2.xml.gz [-o data/sd/dict]
"""
import argparse, csv, glob, gzip, os, sys
import xml.etree.ElementTree as ET
from build_dict import normalize_key, split_spellings

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def ejdict_words(src):
    words = set()
    for p in glob.glob(os.path.join(src, "*.txt")):
        with open(p, encoding="utf-8", errors="replace") as source:
            for line in source:
                head = line.split("\t", 1)[0]
                for w in split_spellings(head):
                    words.add(normalize_key(w))
    return words


def build_ngsl(path, ej, out):
    rows = list(csv.DictReader(open(path, encoding="utf-8-sig")))
    kept = missing = 0
    with open(out, "w", encoding="utf-8") as f:
        f.write("# NGSL 1.2 (Browne, C., Culligan, B., and Phillips, J.), CC BY-SA 4.0\n")
        f.write("# https://www.newgeneralservicelist.com/  https://creativecommons.org/licenses/by-sa/4.0/\n")
        f.write("# Changed: lemmas lower-cased, ranks grouped into 3 levels, words missing from EJDict removed\n")
        f.write("# level 1: rank 1-1000, 2: 1001-2000, 3: 2001-2809\n")
        for r in rows:
            w = r["Lemma"].strip().lower()
            rank = int(r["SFI Rank"])
            level = 1 if rank <= 1000 else 2 if rank <= 2000 else 3
            if w in ej:
                f.write(f"{w}\t{level}\n"); kept += 1
            else:
                missing += 1
    print(f"{out}: {kept} words ({missing} not in EJDict)")


def kana_kun(r):
    """あたら.しい -> あたら(しい); drop the prefix/suffix markers '-'."""
    r = r.replace("-", "")
    if "." in r:
        stem, oku = r.split(".", 1)
        return f"{stem}({oku})"
    return r


def build_kanji(path, out):
    root = ET.parse(gzip.open(path)).getroot()
    n = 0
    with open(out, "w", encoding="utf-8") as f:
        f.write("# KANJIDIC2 (EDRDG), CC BY-SA 4.0 - Jouyou kanji with school grade\n")
        f.write("# The KANJIDIC2 file is the property of the Electronic Dictionary Research and Development Group,\n")
        f.write("# and is used in conformance with the Group's licence. https://www.edrdg.org/edrdg/licence.html\n")
        f.write("# Changed: only grade 1-6 and 8 kanji, Japanese readings only, okurigana marked with ( )\n")
        f.write("# kanji<TAB>grade (1-6 elementary, 8 junior high)<TAB>on (katakana, / separated)<TAB>kun (hiragana)\n")
        for c in root.iter("character"):
            g = c.findtext("misc/grade")
            if not g or int(g) not in (1, 2, 3, 4, 5, 6, 8):
                continue
            on, kun = [], []
            for r in c.iter("reading"):
                if r.get("r_type") == "ja_on":
                    if r.text not in on: on.append(r.text)
                elif r.get("r_type") == "ja_kun":
                    k = kana_kun(r.text)
                    if k and k not in kun: kun.append(k)
            if not on and not kun:
                continue
            f.write(f"{c.findtext('literal')}\t{g}\t{'/'.join(on)}\t{'/'.join(kun)}\n"); n += 1
    print(f"{out}: {n} kanji")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ngsl", required=True)
    ap.add_argument("--kanjidic", required=True)
    ap.add_argument("--ejdict", default=os.path.join(ROOT, "third_party", "ejdict", "src"))
    ap.add_argument("-o", "--outdir", default=os.path.join(ROOT, "data", "sd", "dict"))
    a = ap.parse_args()
    os.makedirs(a.outdir, exist_ok=True)
    build_ngsl(a.ngsl, ejdict_words(a.ejdict), os.path.join(a.outdir, "ngsl_levels.txt"))
    build_kanji(a.kanjidic, os.path.join(a.outdir, "kanji_quiz.txt"))


if __name__ == "__main__":
    main()
