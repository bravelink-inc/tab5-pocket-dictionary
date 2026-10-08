#include "wordbook.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <unistd.h>
#include "esp_log.h"
#include "nvs.h"
#include "sdcard.hpp"
#include "file_replace.hpp"

static const char* TAG = "wordbook";
static std::vector<wordbook::Entry> s_entries;
static std::string s_error;
static bool s_loadOk = false;
#ifndef WORDBOOK_SD_PATH
#define WORDBOOK_SD_PATH "/sdcard/wordbook.txt"
#endif
static const char* SD_PATH = WORDBOOK_SD_PATH;
static const char* NVS_NS = "pdict";
static const char* NVS_KEY = "wordbook";
// Application limits, not the NVS API's maximum blob size.
static constexpr size_t NVS_MAX = 3900, SD_MAX = 64 * 1024, MAX_WORDS = 1000;

static bool fail(const char* message) { s_error = message; ESP_LOGW(TAG, "%s", message); return false; }
static bool validField(const std::string& s)
{
    return s.size() <= 512 && s.find_first_of("\r\n\t") == std::string::npos &&
           s.find('\0') == std::string::npos;
}

// [book:13-wordbook-save]
static bool save(const std::vector<wordbook::Entry>& entries)
{
    std::string blob;
    for (const auto& e : entries) blob += e.word + '\t' + e.quizKey + '\n';
    if (sdcard_mounted()) {
        if (!file_replace::recover(SD_PATH)) return fail("単語帳のSD復旧に失敗しました");
        const std::string temp = file_replace::temporary(SD_PATH);
        FILE* fp = fopen(temp.c_str(), "wb");
        if (!fp) return fail("単語帳をSDに保存できません");
        bool ok = fwrite(blob.data(), 1, blob.size(), fp) == blob.size();
        if (fflush(fp) != 0 || fsync(fileno(fp)) != 0) ok = false;
        if (fclose(fp) != 0) ok = false;
        if (ok) ok = file_replace::commit(SD_PATH);
        if (!ok) { unlink(temp.c_str()); return fail("単語帳のSD保存に失敗しました"); }
        return true;
    }
    if (blob.size() > NVS_MAX) return fail("本体の単語帳が満杯です。SDを使ってください");
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return fail("本体の単語帳を開けません");
    esp_err_t err = blob.empty() ? nvs_erase_key(h, NVS_KEY) :
        nvs_set_blob(h, NVS_KEY, blob.data(), blob.size());
    if (blob.empty() && err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK || fail("本体の単語帳保存に失敗しました");
}
// [/book:13-wordbook-save]

static std::string legacyKey(const std::string& word)
{
    // Old files contain display words only. Recover the first English spelling;
    // Japanese display words are never used as English quiz keys.
    std::string key = word.substr(0, word.find(','));
    while (!key.empty() && key.back() == ' ') key.pop_back();
    while (!key.empty() && key.front() == ' ') key.erase(key.begin());
    bool letter = false;
    for (char& c : key) {
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c >= 'a' && c <= 'z') letter = true;
        else if (c != ' ' && c != '-' && c != '\'' && (c < '0' || c > '9')) return "";
    }
    return letter ? key : "";
}

static bool parse(const std::string& blob)
{
    std::vector<wordbook::Entry> entries;
    size_t pos = 0;
    while (pos < blob.size()) {
        size_t nl = blob.find('\n', pos);
        std::string line = blob.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t tab = line.find('\t');
        wordbook::Entry e{line.substr(0, tab), tab == std::string::npos ? legacyKey(line) : line.substr(tab + 1)};
        if (!validField(e.word) || !validField(e.quizKey)) return fail("単語帳の形式が不正です");
        if (!e.word.empty() && std::none_of(entries.begin(), entries.end(), [&](const auto& old) { return old.word == e.word; }))
            entries.push_back(std::move(e));
        if (entries.size() > MAX_WORDS) return fail("単語帳は最大1000件です");
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    s_entries = std::move(entries);
    s_loadOk = true;
    return true;
}

// [book:13-wordbook-load]
void wordbook::load()
{
    s_loadOk = false;
    s_error.clear();
    std::string blob;
    bool haveSd = false;
    if (sdcard_mounted()) {
        if (!file_replace::recover(SD_PATH)) { fail("単語帳のSD復旧に失敗しました"); return; }
        FILE* fp = fopen(SD_PATH, "rb");
        if (fp) {
            haveSd = true; // an empty SD file is authoritative, too
            char buf[512]; size_t n;
            while ((n = fread(buf, 1, sizeof(buf), fp)) > 0 && blob.size() <= SD_MAX) blob.append(buf, n);
            bool ok = !ferror(fp) && blob.size() <= SD_MAX;
            if (fclose(fp) != 0) ok = false;
            if (!ok) { fail("SDの単語帳を読み込めません（最大64KiB）"); return; }
        } else if (errno != ENOENT) { fail("SDの単語帳を開けません"); return; }
    }
    if (!haveSd) {
        nvs_handle_t h;
        esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &h);
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) { fail("本体の単語帳を開けません"); return; }
        if (err == ESP_OK) {
            size_t len = 0;
            err = nvs_get_blob(h, NVS_KEY, nullptr, &len);
            if (err == ESP_OK && len <= SD_MAX) {
                blob.resize(len);
                if (len) err = nvs_get_blob(h, NVS_KEY, &blob[0], &len);
            } else if (err == ESP_OK) err = ESP_ERR_INVALID_SIZE;
            nvs_close(h);
            if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) { fail("本体の単語帳を読み込めません"); return; }
        }
    }
    if (parse(blob)) ESP_LOGI(TAG, "%u words", (unsigned)s_entries.size());
}
// [/book:13-wordbook-load]

bool wordbook::contains(const std::string& w)
{
    return std::any_of(s_entries.begin(), s_entries.end(), [&](const auto& e) { return e.word == w; });
}
bool wordbook::containsQuizKey(const std::string& k)
{
    return !k.empty() && std::any_of(s_entries.begin(), s_entries.end(), [&](const auto& e) { return e.quizKey == k; });
}
bool wordbook::add(const std::string& w, const std::string& key)
{
    if (!s_loadOk) return fail("単語帳を読み込めていないため保存できません");
    s_error.clear();
    if (w.empty() || contains(w) || containsQuizKey(key)) return false;
    if (!validField(w) || !validField(key)) return fail("単語帳の語が長すぎるか形式が不正です");
    auto next = s_entries;
    next.push_back({w, key});
    size_t bytes = 0;
    for (const auto& e : next) bytes += e.word.size() + e.quizKey.size() + 2;
    if (next.size() > MAX_WORDS || bytes > SD_MAX) return fail("単語帳は最大1000件・64KiBです");
    if (!save(next)) return false;
    s_entries = std::move(next);
    return true;
}
bool wordbook::remove(const std::string& w)
{
    if (!s_loadOk) return fail("単語帳を読み込めていないため保存できません");
    s_error.clear();
    auto next = s_entries;
    auto it = std::find_if(next.begin(), next.end(), [&](const auto& e) { return e.word == w; });
    if (it == next.end()) return false;
    next.erase(it);
    if (!save(next)) return false;
    s_entries = std::move(next);
    return true;
}
bool wordbook::toggle(const std::string& w, const std::string& key)
{
    auto it = std::find_if(s_entries.begin(), s_entries.end(), [&](const auto& e) {
        return e.word == w || (!key.empty() && e.quizKey == key);
    });
    if (it != s_entries.end()) {
        const std::string saved = it->word;
        return !remove(saved);
    }
    return add(w, key);
}
const std::vector<wordbook::Entry>& wordbook::entries() { return s_entries; }
const std::string& wordbook::lastError() { return s_error; }
