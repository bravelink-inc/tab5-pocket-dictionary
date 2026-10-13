#pragma once
#include <string>
#include <vector>

// 単語帳: display words the user marked with Enter, one word per line.
// Stored in /sdcard/wordbook.txt when mounted, else in NVS. A failed save leaves
// the in-memory list unchanged. Older files with an optional second column still load.
namespace wordbook {
struct Entry { std::string word; };
void load();
bool contains(const std::string& word);
bool add(const std::string& word); // true if saved
bool remove(const std::string& word);
bool toggle(const std::string& word); // now contained
const std::vector<Entry>& entries();
const std::string& lastError();
}
