#!/usr/bin/env bash
# Build everything a reader needs without a development environment:
#   release/<version>/web/                         browser flashing page (ESP Web Tools) + firmware parts
#   release/<version>/tab5-pocket-dictionary-<version>-firmware.bin   single image, write at 0x0
#   release/<version>/tab5-pocket-dictionary-<version>-sd-dictionaries.zip   SD card dictionaries
# Usage: scripts/make_release.sh v1.0      (run inside an ESP-IDF shell; needs data/sd/dict/*.pdc)
set -euo pipefail
VER="${1:?version, e.g. v1.0}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/release/$VER"
EWT_VER=10.4.0
# everything the page links to sits next to it, so the folder works as is (GitHub Pages or a local server)
SD_ZIP="tab5-pocket-dictionary-$VER-sd-dictionaries.zip"
FW_BIN="tab5-pocket-dictionary-$VER-firmware.bin"
rm -rf "$OUT" && mkdir -p "$OUT/web/firmware"

echo "== build firmware"
( cd "$ROOT/firmware" && idf.py build >/dev/null )
B="$ROOT/firmware/build"
cp "$B/bootloader/bootloader.bin" "$B/partition_table/partition-table.bin" "$B/pocketdictionary.bin" "$OUT/web/firmware/"
cp "$ROOT/data/ejdict.pdc" "$OUT/web/firmware/ejdict.pdc"
cp "$ROOT/third_party/notosansjp/jp_fonts.pfn" "$OUT/web/firmware/jp_fonts.pfn"
cp "$ROOT/third_party/notosansjp/OFL.txt" "$OUT/web/firmware/OFL.txt"
cp "$ROOT/THIRD_PARTY_NOTICES.md" "$ROOT/third_party/ipafont/IPA_Font_License_Agreement_v1.0.txt" "$OUT/web/firmware/"

echo "== single image (write at 0x0)"
python -m esptool --chip esp32p4 merge_bin -o "$OUT/tab5-pocket-dictionary-$VER-firmware.bin" \
  --flash_mode dio --flash_size 16MB \
  0x2000 "$B/bootloader/bootloader.bin" 0x10000 "$B/partition_table/partition-table.bin" \
  0x20000 "$B/pocketdictionary.bin" 0x400000 "$ROOT/data/ejdict.pdc" 0xA80000 "$ROOT/third_party/notosansjp/jp_fonts.pfn" >/dev/null
cp "$OUT/$FW_BIN" "$OUT/web/$FW_BIN"

echo "== ESP Web Tools manifest"
cat > "$OUT/web/manifest.json" <<JSON
{
  "name": "Tab5 電子辞書",
  "version": "$VER",
  "new_install_prompt_erase": false,
  "new_install_improv_wait_time": 0,
  "builds": [
    {
      "chipFamily": "ESP32-P4",
      "parts": [
        { "path": "firmware/bootloader.bin", "offset": 8192 },
        { "path": "firmware/partition-table.bin", "offset": 65536 },
        { "path": "firmware/pocketdictionary.bin", "offset": 131072 },
        { "path": "firmware/ejdict.pdc", "offset": 4194304 },
        { "path": "firmware/jp_fonts.pfn", "offset": 11010048 }
      ]
    }
  ]
}
JSON

echo "== vendor ESP Web Tools $EWT_VER (no CDN at run time)"
TMP=$(mktemp -d)
curl -sL "https://registry.npmjs.org/esp-web-tools/-/esp-web-tools-$EWT_VER.tgz" | tar xz -C "$TMP"
cp -R "$TMP/package/dist/web" "$OUT/web/esp-web-tools"
cp "$TMP/package/LICENSE" "$OUT/web/esp-web-tools/LICENSE" 2>/dev/null || true
rm -rf "$TMP"

echo "== SD card dictionaries"
ZIPDIR="$OUT/sd"
mkdir -p "$ZIPDIR/dict"
cp "$ROOT"/data/sd/dict/*.pdc "$ZIPDIR/dict/"
cp "$ROOT/THIRD_PARTY_NOTICES.md" "$ZIPDIR/"
cp "$ROOT/third_party/ipafont/IPA_Font_License_Agreement_v1.0.txt" "$ZIPDIR/"
cp "$ROOT/third_party/notosansjp/OFL.txt" "$ZIPDIR/"
cp "$ROOT/third_party/wnja/license.txt" "$ZIPDIR/Japanese_WordNet_License.txt"
cat > "$ZIPDIR/はじめにお読みください.txt" <<TXT
Tab5 電子辞書 追加辞書セット（$VER）

microSD カード（32 GB 以下、FAT32 でフォーマット済み）のいちばん上に、
この中の「dict」フォルダをそのままコピーしてください。
Tab5 に挿して電源を入れ直すと、次の 4 冊が使えるようになります。

  10_jmdict_waei.pdc     JMdict 和英（ローマ字で引く）
  20_jmdict_eiwa.pdc     JMdict 英和（逆引き）
  30_kanjidic2.pdc       KANJIDIC2 漢字辞典
  40_wnjpn_kokugo.pdc    日本語 WordNet 国語辞典

ライセンス: JMdict / KANJIDIC2 は EDRDG の CC BY-SA 4.0、日本語 WordNet は Japanese WordNet License です（同梱の条文を参照）。
ファームウェアに入っている日本語フォントは IPA ゴシック由来（IPA フォントライセンス v1.0）と
Noto Sans JP 由来（SIL Open Font License 1.1）です（同梱の条文を参照）。
詳しくは THIRD_PARTY_NOTICES.md を見てください。
TXT
# Python's zipfile marks non-ASCII names as UTF-8 (Info-ZIP zip on macOS does not), so
# 「はじめにお読みください.txt」 keeps its name when Windows Explorer extracts the ZIP.
rm -f "$OUT/$SD_ZIP"
python -c 'import shutil, sys; shutil.make_archive(sys.argv[1], "zip", sys.argv[2])' "$OUT/${SD_ZIP%.zip}" "$ZIPDIR"
rm -rf "$ZIPDIR"
cp "$OUT/$SD_ZIP" "$OUT/web/$SD_ZIP"
SD_MB=$(( $(stat -f%z "$OUT/tab5-pocket-dictionary-$VER-sd-dictionaries.zip" 2>/dev/null || stat -c%s "$OUT/tab5-pocket-dictionary-$VER-sd-dictionaries.zip") / 1048576 ))

echo "== page"
sed -e "s|__VERSION__|$VER|g" -e "s|__SD_ZIP_URL__|$SD_ZIP|g" -e "s|__SD_ZIP_MB__|$SD_MB|g" -e "s|__FW_BIN__|$FW_BIN|g" "$ROOT/web/index.html" > "$OUT/web/index.html"

echo "== checksums"
( cd "$OUT" && shasum -a 256 *.bin *.zip > SHA256SUMS.txt && cat SHA256SUMS.txt )
cp "$OUT/SHA256SUMS.txt" "$OUT/web/"   # next to the downloads on the page
du -sh "$OUT"/* "$OUT/web"
