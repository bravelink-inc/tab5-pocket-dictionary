#!/usr/bin/env python3
"""convert_kanjidic.py - kanjidic2.xml.gz -> kanjidic2.tsv (key list<TAB>display|definition)

Keys: romaji of every on/kun reading and every English meaning, so 心 is found by
"shin", "kokoro" and "heart".
"""
import argparse
import sys, gzip, re, sys
import xml.etree.ElementTree as ET
from romaji import romaji_keys, to_hiragana

GRADE = {1:'小1',2:'小2',3:'小3',4:'小4',5:'小5',6:'小6',8:'中学(常用)',9:'人名用',10:'人名用'}


def main():
    try:
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    except (AttributeError, ValueError):
        pass
    ap = argparse.ArgumentParser()
    ap.add_argument('kanjidic')
    ap.add_argument('-o', '--output', required=True)
    a = ap.parse_args()
    root = ET.parse(gzip.open(a.kanjidic)).getroot()
    out = open(a.output, 'w', encoding='utf-8')
    n = 0
    chars = []
    for c in root.iter('character'):
        lit = c.findtext('literal')
        misc = c.find('misc')
        grade = misc.findtext('grade')
        strokes = misc.findtext('stroke_count')
        freq = misc.findtext('freq')
        jlpt = misc.findtext('jlpt')
        rad = c.find('radical')
        radical = rad.findtext("rad_value[@rad_type='classical']") if rad is not None else None
        on, kun, nanori, meanings = [], [], [], []
        rm = c.find('reading_meaning')
        if rm is not None:
            for g in rm.findall('rmgroup'):
                for r in g.findall('reading'):
                    t = r.get('r_type')
                    if t == 'ja_on': on.append(r.text)
                    elif t == 'ja_kun': kun.append(r.text)
                for m in g.findall('meaning'):
                    if m.get('m_lang') is None and m.text:
                        meanings.append(m.text)
            nanori = [x.text for x in rm.findall('nanori')]
        if not (on or kun or meanings):
            continue
        keys = []
        for r in on + kun:
            base = r.split('.')[0].replace('-', '')
            for k in romaji_keys(base):
                if k and k not in keys: keys.append(k)
        for m in meanings:
            k = re.sub(r'[^a-z0-9 ]', '', m.lower()).strip()
            if k and k not in keys: keys.append(k)
        if not keys: continue
        parts = []
        if on: parts.append('音: ' + '、'.join(on))
        if kun: parts.append('訓: ' + '、'.join(kun))
        if meanings: parts.append('意味: ' + '; '.join(meanings))
        info = []
        if strokes: info.append(f'{strokes}画')
        if radical: info.append(f'部首番号 {radical}')
        if grade and int(grade) in GRADE: info.append(GRADE[int(grade)])
        if jlpt: info.append(f'JLPT N{jlpt}')
        if freq: info.append(f'頻度 {freq}位')
        if info: parts.append('　'.join(info))
        if nanori: parts.append('名乗り: ' + '、'.join(nanori))
        fr = int(freq) if freq else 9999
        chars.append((fr, ', '.join(keys) + '\t' + lit + '|' + ' / '.join(parts) + '\n'))
    chars.sort(key=lambda t: t[0])   # frequent kanji first for equal keys
    for _, line in chars:
        out.write(line); n += 1
    out.close()
    print(f'{n} kanji', file=sys.stderr)


if __name__ == '__main__':
    main()
