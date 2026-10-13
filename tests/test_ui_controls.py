"""削除後に残すキー操作とダイアログの回帰テスト。

実際の ui.cpp の入力関数と keyboard.cpp の文字変換を抽出して実行する。
描画・検索・単語帳 I/O・電源などの呼び出し先は記録用のホスト境界に置換する。
実機の画面表示・タッチ認識・電源回路の確認を代替するものではない。
"""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, signature):
    start = source.index(signature + "\n{")
    # These file-scope functions put their closing brace in column zero.
    return source[start:source.index("\n}", start) + 2]


class UiControls(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        folder = Path(cls.temp.name)
        cls.binary = folder / "ui-controls"
        ui = (ROOT / "firmware/main/ui.cpp").read_text()
        keyboard = (ROOT / "firmware/main/keyboard.cpp").read_text()
        declarations = ui[ui.index("constexpr int SCREEN_W"):ui.index("// ---- colours")]
        declarations += ui[ui.index("struct Line {"):ui.index("// One full-screen canvas")]
        declarations += ui[ui.index("enum : uint8_t { DIRTY_HEADER"):ui.index("int dictCount()")]
        key_map = keyboard[keyboard.index("static const char kMap"):keyboard.index("// [book:9-to-char]")]
        functions = "\n".join(function(ui, signature) for signature in (
            "Dictionary* dictAt(int i)",
            "void onKeyDialog(const KeyEvent& ev)",
            "void onTouchDialog()",
            "void popUtf8(std::string& s)",
            "void switchDict(int delta)",
            "void onKey(const KeyEvent& ev)",
        ))
        source = folder / "ui-controls.cpp"
        # drawAll is a one-line function; preserve its actual definition as well.
        draw_all = next(line for line in ui.splitlines() if line.startswith("void drawAll()"))
        source.write_text(r'''
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "keyboard.hpp"
namespace lgfx { struct IFont {}; }
struct Dictionary {
    bool entry(uint32_t index, std::string& head, std::string& definition) {
        assert(index == 7);
        head = "color, colour"; definition = "色"; return true;
    }
};
''' + declarations + r'''
int searches = 0, layouts = 0, powerOffs = 0, savedRotation = 0;
void doSearch() { ++searches; }
void layoutBody() { ++layouts; }
void doPowerOff() { ++powerOffs; }
bool scrollBody(int) { return false; }
bool moveSelection(int) { return false; }
int bodyVisibleLines() { return 10; }
void settings_set_rotation(int rotation) { savedRotation = rotation; }
struct Rotation {
    int rotation = 3;
    int getRotation() { return rotation; }
    void setRotation(int value) { rotation = value; }
};
struct TouchDetail { bool clicked = false; bool wasClicked() { return clicked; } };
struct Touch { TouchDetail detail; TouchDetail getDetail() { return detail; } };
struct Device { Rotation Display; struct Touch Touch; } M5;
Rotation screenCv;
namespace wordbook {
    std::vector<std::string> toggled;
    bool toggle(const std::string& word) { toggled.push_back(word); return true; }
}
#define ESP_LOGI(...) ((void)0)
''' + ui[ui.index("int dictCount()"):ui.index("Dictionary* dictAt(int i)")]
            + draw_all + "\n" + key_map
            + function(keyboard, "char keyboard::toChar(uint8_t keycode, uint8_t modifier)")
            + "\n" + functions + r'''
void press(uint8_t code, uint8_t modifier = 0) {
    onKey({KeySource::USB, true, modifier, code});
}
int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string scenario = argv[1];
    std::vector<std::unique_ptr<Dictionary>> dictionaries;
    dictionaries.push_back(std::make_unique<Dictionary>());
    S.dicts = &dictionaries;
    if (scenario == "text") {
        press(0x04); press(0x05, hid::MOD_SHIFT);
        assert(S.query == "aB" && searches == 2);
        press(0x17, 0x01); press(0x17, 0x10); // Left and right Ctrl+T.
        assert(g_mode == Mode::Dict && S.query == "aB" && searches == 2);
        press(0x17);
        assert(S.query == "aBt" && searches == 3);
        press(hid::KEY_BACKSPACE);
        assert(S.query == "aB");
        press(hid::KEY_ESC);
        assert(S.query.empty() && g_mode == Mode::Dict);
    } else if (scenario == "wordbook") {
        press(hid::KEY_ENTER);
        assert(wordbook::toggled.empty()); // No result must not create a blank entry.
        S.results.push_back({0, 7});
        press(hid::KEY_ENTER); press(hid::KEY_KP_ENTER);
        assert(wordbook::toggled == std::vector<std::string>({"color, colour", "color, colour"}));
        assert(layouts == 2 && (g_dirty & DIRTY_FOOTER));
    } else if (scenario == "power") {
        S.query = "pocket";
        press(0x14, 0x01);
        assert(g_mode == Mode::PowerOff && powerOffs == 0);
        press(0x04); // Ordinary typing must not change the search in a dialog.
        assert(S.query == "pocket" && g_mode == Mode::PowerOff);
        press(hid::KEY_ESC);
        assert(g_mode == Mode::Dict && powerOffs == 0);
        press(0x14, 0x10); press(hid::KEY_ENTER);
        assert(powerOffs == 1);
        g_mode = Mode::Dict; // Start a separate confirmation; real power handling is stubbed.
        press(0x14, 0x01);
        press(hid::KEY_KP_ENTER);
        assert(powerOffs == 2);
    } else if (scenario == "credits-touch") {
        S.query = "pocket";
        press(0x0F, 0x01);
        assert(g_mode == Mode::Credits);
        press(hid::KEY_ENTER);
        assert(g_mode == Mode::Dict && powerOffs == 0);
        press(0x0F, 0x10); press(hid::KEY_ESC);
        assert(g_mode == Mode::Dict);
        for (auto mode : {Mode::Credits, Mode::PowerOff}) {
            g_mode = mode;
            M5.Touch.detail.clicked = false; onTouchDialog();
            assert(g_mode == mode);
            M5.Touch.detail.clicked = true; onTouchDialog();
            assert(g_mode == Mode::Dict && powerOffs == 0);
        }
        assert(S.query == "pocket" && wordbook::toggled.empty());
    } else {
        assert(false);
    }
}
''', encoding="utf-8")
        subprocess.run(["c++", "-std=c++17", "-fsanitize=address,undefined",
                        "-fno-omit-frame-pointer", "-I" + str(ROOT / "firmware/main"),
                        str(source), "-o", str(cls.binary)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_retained_input_and_dialog_controls(self):
        for scenario in ("text", "wordbook", "power", "credits-touch"):
            with self.subTest(scenario=scenario):
                subprocess.run([str(self.binary), scenario], check=True, env={
                    **os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
