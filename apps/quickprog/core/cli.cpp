// core/cli.cpp - the file writer of the console.
#include "core/cli.h"

#include <cstdio>
#include <string>

namespace qp {

bool writeFile(const std::string& path, const std::string& text) {
    std::FILE* file = std::fopen(path.c_str(), "wb");
    if (file == nullptr) { return false; }
    const std::size_t written = std::fwrite(text.data(), 1, text.size(), file);
    const bool flushed = std::fclose(file) == 0;
    return written == text.size() && flushed;
}

} // namespace qp
