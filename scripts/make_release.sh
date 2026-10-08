#!/usr/bin/env bash
# Build everything a reader needs without a development environment:
#   release/<version>/web/                         browser flashing page (ESP Web Tools) + firmware parts
#   release/<version>/tab5-pocket-dictionary-<version>-firmware.bin   recovery image at 0x0; resets NVS
#   release/<version>/web/make-dict.html            「辞書を作る」: the reader builds the SD dictionaries in the browser
# The SD dictionaries (JMdict, KANJIDIC2, Japanese WordNet) and the quiz lists (NGSL, KANJIDIC2) are
# NOT distributed: the reader downloads the official files and converts them on their own PC.
# Usage: scripts/make_release.sh v1.0      (run inside an ESP-IDF shell)
set -euo pipefail
VER="${1:?version, e.g. v1.0}"
[[ "$VER" =~ ^v[0-9][A-Za-z0-9._-]*$ ]] || { echo "invalid version: use v1.0 or v1.0-rc1" >&2; exit 1; }
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/release/$VER"
EWT_VER=10.4.0
# everything the page links to sits next to it, so the folder works as is (GitHub Pages or a local server)
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
# licences of the libraries linked into the firmware binary (Apache-2.0 / MIT ask for them with binaries)
L="$OUT/web/firmware/licences"; mkdir -p "$L"
cp "$IDF_PATH/LICENSE" "$L/ESP-IDF_LICENSE.txt"
cp "$IDF_PATH/components/freertos/FreeRTOS-Kernel/LICENSE.md" "$L/FreeRTOS-Kernel_LICENSE.txt"
for d in "$ROOT"/firmware/managed_components/*/; do cp "$d/LICENSE" "$L/$(basename "$d")_LICENSE.txt"; done
( cd "$L" && { echo '<!doctype html><meta charset="utf-8"><title>licences</title><h1>ファームウェアに含まれる部品の条文</h1><ul>'
  for f in *.txt; do echo "<li><a href=\"$f\">$f</a></li>"; done; echo '</ul>'; } > index.html )

echo "== recovery image (write at 0x0; resets settings and the NVS wordbook)"
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

echo "== 辞書を作る page"
"$ROOT/scripts/make_dict_page.sh" "$OUT/web" >/dev/null

echo "== page"
sed -e "s|__VERSION__|$VER|g" -e "s|__FW_BIN__|$FW_BIN|g" "$ROOT/web/index.html" > "$OUT/web/index.html"

echo "== checksums"
( cd "$OUT" && shasum -a 256 *.bin > SHA256SUMS.txt && cat SHA256SUMS.txt )
cp "$OUT/SHA256SUMS.txt" "$OUT/web/"   # next to the downloads on the page
du -sh "$OUT"/* "$OUT/web"
