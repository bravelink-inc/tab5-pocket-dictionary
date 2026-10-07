#pragma once
#include <string>
#include <vector>

// 単語帳: words the user marked (Enter) or got wrong in a quiz.
// Stored in /sdcard/wordbook.txt when the SD card is mounted, else in NVS.
namespace wordbook {
void load();
bool contains(const std::string& word);
bool add(const std::string& word);      // returns true if newly added
bool remove(const std::string& word);
bool toggle(const std::string& word);   // returns true if now contained
const std::vector<std::string>& words();
}
