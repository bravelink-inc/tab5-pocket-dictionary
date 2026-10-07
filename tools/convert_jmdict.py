#!/usr/bin/env python3
"""convert_jmdict.py - JMdict_e.gz -> TSV files for build_dict.py

Outputs (UTF-8, headword<TAB>definition):
  <out>/jmdict_waei.tsv   Japanese -> English (keys: romaji of every reading)
  <out>/jmdict_eiwa.tsv   English gloss -> Japanese words (reverse index)
  <out>/jmdict_readings.tsv  kanji form -> reading (used by convert_wnjpn.py)

Because the firmware searches by *key* while displaying the *headword*, the TSV uses
"key1, key2<TAB>definition" lines where the definition starts with the display headword
followed by a tab-free separator "|" (see build_dict.py --headword-sep).
"""
import argparse
import sys, gzip, io, re, sys
import xml.etree.ElementTree as ET
from collections import defaultdict
from romaji import romaji_keys

PRI1 = {'ichi1', 'news1', 'spec1', 'gai1'}
PRI2 = {'ichi2', 'news2', 'spec2', 'gai2'}


def priority(pris):
    if pris & PRI1: return 0
    if pris & PRI2: return 1
    if any(p.startswith('nf') for p in pris): return 2
    return 3


def main():
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser()
    ap.add_argument('jmdict', help='JMdict_e.gz')
    ap.add_argument('-o', '--outdir', required=True)
    a = ap.parse_args()

    raw = gzip.open(a.jmdict, 'rb').read().decode('utf-8')
    # keep entity *names* as the short tags (&n; -> "n") instead of the long descriptions
    raw = re.sub(r'<!ENTITY (\S+) "[^"]*">', lambda m: '<!ENTITY %s "%s">' % (m.group(1), m.group(1)), raw)
    root = ET.parse(io.BytesIO(raw.encode('utf-8'))).getroot()

    entries = []
    readings_map = {}
    for e in root.iter('entry'):
        seq = e.findtext('ent_seq')
        kebs, rebs, pris = [], [], set()
        for k in e.findall('k_ele'):
            kebs.append(k.findtext('keb'))
            pris.update(p.text for p in k.findall('ke_pri'))
        for r in e.findall('r_ele'):
            reb = r.findtext('reb')
            rebs.append(reb)
            pris.update(p.text for p in r.findall('re_pri'))
        senses = []
        for s in e.findall('sense'):
            pos = [p.text for p in s.findall('pos')]
            misc = [m.text for m in s.findall('misc')]
            field = [f.text for f in s.findall('field')]
            glosses = [g.text for g in s.findall('gloss') if g.text]
            if not glosses: continue
            senses.append((pos, misc, field, glosses))
        if not senses or not rebs: continue
        pri = priority(pris)
        entries.append((pri, int(seq), kebs, rebs, senses))
        for kb in kebs:
            readings_map.setdefault(kb, rebs[0])
    entries.sort(key=lambda t: (t[0], t[1]))
    print(f'{len(entries)} entries', file=sys.stderr)

# [book:12-jmdict-headword]
    def headword(kebs, rebs):
        if kebs:
            return '・'.join(kebs[:3]) + '【' + '・'.join(rebs[:3]) + '】'
        return '・'.join(rebs[:3])

# [/book:12-jmdict-headword]
    # ---- 和英 ---------------------------------------------------------------
    waei = open(f'{a.outdir}/jmdict_waei.tsv', 'w', encoding='utf-8')
    reverse = defaultdict(list)   # gloss -> [(pri, seq, short headword, pos)]
    n_lines = 0
    for pri, seq, kebs, rebs, senses in entries:
        keys = []
        for reb in rebs:
            for k in romaji_keys(reb):
                if k not in keys: keys.append(k)
        if not keys: continue
        parts = []
        for pos, misc, field, glosses in senses:
            tag = ''
            if pos: tag += '(' + ','.join(pos) + ') '
            if field: tag += '[' + ','.join(field) + '] '
            if misc: tag += '{' + ','.join(misc) + '} '
            parts.append(tag + '; '.join(glosses))
        hw = headword(kebs, rebs)
        waei.write(', '.join(keys) + '\t' + hw + '|' + ' / '.join(parts) + '\n')
        n_lines += 1
        short = (kebs[0] + '【' + rebs[0] + '】') if kebs else rebs[0]
        for pos, misc, field, glosses in senses:
            ptag = '(' + ','.join(pos) + ')' if pos else ''
            for g in glosses:
                key = re.sub(r'\s*\(.*?\)\s*', ' ', g).strip().lower()
                if not key or len(key) > 60: continue
                reverse[key].append((pri, seq, short, ptag, g))
    waei.close()
    print(f'waei: {n_lines} lines', file=sys.stderr)

    # ---- 英和 (reverse) -------------------------------------------------------
    eiwa = open(f'{a.outdir}/jmdict_eiwa.tsv', 'w', encoding='utf-8')
    n_lines = 0
    for key in sorted(reverse):
        items = sorted(set(reverse[key]))[:20]
        seen, parts = set(), []
        for pri, seq, short, ptag, g in items:
            if short in seen: continue
            seen.add(short)
            parts.append(short + ' ' + ptag)
        eiwa.write(key + '\t' + key + '|' + ' / '.join(parts) + '\n')
        n_lines += 1
    eiwa.close()
    print(f'eiwa: {n_lines} lines', file=sys.stderr)

    with open(f'{a.outdir}/jmdict_readings.tsv', 'w', encoding='utf-8') as f:
        for k, v in readings_map.items():
            f.write(f'{k}\t{v}\n')
    print(f'readings: {len(readings_map)}', file=sys.stderr)


if __name__ == '__main__':
    main()
