#!/usr/bin/env bash
# Assemble the 「辞書を作る」 page (web/make-dict.html) into OUTDIR. The page builds the SD-card
# dictionaries and quiz lists in the reader's browser from the official data files the reader
# downloads, so no dictionary data is redistributed here; only the tools, Pyodide and the
# libraries they need are copied in (fetched at build time, no CDN at run time).
# Usage: scripts/make_dict_page.sh OUTDIR
set -euo pipefail
OUT="${1:?output directory}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PYODIDE_VER=0.28.3
WHEELS="pykakasi==2.3.0 jaconv==0.5.0 deprecated==1.3.1"
TOOLS="build_dict.py convert_jmdict.py convert_kanjidic.py convert_wnjpn.py romaji.py make_quiz_data.py make_notices.py"
mkdir -p "$OUT"/{pyodide,wheels,tools,ejdict,licences}

cp "$ROOT"/web/make-dict.html "$ROOT"/web/make-dict-worker.js "$ROOT"/web/make_dict.py "$OUT/"
for t in $TOOLS; do cp "$ROOT/tools/$t" "$OUT/tools/"; done
cp "$ROOT"/third_party/ejdict/src/*.txt "$OUT/ejdict/"            # CC0, used to filter the NGSL list
cp "$ROOT"/third_party/wnja/license.txt "$ROOT"/third_party/wnja/princeton_wordnet_license.txt "$OUT/licences/"

echo "== Pyodide $PYODIDE_VER"
TMP=$(mktemp -d)
curl -sfL "https://registry.npmjs.org/pyodide/-/pyodide-$PYODIDE_VER.tgz" | tar xz -C "$TMP"
for f in pyodide.js pyodide.asm.js pyodide.asm.wasm python_stdlib.zip pyodide-lock.json; do cp "$TMP/package/$f" "$OUT/pyodide/"; done
curl -sfL -o "$OUT/licences/Pyodide_LICENSE.txt" "https://raw.githubusercontent.com/pyodide/pyodide/$PYODIDE_VER/LICENSE"
# wrapt (needed by Deprecated, needed by pykakasi) is a compiled Pyodide package
WRAPT=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["packages"]["wrapt"]["file_name"])' "$OUT/pyodide/pyodide-lock.json")
curl -sfL -o "$OUT/pyodide/$WRAPT" "https://cdn.jsdelivr.net/pyodide/v$PYODIDE_VER/full/$WRAPT"

echo "== wheels: $WHEELS"
python3 -m pip download -q --no-deps --only-binary=:all: --python-version 3.13 -d "$OUT/wheels" $WHEELS
for w in "$OUT"/wheels/*.whl "$OUT/pyodide/$WRAPT"; do
  name=$(basename "$w" | cut -d- -f1)
  unzip -Z1 "$w" | grep -iE '\.dist-info/(licenses/)?(LICENSE|COPYING)[^/]*$' | head -1 | while read -r lic; do
    unzip -p "$w" "$lic" > "$OUT/licences/${name}_LICENSE.txt"
  done
done
rm -rf "$TMP"

python3 - "$OUT" "$WRAPT" <<'PY'
import json, os, sys
out, wrapt = sys.argv[1], sys.argv[2]
ls = lambda d, ext: sorted(f for f in os.listdir(os.path.join(out, d)) if f.endswith(ext))
json.dump({"pyodidePackages": ["wrapt"], "wheels": ls("wheels", ".whl"), "tools": ls("tools", ".py"),
           "ejdict": ls("ejdict", ".txt"), "licences": ["license.txt", "princeton_wordnet_license.txt"]},
          open(os.path.join(out, "make-dict-files.json"), "w"), indent=1)
items = "".join(f'<li><a href="{f}">{f}</a></li>' for f in ls("licences", ".txt"))
open(os.path.join(out, "licences", "index.html"), "w", encoding="utf-8").write(
    '<!doctype html><meta charset="utf-8"><title>licences</title><h1>このページで使っている部品とデータの条文</h1>'
    '<p>pykakasi（GPL-3.0）のソースは、wheels フォルダの .whl（ZIP 形式）の中身と、'
    '<a href="https://codeberg.org/miurahr/pykakasi">https://codeberg.org/miurahr/pykakasi</a> にあります。</p>'
    f'<ul>{items}</ul>')
PY
du -sh "$OUT"
