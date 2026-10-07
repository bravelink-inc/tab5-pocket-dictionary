#include "ui.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <vector>
#include <M5Unified.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "app_config.hpp"
#include "keyboard.hpp"
#include "sdcard.hpp"
#include "settings.hpp"
#include "debug_console.hpp"
#include "dict_store.hpp"
#include "wordbook.hpp"
#include "font_store.hpp"
#include "esp_random.h"

static const char* TAG = "ui";

namespace {

// ---- layout (landscape 1280x720) --------------------------------------------
constexpr int SCREEN_W = 1280, SCREEN_H = 720;
constexpr int HEADER_H = 92, FOOTER_H = 36;
constexpr int CONTENT_Y = HEADER_H, CONTENT_H = SCREEN_H - HEADER_H - FOOTER_H;
constexpr int LIST_W = 420, ROW_H = 46, LIST_ROWS = CONTENT_H / ROW_H;
constexpr int BODY_X = LIST_W, BODY_W = SCREEN_W - LIST_W, BODY_PAD = 26;
constexpr int MAX_RESULTS = 500;
constexpr size_t MAX_QUERY = 40;

// ---- colours (RGB888) -------------------------------------------------------
constexpr uint32_t C_BG       = 0xFFFFFF;
constexpr uint32_t C_HEADER   = 0x1F3B5C;
constexpr uint32_t C_HEADER_T = 0xFFFFFF;
constexpr uint32_t C_HINT     = 0x9FB3C8;
constexpr uint32_t C_LIST_BG  = 0xF3F5F8;
constexpr uint32_t C_LIST_T   = 0x1B1F24;
constexpr uint32_t C_SEL_BG   = 0x2F6FD6;
constexpr uint32_t C_SEL_T    = 0xFFFFFF;
constexpr uint32_t C_DIV      = 0xD5DAE1;
constexpr uint32_t C_HEAD     = 0x1F3B5C;
constexpr uint32_t C_TEXT     = 0x1B1F24;
constexpr uint32_t C_NUM      = 0x2F6FD6;
constexpr uint32_t C_FOOTER   = 0xE9EDF2;
constexpr uint32_t C_FOOTER_T = 0x4A5563;
constexpr uint32_t C_WARN     = 0xFFB4A2;
constexpr uint32_t C_HINT2    = 0x7A8794;

const lgfx::IFont* F_QUERY = &fonts::lgfxJapanGothic_40;
const lgfx::IFont* F_TITLE = &fonts::lgfxJapanGothic_24;
const lgfx::IFont* F_SMALL = &fonts::lgfxJapanGothic_20;
const lgfx::IFont* F_LIST  = &fonts::lgfxJapanGothic_28;
const lgfx::IFont* F_HEAD  = &fonts::lgfxJapanGothic_40;
const lgfx::IFont* F_BODY  = &fonts::lgfxJapanGothic_28;
const lgfx::IFont* F_BIG   = &fonts::lgfxJapanGothic_40;   // quiz prompts and results

// Switch to the flash fonts if font_store loaded them (see font_store.hpp).
void applyFonts()
{
    F_QUERY = font_store::get(40);
    F_TITLE = font_store::get(24);
    F_SMALL = font_store::get(20);
    F_LIST  = font_store::get(28);
    F_HEAD  = font_store::get(40);
    F_BODY  = font_store::get(28);
    F_BIG   = font_store::get(40);
}

struct Line {
    std::string text;
    const lgfx::IFont* font;
    uint32_t color;
    int x;   // indent
    int h;   // line height
};

struct Hit {
    uint8_t dict;
    uint32_t idx;
};

constexpr int ALL_DICTS = -1;

struct State {
    std::vector<std::unique_ptr<Dictionary>>* dicts = nullptr;
    int active = ALL_DICTS;   // index into dicts, or ALL_DICTS
    std::string query;
    std::vector<Hit> results;
    bool noMatch = false;
    int selected = 0, listTop = 0;
    std::vector<Line> body;
    int bodyTop = 0;
    int dragAccBody = 0, dragAccList = 0;
    bool kbdI2c = false, kbdUsb = false;
} S;

// One full-screen canvas allocated in the panel's native orientation (720x1280) and
// rotated to landscape via LGFX_Sprite::setRotation(). Pushing it while the display is
// temporarily in rotation 0 is a plain memcpy (~30 ms), whereas pushing landscape sprites
// through M5GFX's rotated copy costs 140-770 ms because of cache thrashing.
M5Canvas screenCv(&M5.Display);

enum : uint8_t { DIRTY_HEADER = 1, DIRTY_LIST = 2, DIRTY_BODY = 4, DIRTY_FOOTER = 8, DIRTY_ALL = 15 };
uint8_t g_dirty = 0;
enum class Mode { Dict, QuizMenu, Quiz, QuizResult, PowerOff };
Mode g_mode = Mode::Dict;
void invalidate(uint8_t mask) { g_dirty |= mask; }

int dictCount() { return S.dicts ? static_cast<int>(S.dicts->size()) : 0; }

Dictionary* dictAt(int i)
{
    if (i < 0 || i >= dictCount()) return nullptr;
    return (*S.dicts)[i].get();
}

// Dictionary of the selected hit (or the active one when there are no results).
Dictionary* activeDict()
{
    if (!S.results.empty() && S.selected < static_cast<int>(S.results.size())) return dictAt(S.results[S.selected].dict);
    if (S.active == ALL_DICTS) return dictAt(0);
    return dictAt(S.active);
}

bool allMode() { return S.active == ALL_DICTS && dictCount() > 1; }

size_t utf8len(unsigned char c)
{
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    return 4;
}

// [book:8-wrap]
// Word-wrap `text` into `out`. ASCII words are kept whole where possible; CJK text
// breaks anywhere. `firstIndent`/`restIndent` give a hanging indent.
void wrapInto(LovyanGFX& g, const lgfx::IFont* font, const std::string& text, int width,
              int firstIndent, int restIndent, uint32_t color, std::vector<Line>& out)
{
    g.setFont(font);
    const int lineH = g.fontHeight() + 8;
    std::string line;
    int lineW = 0, lastSpace = -1;
    bool first = true;
    auto avail = [&]() { return width - (first ? firstIndent : restIndent); };
    auto flush = [&](const std::string& s) {
        out.push_back(Line{s, font, color, first ? firstIndent : restIndent, lineH});
        first = false;
    };

    size_t i = 0;
    while (i < text.size()) {
        size_t n = std::min(utf8len(static_cast<unsigned char>(text[i])), text.size() - i);
        std::string ch = text.substr(i, n);
        const int cw = g.textWidth(ch.c_str());
        if (!line.empty() && lineW + cw > avail()) {
            const bool inWord = (n == 1) && std::isalnum(static_cast<unsigned char>(ch[0]));
            if (inWord && lastSpace > 0) {
                std::string rest = line.substr(lastSpace + 1);
                flush(line.substr(0, lastSpace));
                line = rest;
                lineW = g.textWidth(line.c_str());
            } else {
                flush(line);
                line.clear();
                lineW = 0;
            }
            auto p = line.rfind(' ');
            lastSpace = (p == std::string::npos) ? -1 : static_cast<int>(p);
            if (ch == " ") { i += n; continue; }
        }
        if (ch == " ") lastSpace = static_cast<int>(line.size());
        line += ch;
        lineW += cw;
        i += n;
    }
    if (!line.empty() || out.empty()) flush(line);
}
// [/book:8-wrap]

std::vector<std::string> splitSenses(const std::string& def)
{
    std::vector<std::string> senses;
    size_t pos = 0;
    while (pos <= def.size()) {
        size_t next = def.find(" / ", pos);
        std::string s = def.substr(pos, next == std::string::npos ? std::string::npos : next - pos);
        if (!s.empty()) senses.push_back(s);
        if (next == std::string::npos) break;
        pos = next + 3;
    }
    return senses;
}

// [book:8-layout-body]
// ---- model ------------------------------------------------------------------
void layoutBody()
{
    S.body.clear();
    S.bodyTop = 0;
    if (S.results.empty()) return;
    const Hit hit = S.results[S.selected];
    Dictionary* d = dictAt(hit.dict);
    if (!d) return;
    std::string head, def;
    if (!d->entry(hit.idx, head, def)) return;

    const int width = BODY_W - 2 * BODY_PAD;
    wrapInto(screenCv, F_HEAD, wordbook::contains(head) ? head + "　〔単語帳〕" : head, width, 0, 0, C_HEAD, S.body);
    if (allMode()) S.body.push_back(Line{d->title(), F_SMALL, C_HINT2, 0, 26});
    S.body.push_back(Line{"", F_BODY, C_TEXT, 0, 14});   // spacer (divider drawn here)

    auto senses = splitSenses(def);
    screenCv.setFont(F_BODY);
    if (senses.size() <= 1) {
        wrapInto(screenCv, F_BODY, def, width, 0, 0, C_TEXT, S.body);
    } else {
        const int numW = screenCv.textWidth("00. ");
        for (size_t k = 0; k < senses.size(); ++k) {
            std::string numbered = std::to_string(k + 1) + ". " + senses[k];
            wrapInto(screenCv, F_BODY, numbered, width, 0, numW, C_TEXT, S.body);
            S.body.push_back(Line{"", F_BODY, C_TEXT, 0, 6});
        }
    }
}
// [/book:8-layout-body]

// [book:11-collect]
// Collect up to `limit` prefix matches from one dictionary (or its neighbourhood when
// nothing matches and `neighbours` is set).
void collect(int di, const std::string& nq, size_t limit, bool neighbours, std::vector<Hit>& out)
{
    Dictionary* d = dictAt(di);
    if (!d) return;
    if (nq.empty()) {
        for (uint32_t i = 0; i < d->count() && out.size() < limit; ++i) out.push_back(Hit{(uint8_t)di, i});
        return;
    }
    const uint32_t lb = d->lowerBound(nq.c_str());
    for (uint32_t i = lb; i < d->count() && out.size() < limit && d->hasPrefix(i, nq.c_str()); ++i) out.push_back(Hit{(uint8_t)di, i});
    if (out.empty() && neighbours)
        for (uint32_t i = lb; i < d->count() && out.size() < static_cast<size_t>(LIST_ROWS); ++i) out.push_back(Hit{(uint8_t)di, i});
}
// [/book:11-collect]

// [book:11-merge]
// Merge per-dictionary hit lists (each sorted by key) into one list sorted by key.
void mergeHits(std::vector<std::vector<Hit>>& lists, size_t limit, std::vector<Hit>& out)
{
    std::vector<size_t> pos(lists.size(), 0);
    while (out.size() < limit) {
        int best = -1;
        const char* bestKey = nullptr;
        for (size_t l = 0; l < lists.size(); ++l) {
            if (pos[l] >= lists[l].size()) continue;
            const Hit& h = lists[l][pos[l]];
            const char* k = dictAt(h.dict)->key(h.idx);
            if (best < 0 || strcmp(k, bestKey) < 0) { best = static_cast<int>(l); bestKey = k; }
        }
        if (best < 0) break;
        out.push_back(lists[best][pos[best]++]);
    }
}
// [/book:11-merge]

// [book:8-do-search]
void doSearch()
{
    S.results.clear();
    S.noMatch = false;
    S.selected = 0;
    S.listTop = 0;
    const std::string nq = Dictionary::normalize(S.query);
    if (dictCount() == 0) { layoutBody(); return; }

    if (!allMode()) {
        const int di = (S.active == ALL_DICTS) ? 0 : S.active;
        collect(di, nq, MAX_RESULTS, false, S.results);
        if (S.results.empty() && !nq.empty()) { S.noMatch = true; collect(di, nq, MAX_RESULTS, true, S.results); }
    } else {
        std::vector<std::vector<Hit>> lists(dictCount());
        bool any = false;
        for (int di = 0; di < dictCount(); ++di) { collect(di, nq, MAX_RESULTS / 2, false, lists[di]); any |= !lists[di].empty(); }
        if (!any && !nq.empty()) {
            S.noMatch = true;
            for (int di = 0; di < dictCount(); ++di) collect(di, nq, MAX_RESULTS / 2, true, lists[di]);
        }
        mergeHits(lists, MAX_RESULTS, S.results);
    }
    layoutBody();
}
// [/book:8-do-search]

int bodyVisibleLines()
{
    int y = BODY_PAD, n = 0;
    for (size_t i = S.bodyTop; i < S.body.size(); ++i) {
        if (y + S.body[i].h > CONTENT_H - BODY_PAD) break;
        y += S.body[i].h;
        ++n;
    }
    return n;
}

bool scrollBody(int lines)
{
    const int maxTop = std::max(0, static_cast<int>(S.body.size()) - bodyVisibleLines());
    int t = std::clamp(S.bodyTop + lines, 0, std::max(0, static_cast<int>(S.body.size()) - 1));
    if (lines > 0 && S.bodyTop >= maxTop) return false;
    if (t == S.bodyTop) return false;
    S.bodyTop = t;
    return true;
}

void ensureSelectedVisible()
{
    if (S.selected < S.listTop) S.listTop = S.selected;
    if (S.selected >= S.listTop + LIST_ROWS) S.listTop = S.selected - LIST_ROWS + 1;
    S.listTop = std::max(0, S.listTop);
}

bool moveSelection(int delta)
{
    if (S.results.empty()) return false;
    int n = std::clamp(S.selected + delta, 0, static_cast<int>(S.results.size()) - 1);
    if (n == S.selected) return false;
    S.selected = n;
    ensureSelectedVisible();
    layoutBody();
    return true;
}

// ---- drawing ----------------------------------------------------------------
std::string ellipsize(LovyanGFX& g, std::string s, int width)
{
    if (g.textWidth(s.c_str()) <= width) return s;
    while (!s.empty() && g.textWidth((s + "…").c_str()) > width) {
        size_t cut = s.size() - 1;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
        s.erase(cut);
    }
    return s + "…";
}

// Each draw*() paints one pane into screenCv (clipped to the pane); present() pushes the canvas.
struct PaneClip {
    PaneClip(int x, int y, int w, int h) { screenCv.setClipRect(x, y, w, h); }
    ~PaneClip() { screenCv.clearClipRect(); }
};

void drawHeader()
{
    auto& g = screenCv;
    PaneClip clip(0, 0, SCREEN_W, HEADER_H);
    g.fillRect(0, 0, SCREEN_W, HEADER_H, C_HEADER);
    g.setTextDatum(textdatum_t::top_left);

    g.setFont(F_QUERY);
    if (S.query.empty()) {
        g.setTextColor(C_HINT);
        g.drawString("英単語を入力してください", 24, 22);
    } else {
        g.setTextColor(C_HEADER_T);
        std::string shown = ellipsize(g, S.query, 760);
        g.drawString(shown.c_str(), 24, 22);
        int w = g.textWidth(shown.c_str());
        g.fillRect(24 + w + 4, 24, 4, g.fontHeight() - 4, C_HEADER_T);
        if (S.noMatch) {
            g.setFont(F_TITLE);
            g.setTextColor(C_WARN);
            g.drawString("該当なし（近い語を表示）", 24 + w + 24, 34);
        }
    }

    g.setTextDatum(textdatum_t::top_right);
    g.setFont(F_TITLE);
    g.setTextColor(C_HEADER_T);
    std::string title;
    if (dictCount() == 0) {
        title = "辞書がありません";
    } else if (allMode()) {
        uint32_t total = 0;
        for (int i = 0; i < dictCount(); ++i) total += dictAt(i)->count();
        title = "すべての辞書 (" + std::to_string(dictCount()) + " 冊, " + std::to_string(total) + " 語)";
    } else {
        Dictionary* d = dictAt(S.active == ALL_DICTS ? 0 : S.active);
        title = d->title() + "  (" + std::to_string(d->count()) + " 語)";
        if (dictCount() > 1) title = "[" + std::to_string(S.active + 1) + "/" + std::to_string(dictCount()) + "] " + title;
    }
    g.drawString(title.c_str(), SCREEN_W - 20, 14);

    g.setFont(F_SMALL);
    g.setTextColor(S.kbdI2c || S.kbdUsb ? C_HINT : C_WARN);
    std::string kbd = S.kbdI2c ? "KB: Tab5 Keyboard" : (S.kbdUsb ? "KB: USB" : "キーボード未接続");
    if (S.kbdI2c && S.kbdUsb) kbd = "KB: Tab5 Keyboard + USB";
    kbd += sdcard_mounted() ? "   SD: OK" : "   SD: なし";
    kbd += "   単語帳 " + std::to_string(wordbook::words().size()) + " 語";
    g.drawString(kbd.c_str(), SCREEN_W - 20, 54);
}

// [book:8-draw-list]
void drawList()
{
    auto& g = screenCv;
    PaneClip clip(0, CONTENT_Y, LIST_W, CONTENT_H);
    g.fillRect(0, CONTENT_Y, LIST_W, CONTENT_H, C_LIST_BG);
    g.setTextDatum(textdatum_t::middle_left);
    const bool tags = allMode();
    std::string head, def;
    for (int r = 0; r < LIST_ROWS; ++r) {
        const int idx = S.listTop + r;
        if (idx >= static_cast<int>(S.results.size())) break;
        const Hit hit = S.results[idx];
        Dictionary* d = dictAt(hit.dict);
        if (!d) break;
        const int y = CONTENT_Y + r * ROW_H;
        const bool sel = (idx == S.selected);
        if (sel) g.fillRect(0, y, LIST_W, ROW_H, C_SEL_BG);
        if (!d->entry(hit.idx, head, def)) head = d->key(hit.idx);
        int textW = LIST_W - 32;
        if (tags) {
            g.setFont(F_SMALL);
            g.setTextDatum(textdatum_t::middle_right);
            g.setTextColor(sel ? C_SEL_T : C_NUM);
            g.drawString(d->tag().c_str(), LIST_W - 12, y + ROW_H / 2);
            textW -= g.textWidth(d->tag().c_str()) + 12;
            g.setTextDatum(textdatum_t::middle_left);
        }
        g.setFont(F_LIST);
        g.setTextColor(sel ? C_SEL_T : C_LIST_T);
        g.drawString(ellipsize(g, head, textW).c_str(), 16, y + ROW_H / 2);
        g.drawFastHLine(0, y + ROW_H - 1, LIST_W, C_DIV);
    }
    g.drawFastVLine(LIST_W - 1, CONTENT_Y, CONTENT_H, C_DIV);
}
// [/book:8-draw-list]

void drawBody()
{
    auto& g = screenCv;
    PaneClip clip(BODY_X, CONTENT_Y, BODY_W, CONTENT_H);
    g.fillRect(BODY_X, CONTENT_Y, BODY_W, CONTENT_H, C_BG);
    g.setTextDatum(textdatum_t::top_left);
    int y = BODY_PAD;
    size_t i = S.bodyTop;
    for (; i < S.body.size(); ++i) {
        const Line& L = S.body[i];
        if (y + L.h > CONTENT_H - BODY_PAD) break;
        if (L.text.empty()) {
            if (L.h >= 12) g.drawFastHLine(BODY_X + BODY_PAD, CONTENT_Y + y + L.h / 2, BODY_W - 2 * BODY_PAD, C_DIV);
        } else {
            g.setFont(L.font);
            g.setTextColor(L.color);
            g.drawString(L.text.c_str(), BODY_X + BODY_PAD + L.x, CONTENT_Y + y);
        }
        y += L.h;
    }
    g.setFont(F_SMALL);
    g.setTextColor(C_NUM);
    if (i < S.body.size()) {   // more below
        g.setTextDatum(textdatum_t::bottom_right);
        g.drawString("▼ → キーで続き", BODY_X + BODY_W - 12, CONTENT_Y + CONTENT_H - 4);
    }
    if (S.bodyTop > 0) {
        g.setTextDatum(textdatum_t::top_right);
        g.drawString("▲", BODY_X + BODY_W - 12, CONTENT_Y + 4);
    }
}

void drawFooter()
{
    auto& g = screenCv;
    PaneClip clip(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H);
    g.fillRect(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, C_FOOTER);
    g.setFont(F_SMALL);
    g.setTextColor(C_FOOTER_T);
    g.setTextDatum(textdatum_t::middle_left);
    g.drawString("↑↓ 選択  PgUp/Dn ページ  ←→ 説明  Tab 辞書  Enter 単語帳  Ctrl+T テスト  Esc クリア  Ctrl+R 反転  Ctrl+Q 電源",
                 16, SCREEN_H - FOOTER_H / 2);
}

// [book:10-present]
// Copy the canvas to the panel. The canvas already has the landscape rotation baked in,
// so the display is switched to rotation 0 for the push (straight row copies).
void present()
{
    auto& d = M5.Display;
    const int r = d.getRotation();
    d.setRotation(0);
    screenCv.pushSprite(0, 0);
    d.setRotation(r);
}
// [/book:10-present]

void drawQuizMenu(); void drawQuizQuestion(); void drawQuizResult(); void drawPowerOff();

// [book:10-render]
void render()
{
    if (!g_dirty) return;
    const int64_t t0 = esp_timer_get_time();
    if (g_mode == Mode::QuizMenu) { drawQuizMenu(); present(); g_dirty = 0; return; }
    if (g_mode == Mode::Quiz) { drawQuizQuestion(); present(); g_dirty = 0; return; }
    if (g_mode == Mode::QuizResult) { drawQuizResult(); present(); g_dirty = 0; return; }
    if (g_mode == Mode::PowerOff) { drawPowerOff(); present(); g_dirty = 0; return; }
    if (g_dirty & DIRTY_HEADER) drawHeader();
    if (g_dirty & DIRTY_LIST)   drawList();
    if (g_dirty & DIRTY_BODY)   drawBody();
    if (g_dirty & DIRTY_FOOTER) drawFooter();
    const int64_t t1 = esp_timer_get_time();
    present();
    const int64_t t2 = esp_timer_get_time();
    ESP_LOGD(TAG, "render mask=%02x draw %lld ms push %lld ms", g_dirty, (t1 - t0) / 1000, (t2 - t1) / 1000);
    g_dirty = 0;
}
// [/book:10-render]

void drawAll() { invalidate(DIRTY_ALL); }


// ---- 英単語テスト (quiz) -----------------------------------------------------
extern "C" const char _binary_2000_txt_start[];
extern "C" const char _binary_2000_txt_end[];
extern "C" const char _binary_ngsl_levels_txt_start[];
extern "C" const char _binary_ngsl_levels_txt_end[];
extern "C" const char _binary_kanji_quiz_txt_start[];
extern "C" const char _binary_kanji_quiz_txt_end[];

// What the round asks about.
enum class QuizKind : uint8_t { Word, Kanji };
// Word levels: 0 = frequent 2000 + wordbook, 1..3 = NGSL bands, 4 = wordbook only
constexpr int LEVEL_FREQ = 0, LEVEL_WORDBOOK = 4;


struct Question {
    std::string word, def;      // asked word (or kanji) and its full definition (or readings)
    std::string options[4];
    int correct = 0;
    int kind = 0;               // kanji quiz: 1 = on reading asked, 2 = kun reading asked
};

struct Quiz {
    static constexpr int COUNT = 10;
    QuizKind kind = QuizKind::Word;
    bool jaToEn = false;              // word quiz direction (toggled with Tab in the menu)
    int level = LEVEL_FREQ;           // word quiz source
    int gradeLo = 1, gradeHi = 2;     // kanji quiz grades (8 = junior high)
    int index = 0, score = 0, answered = -1;
    Question q;
    std::vector<std::pair<std::string, std::string>> wrong;   // (key, line for the result screen)
    std::vector<std::string> pool;
    std::vector<std::string> asked;   // words already used as questions in this round
} Z;

std::vector<std::string> g_freqWords;
std::vector<std::string> g_ngsl[4];   // [1..3] = NGSL bands

// One line of kanji_quiz.txt: kanji<TAB>grade<TAB>on/on<TAB>kun/kun. Parsed on demand so the
// 2,136 entries cost one small index instead of thousands of heap strings.
struct KanjiRef { const char* line; uint16_t len; uint8_t grade; };
std::vector<KanjiRef> g_kanji;

// Call fn(line, len) for each non-empty, non-comment line of an embedded text file.
template <class Fn>
void forEachLine(const char* begin, const char* end, Fn fn)
{
    const char* p = begin;
    while (p < end) {
        const char* e = p;
        while (e < end && *e != '\n' && *e != '\0') ++e;
        size_t n = e - p;
        if (n && p[n - 1] == '\r') --n;
        if (n && *p != '#') fn(p, n);
        p = e + 1;
    }
}

// [book:13-load-data]
void loadFreqWords()
{
    g_freqWords.clear();
    forEachLine(_binary_2000_txt_start, _binary_2000_txt_end,
                [](const char* l, size_t n) { g_freqWords.emplace_back(l, n); });
    for (auto& v : g_ngsl) v.clear();
    forEachLine(_binary_ngsl_levels_txt_start, _binary_ngsl_levels_txt_end, [](const char* l, size_t n) {
        const char* tab = static_cast<const char*>(memchr(l, '\t', n));
        if (!tab || tab + 1 >= l + n) return;
        const int level = tab[1] - '0';
        if (level >= 1 && level <= 3) g_ngsl[level].emplace_back(l, tab - l);
    });
    g_kanji.clear();
    forEachLine(_binary_kanji_quiz_txt_start, _binary_kanji_quiz_txt_end, [](const char* l, size_t n) {
        const char* tab = static_cast<const char*>(memchr(l, '\t', n));
        if (!tab) return;
        g_kanji.push_back(KanjiRef{l, static_cast<uint16_t>(n), static_cast<uint8_t>(atoi(tab + 1))});
    });
    ESP_LOGI(TAG, "quiz data: freq %u, ngsl %u/%u/%u, kanji %u", (unsigned)g_freqWords.size(),
             (unsigned)g_ngsl[1].size(), (unsigned)g_ngsl[2].size(), (unsigned)g_ngsl[3].size(), (unsigned)g_kanji.size());
}// [/book:13-load-data]


struct KanjiEntry { std::string ch; std::vector<std::string> on, kun; };

std::vector<std::string> splitSlash(const std::string& s)
{
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < s.size()) {
        size_t e = s.find('/', pos);
        if (e == std::string::npos) e = s.size();
        if (e > pos) out.push_back(s.substr(pos, e - pos));
        pos = e + 1;
    }
    return out;
}

KanjiEntry parseKanji(const KanjiRef& r)
{
    std::string line(r.line, r.len), f[4];
    size_t pos = 0;
    for (int i = 0; i < 4; ++i) {
        size_t e = line.find('\t', pos);
        f[i] = line.substr(pos, e == std::string::npos ? std::string::npos : e - pos);
        if (e == std::string::npos) break;
        pos = e + 1;
    }
    return KanjiEntry{f[0], splitSlash(f[2]), splitSlash(f[3])};
}

std::string kanjiReadings(const KanjiEntry& k)
{
    std::string s;
    auto join = [](const std::vector<std::string>& v) {
        std::string r;
        for (size_t i = 0; i < v.size(); ++i) { if (i) r += "、"; r += v[i]; }
        return r;
    };
    if (!k.on.empty()) s += "音: " + join(k.on);
    if (!k.kun.empty()) { if (!s.empty()) s += "　"; s += "訓: " + join(k.kun); }
    return s;
}

Dictionary* quizDict()
{
    for (int i = 0; i < dictCount(); ++i) if (dictAt(i)->source() == "flash") return dictAt(i);
    return dictAt(0);
}

bool lookupExact(Dictionary* d, const std::string& word, std::string& head, std::string& def)
{
    if (!d) return false;
    const std::string k = Dictionary::normalize(word);
    const uint32_t i = d->lowerBound(k.c_str());
    if (i >= d->count() || strcmp(d->key(i), k.c_str()) != 0) return false;
    return d->entry(i, head, def);
}

std::string truncateUtf8(const std::string& s, size_t maxChars)
{
    size_t i = 0, chars = 0;
    while (i < s.size() && chars < maxChars) { i += utf8len(static_cast<unsigned char>(s[i])); ++chars; }
    return i < s.size() ? s.substr(0, i) + "…" : s;
}

// First sense of an EJDict definition, shortened for an answer button.
std::string firstSense(const std::string& def)
{
    std::string sense = def.substr(0, def.find(" / "));
    if (!sense.empty() && sense[0] == '=') {   // "=other" cross reference: use the next sense if any
        size_t p = def.find(" / ");
        if (p != std::string::npos) sense = def.substr(p + 3, def.find(" / ", p + 3) - (p + 3));
    }
    return truncateUtf8(sense, 44);
}

uint32_t rnd(uint32_t n) { return n ? esp_random() % n : 0; }

bool buildPool()
{
    Z.pool.clear();
    if (Z.level == LEVEL_FREQ || Z.level == LEVEL_WORDBOOK)
        for (const auto& w : wordbook::words()) Z.pool.push_back(w);
    if (Z.level == LEVEL_FREQ) for (const auto& w : g_freqWords) Z.pool.push_back(w);
    if (Z.level >= 1 && Z.level <= 3) Z.pool = g_ngsl[Z.level];
    return Z.pool.size() >= 4;
}

int kanjiCount(int lo, int hi)
{
    int n = 0;
    for (const auto& k : g_kanji) n += (k.grade >= lo && k.grade <= hi);
    return n;
}

// Pick a random pool word that exists in the quiz dictionary.
// `fresh`: the word will be asked, so skip words already asked in this round. Without this,
// a word missed a moment ago (and just added to the wordbook) comes back in the same round.
bool pickWord(std::string& word, std::string& def, const std::string& avoid, bool fresh = false)
{
    Dictionary* d = quizDict();
    for (int tries = 0; tries < 40; ++tries) {
        const auto& wb = wordbook::words();
        std::string w;
        if (!wb.empty() && Z.level == LEVEL_FREQ && rnd(2) == 0) w = wb[rnd(wb.size())];
        else w = Z.pool[rnd(Z.pool.size())];
        if (w == avoid) continue;
        if (fresh && std::find(Z.asked.begin(), Z.asked.end(), w) != Z.asked.end()) continue;
        std::string head;
        if (lookupExact(d, w, head, def)) { word = w; return true; }
    }
    return false;
}

// Kanji reading question: show one kanji, answer one of its on (katakana) or kun (hiragana)
// readings. Distractors are readings of the same kind from other kanji of the same grades.
// [book:13-kanji-question]
bool nextKanjiQuestion()
{
    std::vector<uint32_t> idx;
    for (uint32_t i = 0; i < g_kanji.size(); ++i)
        if (g_kanji[i].grade >= Z.gradeLo && g_kanji[i].grade <= Z.gradeHi) idx.push_back(i);
    if (idx.size() < 8) return false;
    KanjiEntry k;
    for (int tries = 0; tries < 60; ++tries) {
        k = parseKanji(g_kanji[idx[rnd(idx.size())]]);
        if (std::find(Z.asked.begin(), Z.asked.end(), k.ch) == Z.asked.end()) break;
        k.ch.clear();
    }
    if (k.ch.empty()) return false;
    const bool useOn = k.kun.empty() || (!k.on.empty() && rnd(2) == 0);
    const auto& mine = useOn ? k.on : k.kun;
    Question q;
    q.word = k.ch;
    q.def = kanjiReadings(k);
    q.kind = useOn ? 1 : 2;
    std::string opts[4];
    opts[0] = mine[rnd(mine.size())];
    for (int i = 1; i < 4; ++i) {
        for (int guard = 0; guard < 60; ++guard) {
            KanjiEntry o = parseKanji(g_kanji[idx[rnd(idx.size())]]);
            const auto& theirs = useOn ? o.on : o.kun;
            if (o.ch == k.ch || theirs.empty()) continue;
            const std::string cand = theirs[rnd(theirs.size())];
            bool clash = std::find(k.on.begin(), k.on.end(), cand) != k.on.end() ||
                         std::find(k.kun.begin(), k.kun.end(), cand) != k.kun.end();
            for (int j = 1; j < i; ++j) clash |= (opts[j] == cand);
            if (!clash) { opts[i] = cand; break; }
        }
        if (opts[i].empty()) return false;
    }
    q.correct = rnd(4);
    int src = 1;
    for (int i = 0; i < 4; ++i) q.options[i] = (i == q.correct) ? opts[0] : opts[src++];
    Z.q = q;
    Z.asked.push_back(q.word);
    Z.answered = -1;
    return true;
}// [/book:13-kanji-question]


// [book:13-next-question]
bool nextQuestion()
{
    if (Z.kind == QuizKind::Kanji) return nextKanjiQuestion();
    Question q;
    if (!pickWord(q.word, q.def, "", true)) return false;
    std::string opts[4];
    opts[0] = Z.jaToEn ? q.word : firstSense(q.def);
    for (int i = 1; i < 4; ++i) {
        std::string w, def;
        int guard = 0;
        do {
            if (!pickWord(w, def, q.word)) return false;
            opts[i] = Z.jaToEn ? w : firstSense(def);
        } while (++guard < 10 && (opts[i] == opts[0] || opts[i] == opts[1] || (i > 2 && opts[i] == opts[2])));
    }
    q.correct = rnd(4);
    int src = 1;
    for (int i = 0; i < 4; ++i) q.options[i] = (i == q.correct) ? opts[0] : opts[src++];
    Z.q = q;
    Z.asked.push_back(q.word);
    Z.answered = -1;
    return true;
}
// [/book:13-next-question]

void startQuiz()
{
    Z.index = 0; Z.score = 0; Z.answered = -1;
    Z.wrong.clear();
    Z.asked.clear();
    const bool ready = (Z.kind == QuizKind::Kanji) ? true : buildPool();
    if (!ready || !nextQuestion()) { g_mode = Mode::QuizMenu; invalidate(DIRTY_ALL); return; }
    g_mode = Mode::Quiz;
    invalidate(DIRTY_ALL);
}

// [book:13-answer]
void answerQuiz(int k)
{
    if (Z.answered >= 0) return;
    Z.answered = k;
    if (k == Z.q.correct) ++Z.score;
    else {
        const bool seen = std::any_of(Z.wrong.begin(), Z.wrong.end(),
                                      [](const auto& w) { return w.first == Z.q.word; });
        if (!seen) {
            const std::string line = Z.q.word + "　" +
                (Z.kind == QuizKind::Kanji ? Z.q.def : firstSense(Z.q.def));
            Z.wrong.emplace_back(Z.q.word, line);
        }
        if (Z.kind == QuizKind::Word) wordbook::add(Z.q.word);   // the wordbook holds English words
    }
    invalidate(DIRTY_ALL);
}
// [/book:13-answer]

void advanceQuiz()
{
    if (++Z.index >= Quiz::COUNT || !nextQuestion()) g_mode = Mode::QuizResult;
    invalidate(DIRTY_ALL);
}

// Draw wrapped text; returns the y after the last line.
int drawTextBlock(const lgfx::IFont* font, uint32_t color, const std::string& text, int x, int y, int width, int maxLines = 99)
{
    std::vector<Line> lines;
    wrapInto(screenCv, font, text, width, 0, 0, color, lines);
    screenCv.setTextDatum(textdatum_t::top_left);
    int n = 0;
    for (const Line& L : lines) {
        if (n++ >= maxLines) break;
        screenCv.setFont(L.font);
        screenCv.setTextColor(L.color);
        screenCv.drawString(L.text.c_str(), x, y);
        y += L.h;
    }
    return y;
}

void drawQuizChrome(const char* title, const char* footer)
{
    auto& g = screenCv;
    g.fillScreen(C_BG);
    g.fillRect(0, 0, SCREEN_W, HEADER_H, C_HEADER);
    g.setTextDatum(textdatum_t::middle_left);
    g.setFont(F_QUERY); g.setTextColor(C_HEADER_T);
    g.drawString(title, 24, HEADER_H / 2);
    g.fillRect(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, C_FOOTER);
    g.setFont(F_SMALL); g.setTextColor(C_FOOTER_T);
    g.drawString(footer, 16, SCREEN_H - FOOTER_H / 2);
}

// Menu rows: keys 1-5 = word quiz sources, 6-9 = kanji grade ranges.
struct MenuItem { const char* label; const char* note; };
const MenuItem kWordItems[5] = {
    {"頻出 2000 語 + 単語帳", ""},
    {"基礎（NGSL 1〜1000 位）", "中学英語の目安"},
    {"標準（NGSL 1001〜2000 位）", "高校基礎の目安"},
    {"発展（NGSL 2001 位〜）", "高校〜大学入試の目安"},
    {"単語帳だけ", ""},
};
const struct { const char* label; int lo, hi; } kKanjiItems[4] = {
    {"小学 1〜2 年", 1, 2}, {"小学 3〜4 年", 3, 4}, {"小学 5〜6 年", 5, 6}, {"中学（常用漢字）", 8, 8},
};
constexpr int MENU_Y0 = 170, MENU_ROW = 76, MENU_COL2 = 700;

bool menuItemEnabled(int i)
{
    if (i == 4) return wordbook::words().size() >= 4;
    if (i >= 1 && i <= 3) return g_ngsl[i].size() >= 4;
    return true;
}

void drawQuizMenu()
{
    auto& g = screenCv;
    drawQuizChrome("テスト", "1〜9 で開始    Tab 英→和 / 和→英    Esc 辞書に戻る");
    g.setTextDatum(textdatum_t::top_left);
    g.setFont(F_TITLE); g.setTextColor(C_HEAD);
    g.drawString(Z.jaToEn ? "英単語（和 → 英）" : "英単語（英 → 和）", 60, 112);
    g.drawString("漢字の読み", MENU_COL2, 112);
    for (int i = 0; i < 5; ++i) {
        const int y = MENU_Y0 + i * MENU_ROW;
        g.setFont(F_BODY); g.setTextColor(menuItemEnabled(i) ? C_TEXT : C_DIV);
        g.drawString((std::to_string(i + 1) + "  " + kWordItems[i].label).c_str(), 60, y);
        g.setFont(F_SMALL); g.setTextColor(C_HINT2);
        if (*kWordItems[i].note) g.drawString(kWordItems[i].note, 104, y + 36);
    }
    for (int i = 0; i < 4; ++i) {
        const int y = MENU_Y0 + i * MENU_ROW;
        g.setFont(F_BODY); g.setTextColor(C_TEXT);
        g.drawString((std::to_string(i + 6) + "  " + kKanjiItems[i].label).c_str(), MENU_COL2, y);
        g.setFont(F_SMALL); g.setTextColor(C_HINT2);
        g.drawString((std::to_string(kanjiCount(kKanjiItems[i].lo, kKanjiItems[i].hi)) + " 字").c_str(), MENU_COL2 + 44, y + 36);
    }
    std::string info = "単語帳 " + std::to_string(wordbook::words().size()) +
                       " 語（辞書で Enter、英単語テストで間違えた語も入ります）。10 問 1 セット、1〜4 かタッチで回答";
    drawTextBlock(F_SMALL, C_HINT2, info, 60, SCREEN_H - FOOTER_H - 60, SCREEN_W - 120, 2);
}

// Start the round chosen by menu item 0..8.
// [book:13-menu-item]
void startMenuItem(int item)
{
    if (item < 5) {
        if (!menuItemEnabled(item)) return;
        Z.kind = QuizKind::Word;
        Z.level = item;
    } else if (item < 9) {
        Z.kind = QuizKind::Kanji;
        Z.gradeLo = kKanjiItems[item - 5].lo;
        Z.gradeHi = kKanjiItems[item - 5].hi;
    } else {
        return;
    }
    startQuiz();
}// [/book:13-menu-item]


constexpr int OPT_Y0 = 286, OPT_H = 68, OPT_GAP = 8;

void drawQuizQuestion()
{
    auto& g = screenCv;
    std::string title = "第 " + std::to_string(Z.index + 1) + " 問 / " + std::to_string(Quiz::COUNT) + "　　正解 " + std::to_string(Z.score);
    drawQuizChrome(title.c_str(), Z.answered < 0 ? "1〜4 またはタッチで回答    Esc 中止" : "Enter / Space 次へ    Esc 中止");

    const int x = 60, w = SCREEN_W - 2 * x;
    const bool kanji = Z.kind == QuizKind::Kanji;
    if (kanji) {
        g.setTextDatum(textdatum_t::top_left);
        g.setFont(F_BIG); g.setTextColor(C_HEAD);
        g.setTextSize(3);
        g.drawString(Z.q.word.c_str(), x, 104);
        g.setTextSize(1);
    } else {
        std::string prompt = Z.jaToEn ? firstSense(Z.q.def) : Z.q.word;
        drawTextBlock(Z.jaToEn ? F_HEAD : F_BIG, C_HEAD, prompt, x, 120, w, 2);
    }
    g.setFont(F_SMALL); g.setTextColor(C_HINT2);
    g.setTextDatum(textdatum_t::top_left);
    const char* ask = kanji ? (Z.q.kind == 1 ? "この漢字の音読みは？（カタカナ）" : "この漢字の訓読みは？（ひらがな、（ ）は送りがな）")
                            : (Z.jaToEn ? "この意味の英単語は？" : "この単語の意味は？");
    g.drawString(ask, kanji ? x + 160 : x, kanji ? 200 : 244);

    for (int i = 0; i < 4; ++i) {
        const int y = OPT_Y0 + i * (OPT_H + OPT_GAP);
        uint32_t bg = C_LIST_BG, fg = C_TEXT;
        if (Z.answered >= 0) {
            if (i == Z.q.correct) { bg = 0x2E9E5B; fg = 0xFFFFFF; }
            else if (i == Z.answered) { bg = 0xD64545; fg = 0xFFFFFF; }
        }
        g.fillRoundRect(x, y, w, OPT_H, 10, bg);
        g.setTextDatum(textdatum_t::middle_left);
        g.setFont(F_HEAD); g.setTextColor(fg);
        g.drawString(std::to_string(i + 1).c_str(), x + 24, y + OPT_H / 2);
        g.setFont(F_BODY);
        g.drawString(ellipsize(g, Z.q.options[i], w - 110).c_str(), x + 80, y + OPT_H / 2);
    }
    if (Z.answered >= 0) {
        const int y = OPT_Y0 + 4 * (OPT_H + OPT_GAP) + 4;
        g.setTextDatum(textdatum_t::top_left);
        g.setFont(F_SMALL); g.setTextColor(Z.answered == Z.q.correct ? (uint32_t)0x2E9E5B : (uint32_t)0xD64545);
        g.drawString(Z.answered == Z.q.correct ? "正解！" : (kanji ? "不正解" : "不正解 → 単語帳へ"), x, y);
        drawTextBlock(F_SMALL, C_TEXT, Z.q.word + ": " + Z.q.def, x + 250, y, w - 250, 2);
    }
}

void drawPowerOff()
{
    auto& g = screenCv;
    drawQuizChrome("電源オフ", "Enter 電源を切る    Esc 戻る");
    g.setTextDatum(textdatum_t::top_left);
    g.setFont(F_BIG); g.setTextColor(C_HEAD);
    g.drawString("電源を切りますか？", 80, 200);
    drawTextBlock(F_BODY, C_HINT2, "Enter で電源が切れます。次に使うときは本体の電源ボタンを押してください。", 80, 300, SCREEN_W - 160);
}

// [book:14-power-off]
void doPowerOff()
{
    ESP_LOGI(TAG, "power off");
    screenCv.fillScreen(C_HEADER);
    screenCv.setTextDatum(textdatum_t::middle_center);
    screenCv.setFont(F_BIG); screenCv.setTextColor(C_HEADER_T);
    screenCv.drawString("電源を切っています...", SCREEN_W / 2, SCREEN_H / 2);
    present();
    vTaskDelay(pdMS_TO_TICKS(600));
    M5.Display.setBrightness(0);
    M5.Power.powerOff();
    vTaskDelay(pdMS_TO_TICKS(2000));
    // still here: power off is not possible (e.g. running from USB without the power circuit)
    M5.Display.setBrightness(cfg::DISPLAY_BRIGHTNESS);
    g_mode = Mode::Dict;
    drawAll();
}
// [/book:14-power-off]

void drawQuizResult()
{
    auto& g = screenCv;
    drawQuizChrome("結果", "Enter もう一度    Esc 辞書に戻る");
    const int x = 80;
    g.setTextDatum(textdatum_t::top_left);
    g.setFont(F_BIG); g.setTextColor(C_HEAD);
    std::string score = std::to_string(Z.index) + " 問中 " + std::to_string(Z.score) + " 問正解";
    g.drawString(score.c_str(), x, 140);
    int y = 230;
    if (Z.wrong.empty()) {
        drawTextBlock(F_BODY, C_TEXT, "全問正解です！", x, y, SCREEN_W - 2 * x);
    } else {
        drawTextBlock(F_BODY, C_HINT2, Z.kind == QuizKind::Kanji ? "間違えた漢字:" : "間違えた単語（単語帳に入れました）:",
                      x, y, SCREEN_W - 2 * x);
        y += 50;
        for (const auto& w : Z.wrong) {
            y = drawTextBlock(F_BODY, C_TEXT, w.second, x, y, SCREEN_W - 2 * x, 1);
            if (y > SCREEN_H - FOOTER_H - 40) break;
        }
    }
}

void onKeyQuiz(const KeyEvent& ev)
{
    const uint8_t k = ev.keycode;
    // digit 0..8 for keys 1..9 (number row or keypad)
    const int digit = (k >= 0x1E && k <= 0x26) ? (k - 0x1E) : (k >= 0x59 && k <= 0x61) ? (k - 0x59) : -1;
    switch (g_mode) {
    case Mode::QuizMenu:
        if (k == hid::KEY_ESC) { g_mode = Mode::Dict; drawAll(); }
        else if (k == hid::KEY_TAB) { Z.jaToEn = !Z.jaToEn; invalidate(DIRTY_ALL); }
        else if (digit >= 0) startMenuItem(digit);
        break;
    case Mode::Quiz:
        if (k == hid::KEY_ESC) { g_mode = Mode::QuizMenu; invalidate(DIRTY_ALL); }
        else if (Z.answered < 0 && digit >= 0 && digit < 4) answerQuiz(digit);
        else if (Z.answered >= 0 && (k == hid::KEY_ENTER || k == hid::KEY_KP_ENTER || k == hid::KEY_SPACE || k == hid::KEY_RIGHT)) advanceQuiz();
        break;
    case Mode::QuizResult:
        if (k == hid::KEY_ESC) { g_mode = Mode::Dict; drawAll(); }
        else if (k == hid::KEY_ENTER || k == hid::KEY_KP_ENTER || k == hid::KEY_SPACE) { g_mode = Mode::QuizMenu; invalidate(DIRTY_ALL); }
        break;
    case Mode::PowerOff:
        if (k == hid::KEY_ESC) { g_mode = Mode::Dict; drawAll(); }
        else if (k == hid::KEY_ENTER || k == hid::KEY_KP_ENTER) doPowerOff();
        break;
    default: break;
    }
}

void onTouchQuiz()
{
    auto t = M5.Touch.getDetail();
    if (!t.wasClicked()) return;
    if (g_mode == Mode::Quiz) {
        if (Z.answered < 0) {
            for (int i = 0; i < 4; ++i) {
                const int y = OPT_Y0 + i * (OPT_H + OPT_GAP);
                if (t.y >= y && t.y < y + OPT_H) { answerQuiz(i); return; }
            }
        } else if (t.y > HEADER_H) {
            advanceQuiz();
        }
    } else if (g_mode == Mode::QuizMenu) {
        if (t.y < MENU_Y0 - 10) { Z.jaToEn = !Z.jaToEn; invalidate(DIRTY_ALL); return; }   // tap the headings
        const int row = (t.y - (MENU_Y0 - 10)) / MENU_ROW;
        if (t.x < MENU_COL2 - 20 && row >= 0 && row < 5) startMenuItem(row);
        else if (t.x >= MENU_COL2 - 20 && row >= 0 && row < 4) startMenuItem(5 + row);
    } else if (g_mode == Mode::QuizResult && t.y > HEADER_H) {
        g_mode = Mode::QuizMenu; invalidate(DIRTY_ALL);
    } else if (g_mode == Mode::PowerOff) {
        g_mode = Mode::Dict; drawAll();
    }
}

// ---- input ------------------------------------------------------------------
void popUtf8(std::string& s)
{
    if (s.empty()) return;
    size_t cut = s.size() - 1;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    s.erase(cut);
}

void switchDict(int delta)
{
    const int n = dictCount();
    if (n < 2) return;
    // cycle over n+1 states: ALL, 0, 1, ..., n-1
    int state = (S.active == ALL_DICTS) ? 0 : S.active + 1;
    state = ((state + delta) % (n + 1) + n + 1) % (n + 1);
    S.active = (state == 0) ? ALL_DICTS : state - 1;
    doSearch();
    drawAll();
}

void reloadDictionaries()
{
    ESP_LOGI(TAG, "reloading dictionaries");
    S.body.clear();
    S.results.clear();
    dict_store::loadAll(*S.dicts);
    if (S.active >= dictCount()) S.active = ALL_DICTS;
    doSearch();
    drawAll();
}

void onKeyQuiz(const KeyEvent& ev);

// [book:8-on-key]
void onKey(const KeyEvent& ev)
{
    const uint8_t k = ev.keycode;
    const bool ctrl = ev.modifier & hid::MOD_CTRL;
    const bool shift = ev.modifier & hid::MOD_SHIFT;

    if (g_mode != Mode::Dict) { onKeyQuiz(ev); return; }
    if (ctrl && k == 0x14) {   // Ctrl+Q: power off (with confirmation)
        g_mode = Mode::PowerOff;
        invalidate(DIRTY_ALL);
        return;
    }
    if (ctrl && k == 0x17) {   // Ctrl+T: quiz
        g_mode = Mode::QuizMenu;
        invalidate(DIRTY_ALL);
        return;
    }

    switch (k) {
    case hid::KEY_BACKSPACE:
        if (S.query.empty()) return;
        if (ctrl) S.query.clear(); else popUtf8(S.query);
        doSearch(); drawAll();
        return;
    case hid::KEY_ESC:
        if (S.query.empty()) return;
        S.query.clear(); doSearch(); drawAll();
        return;
    case hid::KEY_UP:
        if (ctrl) { if (scrollBody(-1)) invalidate(DIRTY_BODY); }
        else if (moveSelection(-1)) { invalidate(DIRTY_LIST | DIRTY_BODY); }
        return;
    case hid::KEY_DOWN:
        if (ctrl) { if (scrollBody(+1)) invalidate(DIRTY_BODY); }
        else if (moveSelection(+1)) { invalidate(DIRTY_LIST | DIRTY_BODY); }
        return;
    case hid::KEY_PAGEUP:
        if (moveSelection(-LIST_ROWS)) { invalidate(DIRTY_LIST | DIRTY_BODY); }
        return;
    case hid::KEY_PAGEDOWN:
        if (moveSelection(+LIST_ROWS)) { invalidate(DIRTY_LIST | DIRTY_BODY); }
        return;
    case hid::KEY_HOME:
        if (moveSelection(-1000000)) { invalidate(DIRTY_LIST | DIRTY_BODY); }
        return;
    case hid::KEY_END:
        if (moveSelection(+1000000)) { invalidate(DIRTY_LIST | DIRTY_BODY); }
        return;
    case hid::KEY_LEFT:
        if (scrollBody(-std::max(1, bodyVisibleLines() - 1))) invalidate(DIRTY_BODY);
        return;
    case hid::KEY_RIGHT:
        if (scrollBody(+std::max(1, bodyVisibleLines() - 1))) invalidate(DIRTY_BODY);
        return;
    case hid::KEY_TAB:
        switchDict(shift ? -1 : +1);
        return;
    case hid::KEY_ENTER:
    case hid::KEY_KP_ENTER: {   // toggle the selected word in the 単語帳
        if (S.results.empty()) return;
        Dictionary* d = dictAt(S.results[S.selected].dict);
        std::string head, def;
        if (d && d->entry(S.results[S.selected].idx, head, def)) {
            wordbook::toggle(head);
            layoutBody();
            invalidate(DIRTY_HEADER | DIRTY_BODY);
        }
        return;
    }
    default:
        break;
    }

    if (ctrl && k == 0x15) {   // Ctrl+R: flip the screen 180 degrees (persisted)
        const int r = (M5.Display.getRotation() == 1) ? 3 : 1;
        M5.Display.setRotation(r);
        screenCv.setRotation(r);
        settings_set_rotation(r);
        ESP_LOGI(TAG, "rotation -> %d", r);
        drawAll();
        return;
    }
    if (ctrl && k == 0x18) {   // Ctrl+U: clear
        if (!S.query.empty()) { S.query.clear(); doSearch(); drawAll(); }
        return;
    }
    const char c = keyboard::toChar(k, ev.modifier);
    if (c && S.query.size() < MAX_QUERY) {
        if (c == ' ' && (S.query.empty() || S.query.back() == ' ')) return;
        S.query.push_back(c);
        doSearch();
        drawAll();
    }
}
// [/book:8-on-key]

void onTouch()
{
    auto t = M5.Touch.getDetail();
    if (t.wasClicked()) {
        S.dragAccBody = S.dragAccList = 0;
        if (t.x < LIST_W && t.y >= CONTENT_Y && t.y < CONTENT_Y + CONTENT_H) {
            const int idx = S.listTop + (t.y - CONTENT_Y) / ROW_H;
            if (idx < static_cast<int>(S.results.size()) && idx != S.selected) {
                S.selected = idx;
                layoutBody();
                invalidate(DIRTY_LIST | DIRTY_BODY);
            }
        } else if (t.y < HEADER_H && t.x > SCREEN_W / 2) {
            switchDict(+1);
        }
        return;
    }
    if (t.isDragging() || t.isFlicking()) {
        if (t.x >= BODY_X) {
            S.dragAccBody += t.deltaY();
            bool changed = false;
            const int step = 36;
            while (S.dragAccBody <= -step) { changed |= scrollBody(+1); S.dragAccBody += step; }
            while (S.dragAccBody >= step)  { changed |= scrollBody(-1); S.dragAccBody -= step; }
            if (changed) invalidate(DIRTY_BODY);
        } else {
            S.dragAccList += t.deltaY();
            bool changed = false;
            const int maxTop = std::max(0, static_cast<int>(S.results.size()) - LIST_ROWS);
            while (S.dragAccList <= -ROW_H) { if (S.listTop < maxTop) { ++S.listTop; changed = true; } S.dragAccList += ROW_H; }
            while (S.dragAccList >= ROW_H)  { if (S.listTop > 0) { --S.listTop; changed = true; } S.dragAccList -= ROW_H; }
            if (changed) invalidate(DIRTY_LIST);
        }
    }
    if (t.wasReleased()) S.dragAccBody = S.dragAccList = 0;
}

// [book:10-create-canvas]
bool createScreenCanvas()
{
    auto& d = M5.Display;
    screenCv.setPsram(true);
    screenCv.setColorDepth(d.getColorDepth());
    // allocate in the panel's native orientation, then rotate logically
    const int r = d.getRotation();
    const int nw = (r & 1) ? d.height() : d.width();
    const int nh = (r & 1) ? d.width() : d.height();
    if (!screenCv.createSprite(nw, nh)) {
        ESP_LOGE(TAG, "cannot allocate screen canvas (%dx%d)", nw, nh);
        return false;
    }
    screenCv.setRotation(r);
    ESP_LOGI(TAG, "screen canvas %dx%d (native %dx%d, rotation %d)", screenCv.width(), screenCv.height(), nw, nh, r);
    return true;
}
// [/book:10-create-canvas]

}  // namespace

void ui::showSplash(const char* message)
{
    auto& d = M5.Display;
    d.startWrite();
    d.fillScreen(C_HEADER);
    d.setTextDatum(textdatum_t::middle_center);
    d.setTextColor(C_HEADER_T);
    d.setFont(&fonts::lgfxJapanGothic_40);
    d.drawString("Pocket Dictionary", d.width() / 2, d.height() / 2 - 40);
    d.setFont(&fonts::lgfxJapanGothic_24);
    d.setTextColor(C_HINT);
    d.drawString(message, d.width() / 2, d.height() / 2 + 30);
    d.endWrite();
}

// [book:8-run]
void ui::run(std::vector<std::unique_ptr<Dictionary>>& dicts)
{
    applyFonts();
    S.dicts = &dicts;
    S.active = ALL_DICTS;
    loadFreqWords();
    createScreenCanvas();
    S.kbdI2c = keyboard::i2cConnected();
    S.kbdUsb = keyboard::usbConnected();
    doSearch();
    drawAll();
    render();
    ESP_LOGI(TAG, "ready");

    struct { bool active = false; KeyEvent ev{}; int64_t next = 0; } repeat;
    int64_t lastStatus = 0;

    for (;;) {
        KeyEvent ev;
        // Handle every queued key before redrawing so fast typing costs one redraw
        // (bounded so a stuck key cannot starve touch handling and rendering).
        bool first = true;
        int handled = 0;
        while (handled < 32 && keyboard::wait(ev, first ? 20 : 0)) {
            first = false;
            ++handled;
            if (ev.source == KeySource::USB) {
                if (ev.pressed) {
                    repeat.active = true;
                    repeat.ev = ev;
                    repeat.next = esp_timer_get_time() + 400 * 1000;
                } else if (repeat.active && repeat.ev.keycode == ev.keycode) {
                    repeat.active = false;
                }
            }
            if (ev.pressed) { ESP_LOGD(TAG, "key src=%d mod=%02x code=%02x", (int)ev.source, ev.modifier, ev.keycode); onKey(ev); }
        }
        const int64_t now = esp_timer_get_time();
        if (repeat.active && now >= repeat.next) {
            onKey(repeat.ev);
            repeat.next = now + 50 * 1000;
        }

        M5.update();
        if (g_mode == Mode::Dict) onTouch(); else onTouchQuiz();
        if (dict_store::reloadPending()) reloadDictionaries();
        render();
        if (debug_screenshot_pending()) debug_dump_screen();

        if (now - lastStatus > 500 * 1000) {
            lastStatus = now;
            const bool i2c = keyboard::i2cConnected(), usb = keyboard::usbConnected();
            if (i2c != S.kbdI2c || usb != S.kbdUsb) {
                S.kbdI2c = i2c;
                S.kbdUsb = usb;
                invalidate(DIRTY_HEADER);
            }
        }
    }
}
// [/book:8-run]
