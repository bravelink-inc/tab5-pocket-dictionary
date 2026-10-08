#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
extern size_t test_largest_block;
inline void* heap_caps_malloc(size_t n, int) { return malloc(n); }
inline void heap_caps_free(void* p) { free(p); }
inline size_t heap_caps_get_largest_free_block(int) { return test_largest_block; }
