# Third-party data and libraries

## What this project distributes, and what it does not

- **Distributed** (in the repository and in the firmware): the program code (MIT), EJDict-hand (CC0), the Japanese fonts, and the libraries linked into the firmware. Their licences are listed below.
- **Not distributed**: JMdict, KANJIDIC2, Japanese WordNet and NGSL, and anything converted from them (the SD-card `.pdc` dictionaries and the quiz lists `ngsl_levels.txt` / `kanji_quiz.txt`). The reader downloads the official files and converts them on their own computer, with the tools in `tools/` (book chapters 11 and 12) or with the 「辞書を作る」 page (`web/make-dict.html`), which runs the same tools in the browser. `tools/make_notices.py` writes `出典とライセンス.txt` next to the converted files with the sources, licences, changes and the full licence texts that must travel with them.

## Dictionary data

| Data | Source | License | In this project |
| --- | --- | --- | --- |
| EJDict-hand (英和) | https://github.com/kujirahand/EJDict | CC0 1.0 (Public Domain) | Included in `third_party/ejdict/` (with its frequency lists). Converted to `data/ejdict.pdc` at build time and flashed with the firmware |
| JMdict (和英・英和) | https://www.edrdg.org/wiki/index.php/JMdict-EDICT_Dictionary_Project | CC BY-SA 4.0 (https://www.edrdg.org/edrdg/licence.html) | Not distributed. Converted by the reader with `tools/convert_jmdict.py` |
| KANJIDIC2 (漢字) | https://www.edrdg.org/wiki/index.php/KANJIDIC_Project | CC BY-SA 4.0 (same licence) | Not distributed. Converted by the reader with `tools/convert_kanjidic.py` and `tools/make_quiz_data.py`. No fields with special conditions (SKIP, Pinyin, Four Corner, etc.) are used |
| Japanese WordNet 1.1 (国語) | https://github.com/bond-lab/wnja | Japanese WordNet License (`third_party/wnja/license.txt`); based on WordNet 3.0 (`third_party/wnja/princeton_wordnet_license.txt`) | Not distributed. Converted by the reader with `tools/convert_wnjpn.py` (readings from JMdict, so the result is also CC BY-SA 4.0) |
| NGSL 1.2 (英単語テストの段階) | New General Service List by Browne, C., Culligan, B., and Phillips, J. — https://www.newgeneralservicelist.com | CC BY-SA 4.0 | Not distributed. Converted by the reader with `tools/make_quiz_data.py` |

The firmware acknowledges these sources on its 「出典とライセンス」 screen (Ctrl+L), as the EDRDG licence asks of apps that use the files: "These files are the property of the Electronic Dictionary Research and Development Group, and are used in conformance with the Group's licence."

## Fonts embedded in the firmware

| Font | License |
| --- | --- |
| IPAGothic (IPA), as converted by M5GFX into `lgfxJapanGothic` (u8g2 bitmap format, 4,425 glyphs) | IPA Font License Agreement v1.0 — full text in `third_party/ipafont/IPA_Font_License_Agreement_v1.0.txt` |
| Noto Sans JP (Copyright 2014-2021 Adobe, https://github.com/notofonts/noto-cjk), converted by `tools/make_font.py` into u8g2 bitmap fonts (about 13,400 glyphs, renamed), packed as `third_party/notosansjp/jp_fonts.pfn` and flashed to the `font` partition | SIL Open Font License 1.1 — full text in `third_party/notosansjp/OFL.txt` |

The firmware image contains bitmap fonts derived from IPAGothic. M5GFX distributes them under the
IPA Font License with a different name (`lgfx_font_japan_gothic_*`), and this project redistributes
them unchanged inside the firmware. The original IPA fonts are available from
<https://moji.or.jp/ipafont/>; to use the original font instead, replace the M5GFX font files and rebuild.

## Libraries in the firmware (fetched by the ESP-IDF component manager)

| Library | License |
| --- | --- |
| ESP-IDF (including FreeRTOS-Kernel, MIT), espressif/usb_host_hid | Apache-2.0 |
| M5Unified, M5GFX (M5Stack) | MIT |

`scripts/make_release.sh` copies these licence texts next to the firmware on the flashing page (`firmware/licences/`).

## The flashing page and the 「辞書を作る」 page

| Component | License |
| --- | --- |
| ESP Web Tools (esphome/esp-web-tools), bundled with esptool-js | Apache-2.0 |
| Pyodide 0.28.3 (CPython for WebAssembly) | MPL-2.0 (CPython: PSF License) |
| pykakasi 2.3.0 (readings for the Japanese WordNet words that JMdict lacks) | GPL-3.0-or-later — shipped as its original wheel, which is its source; https://codeberg.org/miurahr/pykakasi |
| jaconv, Deprecated (used by pykakasi) | MIT |
| wrapt (used by Deprecated) | BSD-2-Clause |

`scripts/make_dict_page.sh` copies these licence texts into the page's `licences/` folder.
pykakasi is used only as a conversion tool (in `tools/convert_wnjpn.py` and on the page); it is not part of the firmware.
