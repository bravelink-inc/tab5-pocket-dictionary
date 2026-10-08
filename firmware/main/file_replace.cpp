#include "file_replace.hpp"
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr const char* BACKUP = ".pdict-bak";
bool exists(const std::string& path, bool& found)
{
    struct stat st;
    if (stat(path.c_str(), &st) == 0) { found = true; return S_ISREG(st.st_mode); }
    found = false;
    return errno == ENOENT;
}
}

std::string file_replace::temporary(const std::string& path) { return path + ".pdict-tmp"; }

bool file_replace::recover(const std::string& path)
{
    bool target, backup;
    const std::string old = path + BACKUP;
    if (!exists(path, target) || !exists(old, backup)) return false;
    if (!backup) return true;
    if (!target) return rename(old.c_str(), path.c_str()) == 0;
    return unlink(old.c_str()) == 0;
}

bool file_replace::commit(const std::string& path)
{
    if (!recover(path)) return false;
    bool target;
    if (!exists(path, target)) return false;
    const std::string old = path + BACKUP, temp = temporary(path);
    bool staged;
    if (!exists(temp, staged) || !staged) return false;
    if (target && rename(path.c_str(), old.c_str()) != 0) return false;
    if (rename(temp.c_str(), path.c_str()) != 0) {
        const int error = errno;
        if (target) rename(old.c_str(), path.c_str()); // backup remains if rollback fails
        errno = error;
        return false;
    }
    if (target) unlink(old.c_str()); // a leftover backup is recovered on the next load
    return true;
}

void file_replace::recoverDirectory(const char* dir)
{
    DIR* d = opendir(dir);
    if (!d) return;
    while (dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        const size_t suffix = std::char_traits<char>::length(BACKUP);
        if (name.size() > suffix && name.compare(name.size() - suffix, suffix, BACKUP) == 0)
            recover(std::string(dir) + "/" + name.substr(0, name.size() - suffix));
    }
    closedir(d);
}
