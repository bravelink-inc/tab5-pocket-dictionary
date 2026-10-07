#pragma once
#include <memory>
#include <vector>
#include "dictionary.hpp"

namespace dict_store {
// Loads the built-in dictionary from the "dict" flash partition (if present) and every
// *.pdc file found in /sdcard/dict. Returns the number of dictionaries loaded.
size_t loadAll(std::vector<std::unique_ptr<Dictionary>>& out);
// Request a reload (e.g. after files were uploaded); the UI task performs it.
void requestReload();
bool reloadPending();
}
