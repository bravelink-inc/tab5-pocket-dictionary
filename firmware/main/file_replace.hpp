#pragma once
#include <string>

// FAT rename does not overwrite an existing file. Keep the old file until the
// flushed staging file can take its name; recover interrupted renames on boot.
namespace file_replace {
std::string temporary(const std::string& path);
bool recover(const std::string& path);
bool commit(const std::string& path);
void recoverDirectory(const char* dir);
}
