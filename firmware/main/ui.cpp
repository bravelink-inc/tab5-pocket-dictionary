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

// Switch to the flash fonts if font_store loaded them (see font_store.hpp).
void applyFonts()
{
    F_QUERY = font_store::get(40);
    F_TITLE = font_store::get(24);
    F_SMALL = font_store::get(20);
    F_LIST  = font_store::get(28);
    F_HEAD  = font_store::get(40);
    F_BODY  = font_store::get(28);
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
enum class Mode { Dict, PowerOff, Credits };
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
// [book:8-wrap-start]
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
// [/book:8-wrap-start]
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
// [book:8-wrap-end]
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
// [/book:8-wrap-end]
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
    const bool saved = wordbook::contains(head);
    wrapInto(screenCv, F_HEAD, saved ? head + "　〔単語帳〕" : head, width, 0, 0, C_HEAD, S.body);
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
        title = "すべての辞書 (" + std::to_string(dictCount()) + " 冊, " + std::to_string(total) + " キー)";
    } else {
        Dictionary* d = dictAt(S.active == ALL_DICTS ? 0 : S.active);
        title = d->title() + "  (" + std::to_string(d->count()) + " キー)";
        if (dictCount() > 1) title = "[" + std::to_string(S.active + 1) + "/" + std::to_string(dictCount()) + "] " + title;
    }
    g.drawString(title.c_str(), SCREEN_W - 20, 14);

    g.setFont(F_SMALL);
    g.setTextColor(S.kbdI2c || S.kbdUsb ? C_HINT : C_WARN);
    std::string kbd = S.kbdI2c ? "KB: Tab5 Keyboard" : (S.kbdUsb ? "KB: USB" : "キーボード未接続");
    if (S.kbdI2c && S.kbdUsb) kbd = "KB: Tab5 Keyboard + USB";
    kbd += sdcard_mounted() ? "   SD: OK" : "   SD: なし";
    kbd += "   単語帳 " + std::to_string(wordbook::entries().size()) + " 件";
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
    g.setTextColor(wordbook::lastError().empty() ? C_FOOTER_T : C_WARN);
    g.setTextDatum(textdatum_t::middle_left);
    g.drawString(wordbook::lastError().empty() ? "↑↓ 選択  PgUp/Dn ページ  ←→ 説明  Tab 辞書  Enter 単語帳  Esc クリア  Ctrl+R 反転  Ctrl+Q 電源  Ctrl+L 出典" : wordbook::lastError().c_str(),
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

void drawPowerOff(); void drawCredits();

// [book:10-render]
void render()
{
    if (!g_dirty) return;
    const int64_t t0 = esp_timer_get_time();
    if (g_mode == Mode::PowerOff) { drawPowerOff(); present(); g_dirty = 0; return; }
    if (g_mode == Mode::Credits) { drawCredits(); present(); g_dirty = 0; return; }
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


// ---- dialogs ---------------------------------------------------------------
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

void drawDialogChrome(const char* title, const char* footer)
{
    auto& g = screenCv;
    g.fillScreen(C_BG);
    g.fillRect(0, 0, SCREEN_W, HEADER_H, C_HEADER);
    g.setTextDatum(textdatum_t::middle_left);
    g.setFont(F_QUERY); g.setTextColor(C_HEADER_T);
    g.drawString(title, 24, HEADER_H / 2);
    g.fillRect(0, SCREEN_H - FOOTER_H, SCREEN_W, FOOTER_H, C_FOOTER);
    g.setFont(F_SMALL); g.setTextColor(wordbook::lastError().empty() ? C_FOOTER_T : C_WARN);
    g.drawString(wordbook::lastError().empty() ? footer : wordbook::lastError().c_str(), 16, SCREEN_H - FOOTER_H / 2);
}

void drawPowerOff()
{
    auto& g = screenCv;
    drawDialogChrome("電源オフ", "Enter 電源を切る    Esc 戻る");
    g.setTextDatum(textdatum_t::top_left);
    g.setFont(F_HEAD); g.setTextColor(C_HEAD);
    g.drawString("電源を切りますか？", 80, 200);
    drawTextBlock(F_BODY, C_HINT2, "Enter で電源が切れます。次に使うときは本体の電源ボタンを押してください。", 80, 300, SCREEN_W - 160);
}

// Sources and licences of the data and fonts (Ctrl+L). The EDRDG licence asks apps that use
// JMdict / KANJIDIC2 to acknowledge them on a separate screen such as "About" or "Sources".
// [book:credits]
const struct { const char* head; const char* body; } kCredits[] = {
    {"英和辞書（本体に内蔵）",
     "EJDict-hand（kujirahand）。CC0 1.0（パブリックドメイン）"},
    {"和英・英和（逆引き）・漢字辞典",
     "JMdict と KANJIDIC2 を使っています。These files are the property of the Electronic Dictionary "
     "Research and Development Group, and are used in conformance with the Group's licence. "
     "CC BY-SA 4.0　https://www.edrdg.org/"},
    {"語義・類語辞書",
     "日本語 WordNet（NICT、Francis Bond ほか）。Japanese WordNet License。"
     "元になった WordNet 3.0 は Copyright 2006 by Princeton University"},
    {"フォント",
     "Noto Sans JP（SIL Open Font License 1.1）、IPA ゴシック由来の lgfxJapanGothic（IPA フォントライセンス v1.0）"},
};

void drawCredits()
{
    drawDialogChrome("出典とライセンス", "Esc 辞書に戻る");
    int y = 108;
    for (const auto& c : kCredits) {
        y = drawTextBlock(F_TITLE, C_HEAD, c.head, 60, y, SCREEN_W - 120, 1);
        y = drawTextBlock(F_SMALL, C_TEXT, c.body, 84, y + 2, SCREEN_W - 144, 3) + 12;
    }
    drawTextBlock(F_SMALL, C_HINT2,
                  "SD カードの辞書は、使う人が公式のデータから作ったものです。"
                  "変更点と条文は、SD カードの dict フォルダの「出典とライセンス.txt」にあります。",
                  60, y + 4, SCREEN_W - 120, 2);
}
// [/book:credits]

// [book:14-power-off]
void doPowerOff()
{
    ESP_LOGI(TAG, "power off");
    screenCv.fillScreen(C_HEADER);
    screenCv.setTextDatum(textdatum_t::middle_center);
    screenCv.setFont(F_HEAD); screenCv.setTextColor(C_HEADER_T);
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

void onKeyDialog(const KeyEvent& ev)
{
    const uint8_t k = ev.keycode;
    if (k == hid::KEY_ESC) { g_mode = Mode::Dict; drawAll(); return; }
    if (k != hid::KEY_ENTER && k != hid::KEY_KP_ENTER) return;
    if (g_mode == Mode::PowerOff) doPowerOff();
    else if (g_mode == Mode::Credits) { g_mode = Mode::Dict; drawAll(); }
}

void onTouchDialog()
{
    if (!M5.Touch.getDetail().wasClicked()) return;
    g_mode = Mode::Dict;
    drawAll();
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
    wordbook::load(); // also refresh a wordbook restored through the transfer tool
    if (S.active >= dictCount()) S.active = ALL_DICTS;
    doSearch();
    drawAll();
}

// [book:8-on-key]
// [book:8-on-key-start]
void onKey(const KeyEvent& ev)
{
    const uint8_t k = ev.keycode;
    const bool ctrl = ev.modifier & hid::MOD_CTRL;
    const bool shift = ev.modifier & hid::MOD_SHIFT;

    if (g_mode != Mode::Dict) { onKeyDialog(ev); return; }
    if (ctrl && k == 0x14) {   // Ctrl+Q: power off (with confirmation)
        g_mode = Mode::PowerOff;
        invalidate(DIRTY_ALL);
        return;
    }
    if (ctrl && k == 0x0F) {   // Ctrl+L: sources and licences
// [/book:8-on-key-start]
        g_mode = Mode::Credits;
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
            invalidate(DIRTY_HEADER | DIRTY_BODY | DIRTY_FOOTER);
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
// [book:8-on-key-end]
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
// [/book:8-on-key-end]
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
// [book:8-run-start]
void ui::run(std::vector<std::unique_ptr<Dictionary>>& dicts)
{
    applyFonts();
    S.dicts = &dicts;
    S.active = ALL_DICTS;
    createScreenCanvas();
    S.kbdI2c = keyboard::i2cConnected();
    S.kbdUsb = keyboard::usbConnected();
    doSearch();
    drawAll();
    render();
    ESP_LOGI(TAG, "ready");

    struct { bool active = false; KeyEvent ev{}; int64_t next = 0; } repeat;
    int64_t lastStatus = 0;
    bool transferWasPaused = false;

// [/book:8-run-start]
    for (;;) {
        if (dict_store::transferPauseRequested()) {
            if (!transferWasPaused) {
                S.results.clear(); S.body.clear(); dicts.clear();
                repeat.active = false;
            }
            transferWasPaused = true;
            dict_store::acknowledgeTransferPause();
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (transferWasPaused) {
            transferWasPaused = false;
            g_mode = Mode::Dict;
            reloadDictionaries();
        }
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
        if (g_mode == Mode::Dict) onTouch(); else onTouchDialog();
        if (dict_store::reloadPending()) reloadDictionaries();
        render();
        if (debug_screenshot_pending()) debug_dump_screen();
// [book:8-run-end]

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
// [/book:8-run-end]
// [/book:8-run]
