"""romaji.py - kana -> romaji search keys for the pocket dictionary.

Two spellings are produced for every reading so that both common IME habits work:
  * wapuro Hepburn : shi chi tsu fu ji sha ...   (とうきょう -> toukyou)
  * kunrei          : si  ti  tu  hu zi  sya ...
"""
import unicodedata

_BASE = {
 'あ':'a','い':'i','う':'u','え':'e','お':'o',
 'か':'ka','き':'ki','く':'ku','け':'ke','こ':'ko','さ':'sa','し':'shi','す':'su','せ':'se','そ':'so',
 'た':'ta','ち':'chi','つ':'tsu','て':'te','と':'to','な':'na','に':'ni','ぬ':'nu','ね':'ne','の':'no',
 'は':'ha','ひ':'hi','ふ':'fu','へ':'he','ほ':'ho','ま':'ma','み':'mi','む':'mu','め':'me','も':'mo',
 'や':'ya','ゆ':'yu','よ':'yo','ら':'ra','り':'ri','る':'ru','れ':'re','ろ':'ro','わ':'wa','ゐ':'i','ゑ':'e','を':'wo','ん':'n',
 'が':'ga','ぎ':'gi','ぐ':'gu','げ':'ge','ご':'go','ざ':'za','じ':'ji','ず':'zu','ぜ':'ze','ぞ':'zo',
 'だ':'da','ぢ':'ji','づ':'zu','で':'de','ど':'do','ば':'ba','び':'bi','ぶ':'bu','べ':'be','ぼ':'bo',
 'ぱ':'pa','ぴ':'pi','ぷ':'pu','ぺ':'pe','ぽ':'po','ゔ':'vu',
 'ぁ':'a','ぃ':'i','ぅ':'u','ぇ':'e','ぉ':'o','ゃ':'ya','ゅ':'yu','ょ':'yo','ゎ':'wa',
 'きゃ':'kya','きゅ':'kyu','きょ':'kyo','しゃ':'sha','しゅ':'shu','しょ':'sho','ちゃ':'cha','ちゅ':'chu','ちょ':'cho',
 'にゃ':'nya','にゅ':'nyu','にょ':'nyo','ひゃ':'hya','ひゅ':'hyu','ひょ':'hyo','みゃ':'mya','みゅ':'myu','みょ':'myo',
 'りゃ':'rya','りゅ':'ryu','りょ':'ryo','ぎゃ':'gya','ぎゅ':'gyu','ぎょ':'gyo','じゃ':'ja','じゅ':'ju','じょ':'jo',
 'ぢゃ':'ja','ぢゅ':'ju','ぢょ':'jo','びゃ':'bya','びゅ':'byu','びょ':'byo','ぴゃ':'pya','ぴゅ':'pyu','ぴょ':'pyo',
 'しぇ':'she','ちぇ':'che','じぇ':'je','てぃ':'ti','でぃ':'di','とぅ':'tu','どぅ':'du','でゅ':'dyu','てゅ':'tyu',
 'ふぁ':'fa','ふぃ':'fi','ふぇ':'fe','ふぉ':'fo','ふゅ':'fyu','うぃ':'wi','うぇ':'we','うぉ':'wo',
 'ゔぁ':'va','ゔぃ':'vi','ゔぇ':'ve','ゔぉ':'vo','つぁ':'tsa','つぃ':'tsi','つぇ':'tse','つぉ':'tso','いぇ':'ye',
}
_KUNREI = {
 'し':'si','ち':'ti','つ':'tu','ふ':'hu','じ':'zi','ぢ':'di','づ':'du',
 'しゃ':'sya','しゅ':'syu','しょ':'syo','ちゃ':'tya','ちゅ':'tyu','ちょ':'tyo','じゃ':'zya','じゅ':'zyu','じょ':'zyo',
 'ぢゃ':'dya','ぢゅ':'dyu','ぢょ':'dyo','しぇ':'sye','ちぇ':'tye','じぇ':'zye','ふぁ':'fa','を':'wo',
}
_VOWELS = 'aiueo'


def to_hiragana(s: str) -> str:
    out = []
    for ch in s:
        o = ord(ch)
        if 0x30A1 <= o <= 0x30F6:      # katakana -> hiragana
            out.append(chr(o - 0x60))
        else:
            out.append(ch)
    return ''.join(out)


# [book:12-romanize]
def _romanize(kana: str, table_override: dict) -> str:
    s = to_hiragana(kana)
    out = []
    i = 0
    sokuon = False
    while i < len(s):
        ch = s[i]
        if ch == 'っ':
            sokuon = True
            i += 1
            continue
        if ch == 'ー':
            if out and out[-1] and out[-1][-1] in _VOWELS:
                out.append(out[-1][-1])
            i += 1
            continue
        pair = s[i:i+2]
        rom = None
        if len(pair) == 2 and pair in _BASE:
            rom = table_override.get(pair, _BASE[pair])
            i += 2
        elif ch in _BASE:
            rom = table_override.get(ch, _BASE[ch])
            i += 1
        else:
            # not kana: keep ASCII letters/digits, drop the rest
            n = unicodedata.normalize('NFKC', ch)
            if n.isascii() and n.isalnum():
                out.append(n.lower())
            i += 1
            continue
        if sokuon and rom and rom[0] not in _VOWELS:
            rom = rom[0] + rom
        sokuon = False
        out.append(rom)
    return ''.join(out)
# [/book:12-romanize]


# [book:12-romaji-keys]
def romaji_keys(kana: str):
    """Return the distinct romaji spellings (wapuro-Hepburn first, then kunrei)."""
    keys = []
    for tbl in ({}, _KUNREI):
        r = _romanize(kana, tbl)
        if r and r not in keys:
            keys.append(r)
    return keys
# [/book:12-romaji-keys]


def is_kana_only(s: str) -> bool:
    return all(0x3041 <= ord(c) <= 0x30FF or c in 'ー々' for c in s)


if __name__ == '__main__':
    import sys
    for w in sys.argv[1:] or ['とうきょう', 'しんぶん', 'きって', 'ジャズ', 'ぢゃ', 'コーヒー', 'ふぁいる', 'ちょっと']:
        print(w, romaji_keys(w))
