// core/cli.cpp - the file reader and writer of the console.
#include "core/cli.h"

#include <cstdio>
#include <string>
#include <vector>

namespace qp {

bool readFile(const std::string& path, std::string& text) {
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr) { return false; }
    std::vector<char> buf(4096);
    text.clear();
    bool ok = true;
    while (true) {
        const std::size_t got = std::fread(buf.data(), 1, buf.size(), file);
        text.append(buf.data(), got);
        if (got < buf.size()) {
            ok = std::ferror(file) == 0;
            break;
        }
    }
    if (std::fclose(file) != 0) { ok = false; }
    return ok;
}

bool writeFile(const std::string& path, const std::string& text) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) { return false; }
    const std::size_t written = std::fwrite(text.data(), 1, text.size(), file);
    const bool flushed = std::fclose(file) == 0;
    return written == text.size() && flushed;
}

} // namespace qp
