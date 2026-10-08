#include "dictionary.hpp"
#include <cassert>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

size_t test_largest_block = 16u << 20;

int main(int argc, char** argv)
{
    assert(argc == 4);
    const bool accepted = std::strcmp(argv[2], "valid") == 0;
    const bool record_ok = std::strcmp(argv[3], "record") == 0;
    std::ifstream file(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
    Dictionary d;
    assert(!d.openMemory(nullptr, 64, "null"));
    assert(d.openMemory(bytes.data(), bytes.size(), "test") == accepted);
    assert(d.valid() == accepted);
    auto exercise = [&]() {
        if (!accepted) return;
        assert(d.lowerBound("alpha") == 0);
        assert(d.hasPrefix(0, "al"));
        assert(!d.hasPrefix(d.count(), "al"));
        assert(std::strcmp(d.key(d.count()), "") == 0);
        std::string head, def;
        assert(d.entry(0, head, def) == record_ok);
        if (record_ok) { assert(head == "alpha"); assert(def == "first definition"); }
        assert(!d.entry(d.count(), head, def));
    };
    exercise();
    // Exercise both the resident and lazy SD read paths.
    for (size_t largest : {size_t(16u << 20), size_t(0)}) {
        test_largest_block = largest;
        assert(d.openFile(argv[1]) == accepted);
        if (accepted) assert(d.lazy() == (largest == 0));
        exercise();
    }
    d.close();
    assert(!d.valid() && d.tag().empty());
}
