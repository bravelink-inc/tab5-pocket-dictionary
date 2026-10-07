#pragma once
#include <memory>
#include <vector>
#include "dictionary.hpp"

namespace ui {
// Full-screen status message (used while booting).
void showSplash(const char* message);
// Main loop: keyboard/touch driven dictionary browser. Never returns.
void run(std::vector<std::unique_ptr<Dictionary>>& dicts);
}
