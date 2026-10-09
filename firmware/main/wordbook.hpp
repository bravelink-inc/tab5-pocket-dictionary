#pragma once
#include <string>
#include <vector>

// 単語帳: words the user marked (Enter) or got wrong in a quiz.
// Stored in /sdcard/wordbook.txt when mounted, else in NVS. A failed save leaves
// the in-memory list unchanged. Existing one-word-per-line files still load.
namespace wordbook {
struct Entry { std::string word, quizKey; };
void load();
bool contains(const std::string& word);
bool containsQuizKey(const std::string& key);
bool add(const std::string& word, const std::string& quizKey = ""); // true if saved
bool remove(const std::string& word);
bool toggle(const std::string& word, const std::string& quizKey = ""); // now contained
const std::vector<Entry>& entries();
const std::string& lastError();
}
