#!/usr/bin/env python3
"""convert_wnjpn.py - Japanese WordNet (wnjpn-ok.tab.gz + wnjpn-def.tab.gz) -> kokugo TSV

Each Japanese word becomes one entry whose senses are the Japanese definitions of its
synsets. Readings come from JMdict (jmdict_readings.tsv) with pykakasi as a fallback,
and are turned into romaji search keys.
"""
import argparse
import sys, gzip, sys, unicodedata
from collections import defaultdict
from romaji import romaji_keys, is_kana_only

POS = {'n': '名', 'v': '動', 'a': '形', 'r': '副'}


def main():
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser()
    ap.add_argument('ok_tab')
    ap.add_argument('def_tab')
    ap.add_argument('--readings', required=True, help='jmdict_readings.tsv')
    ap.add_argument('-o', '--output', required=True)
    a = ap.parse_args()

    readings = {}
    for line in open(a.readings, encoding='utf-8'):
        k, _, v = line.rstrip('\n').partition('\t')
        if k and v: readings[k] = v

    defs = defaultdict(list)
    for line in gzip.open(a.def_tab, 'rt', encoding='utf-8'):
        f = line.rstrip('\n').split('\t')
        if len(f) >= 4 and f[3].strip():
            defs[f[0]].append(f[3].strip())

    word_syn = defaultdict(list)
    syn_words = defaultdict(list)
    for line in gzip.open(a.ok_tab, 'rt', encoding='utf-8'):
        f = line.rstrip('\n').split('\t')
        if len(f) < 2: continue
        syn, word = f[0], f[1]
        if syn not in defs: continue
        if syn not in word_syn[word]: word_syn[word].append(syn)
        syn_words[syn].append(word)

    import pykakasi
    kks = pykakasi.kakasi()

    out = open(a.output, 'w', encoding='utf-8')
    n = skipped = 0
    for word in sorted(word_syn, key=lambda w: (-len(word_syn[w]), w)):
        if is_kana_only(word):
            reading = word
        elif word in readings:
            reading = readings[word]
        else:
            reading = ''.join(x['hira'] for x in kks.convert(word))
        keys = []
        for k in romaji_keys(reading):
            if k and k not in keys: keys.append(k)
        ascii_key = unicodedata.normalize('NFKC', word).lower()
        if ascii_key.isascii() and ascii_key.isalnum() and ascii_key not in keys:
            keys.append(ascii_key)
        if not keys:
            skipped += 1
            continue
        hw = word if is_kana_only(word) or reading == word else f'{word}【{reading}】'
        parts = []
        for syn in word_syn[word]:
            pos = POS.get(syn[-1], '')
            syns = [w for w in syn_words[syn] if w != word][:6]
            for d in defs[syn]:
                s = f'({pos}) {d}' if pos else d
                if syns: s += '　〔類: ' + '、'.join(syns) + '〕'
                parts.append(s)
        out.write(', '.join(keys) + '\t' + hw + '|' + ' / '.join(parts) + '\n')
        n += 1
    out.close()
    print(f'{n} words ({skipped} skipped)', file=sys.stderr)


if __name__ == '__main__':
    main()
