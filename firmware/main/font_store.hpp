#pragma once
// Japanese fonts for the UI.
//
// The built-in M5GFX font (lgfxJapanGothic) holds only 4,425 characters, so rare kanji show
// up as boxes. `idf.py flash` writes a larger font image (made by tools/make_font.py from
// Noto Sans JP) to the "font" partition; if it is there, it is memory-mapped and used in
// place of the built-in font. If the partition is empty, the built-in font is used.
#include <M5GFX.h>

namespace font_store {
// Maps the "font" partition. Returns how many sizes were found (0 = built-in font only).
int load();
// Font for one of the UI sizes (20, 24, 28, 40): the flash font if loaded, else the built-in one.
const lgfx::IFont* get(int size);
}
