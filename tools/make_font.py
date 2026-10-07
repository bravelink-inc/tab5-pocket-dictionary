#!/usr/bin/env python3
"""make_font.py - build a full-coverage Japanese font image for the "font" flash partition.

The built-in M5GFX font (lgfxJapanGothic) holds only 4,425 characters, so many kanji in
KANJIDIC2 and some dictionary headwords show up as boxes. This tool renders a TrueType /
OpenType font (default: Noto Sans JP, SIL Open Font License) at the pixel sizes the UI uses,
keeps only the characters the dictionaries actually need, converts each size to the u8g2
bitmap format that M5GFX draws, and packs them into one image. `idf.py flash` writes the
image to the "font" partition and the firmware uses it in place (memory-mapped, no copy).

Pipeline: font file --(freetype-py)--> BDF --(u8g2 bdfconv)--> C array --> u8g2 bytes --> PFN1 image

PFN1 image layout (little endian):
  0   "PFN1"
  4   uint32 count
  8   count x { uint32 pixel_size, uint32 offset, uint32 length }   offset from the image start
  ... u8g2 font data, each starting on a 4-byte boundary

Requires: pip install freetype-py fonttools numpy ; bdfconv from https://github.com/olikraus/u8g2
          (tools/font/bdfconv, build with: make CFLAGS="-O2 -w")
Usage:    python tools/make_font.py --font NotoSansJP-Regular.otf --bdfconv path/to/bdfconv
          -> third_party/notosansjp/jp_fonts.pfn
"""
import argparse, ast, glob, os, re, struct, subprocess, sys, tempfile
import freetype
import numpy as np
from fontTools.ttLib import TTFont

try:
    sys.stdout.reconfigure(encoding="utf-8")
except Exception:
    pass

SIZES = (20, 24, 28, 40)          # the pixel sizes ui.cpp uses
ASCENT, DESCENT = 0.88, 0.12      # line box, as a fraction of the size (same as IPAGothic): line height and baseline
INK_ASCENT, INK_DESCENT = 1.05, 0.30   # glyph pixels kept: Latin descenders (g, p, y) reach 0.25, accented capitals 1.0


def needed_chars(root):
    """Every non-ASCII BMP character in the dictionary sources, quiz data and UI strings,
    plus all of JIS X 0208 so that ordinary Japanese text is always covered."""
    chars = set(range(0x20, 0x7F)) | set(range(0xA0, 0x100))
    files = (glob.glob(os.path.join(root, "data/tsv/*.tsv")) +
             glob.glob(os.path.join(root, "third_party/**/*.txt"), recursive=True) +
             glob.glob(os.path.join(root, "firmware/main/*.cpp")))
    for f in files:
        with open(f, encoding="utf-8", errors="ignore") as fh:
            for line in fh:
                chars.update(ord(c) for c in line if 0x80 <= ord(c) <= 0xFFFF)
    for hi in range(0xA1, 0xFF):
        for lo in range(0xA1, 0xFF):
            try:
                chars.add(ord(bytes([hi, lo]).decode("euc_jp")))
            except UnicodeDecodeError:
                pass
    return chars


def write_bdf(font_path, size, codes, path):
    face = freetype.Face(font_path)
    face.set_char_size(size * 64, 0, 72, 72)
    top = round(size * INK_ASCENT)        # rows above the baseline that are kept
    bottom = round(size * INK_DESCENT)    # rows below the baseline that are kept
    glyphs = []
    for code in sorted(codes):
        if face.get_char_index(code) == 0:
            continue
        face.load_char(code, freetype.FT_LOAD_RENDER | freetype.FT_LOAD_TARGET_NORMAL)
        g = face.glyph
        bm = g.bitmap
        adv = (g.advance.x + 32) >> 6
        w, h, left, btop = bm.width, bm.rows, g.bitmap_left, g.bitmap_top
        if w == 0 or h == 0:
            glyphs.append((code, adv, 0, 0, 0, 0, []))
            continue
        img = np.frombuffer(bytes(bm.buffer), dtype=np.uint8).reshape(h, bm.pitch)[:, :w] >= 110
        # row r sits at y = btop - 1 - r above the baseline; keep only the band [-bottom, top).
        # The band is wider than the line box; set_line_box() keeps the line height unchanged.
        r0 = max(0, btop - top)
        r1 = min(h, btop + bottom)
        img = img[r0:r1]
        if img.size == 0 or not img.any():
            glyphs.append((code, adv, 0, 0, 0, 0, []))
            continue
        ymax = btop - 1 - r0
        ymin = ymax - img.shape[0] + 1
        packed = np.packbits(img, axis=1)
        hexrows = [row.tobytes().hex().upper() for row in packed]
        glyphs.append((code, adv, w, ymax - ymin + 1, left, ymin, hexrows))
    with open(path, "w", encoding="ascii") as f:
        f.write(f"STARTFONT 2.1\nFONT -tab5-jp-medium-r-normal--{size}-{size*10}-72-72-p-0-iso10646-1\n")
        f.write(f"SIZE {size} 72 72\nFONTBOUNDINGBOX {size * 2} {top + bottom} 0 {-bottom}\n")
        f.write(f"STARTPROPERTIES 2\nFONT_ASCENT {top}\nFONT_DESCENT {bottom}\nENDPROPERTIES\n")
        f.write(f"CHARS {len(glyphs)}\n")
        for code, adv, w, h, xo, yo, hexrows in glyphs:
            f.write(f"STARTCHAR U+{code:04X}\nENCODING {code}\nSWIDTH {adv * 1000 // size} 0\n"
                    f"DWIDTH {adv} 0\nBBX {w} {h} {xo} {yo}\nBITMAP\n")
            f.write("".join(r + "\n" for r in hexrows))
            f.write("ENDCHAR\n")
        f.write("ENDFONT\n")
    return len(glyphs)


def c_array_to_bytes(c_path):
    """bdfconv writes the font as C string literals; turn them back into raw bytes."""
    src = open(c_path, encoding="latin-1").read()
    body = src[src.index("=") + 1:src.rindex(";")]
    out = b""
    for lit in re.findall(r'"((?:[^"\\]|\\.)*)"', body, re.S):
        out += ast.literal_eval('b"' + lit + '"')
    # The C compiler adds a NUL after the last literal, and u8g2 needs it: the glyph list
    # ends with a 2-byte encoding 0. Without it, looking up a missing character runs into
    # whatever follows in the image.
    return out + b"\0"


def set_line_box(data, size):
    """bdfconv sizes a font to its tallest glyph. Give it the IPAGothic line box instead, so the
    line height and baseline stay those of the built-in font. M5GFX reads only these two bytes
    for the line metrics and draws the pixels outside the box (descenders, accents) as they are,
    into the gap between lines."""
    data = bytearray(data)
    top, bottom = round(size * ASCENT), round(size * DESCENT)
    data[10] = top + bottom          # max_char_height = line height
    data[12] = -bottom & 0xFF        # y_offset: baseline = line height + y_offset
    return bytes(data)


def pack(blobs):
    """blobs: [(pixel_size, bytes)] -> one PFN1 image."""
    head = 8 + 12 * len(blobs)
    offset = (head + 3) & ~3
    table, body = b"", b""
    for size, data in blobs:
        table += struct.pack("<III", size, offset + len(body), len(data))
        body += data + bytes(-len(data) % 4)
    return b"PFN1" + struct.pack("<I", len(blobs)) + table + bytes(offset - head) + body


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--font", required=True, help="TrueType/OpenType font (e.g. NotoSansJP-Regular.otf)")
    ap.add_argument("--bdfconv", required=True, help="path to the u8g2 bdfconv executable")
    ap.add_argument("--root", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
    ap.add_argument("-o", "--out", default=None, help="output image (default: third_party/notosansjp/jp_fonts.pfn)")
    a = ap.parse_args()
    out = a.out or os.path.join(a.root, "third_party", "notosansjp", "jp_fonts.pfn")
    have = set(TTFont(a.font, lazy=True).getBestCmap())
    want = needed_chars(a.root)
    codes = want & have
    print(f"characters wanted {len(want)}, in the font {len(codes)}", flush=True)
    blobs = []
    with tempfile.TemporaryDirectory() as tmp:
        for size in SIZES:
            bdf = os.path.join(tmp, f"jp_{size}.bdf")
            n = write_bdf(a.font, size, codes, bdf)
            c = os.path.join(tmp, f"jp_{size}.c")
            subprocess.run([a.bdfconv, "-b", "0", "-f", "1", "-m", "32-65535", bdf,
                            "-o", c, "-n", f"jp_{size}"], check=True, stdout=subprocess.DEVNULL)
            data = set_line_box(c_array_to_bytes(c), size)
            blobs.append((size, data))
            print(f"  {size} px: {n} glyphs, {len(data) / 1024:.0f} KB, line height {data[10]} px", flush=True)
    image = pack(blobs)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, "wb") as f:
        f.write(image)
    print(f"{out}: {len(image) / 1048576:.2f} MB (the \"font\" partition must be at least this big)")


if __name__ == "__main__":
    main()
