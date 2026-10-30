// core/option_words.cpp - the shared words of the console parsers (7.2).
//
// The bodies moved from face_options.cpp unchanged: the face parser keeps its
// behaviour, and the pocket parser reads the same numbers, the same
// "100x80x20" and the same material words. A typed number wins over the table
// in both parsers; that rule lives in the caller, not here.
#include "core/option_words.h"

#include <cstddef>
#include <string>
#include <vector>

namespace qp {

bool parseDouble(const std::string& text, double& out) {
    try {
        std::size_t used = 0;
        out = std::stod(text, &used);
        return used == text.size();
    } catch (...) {
        return false;
    }
}

bool parseInt(const std::string& text, int& out) {
    try {
        std::size_t used = 0;
        out = std::stoi(text, &used);
        return used == text.size();
    } catch (...) {
        return false;
    }
}

bool takeValue(const std::vector<std::string>& args, std::size_t& i, std::string& value) {
    if (i + 1 >= args.size()) { return false; }
    value = args[i + 1];
    i += 2;
    return true;
}

bool parseStock(const std::string& text, double& x, double& y, double& z) {
    const std::size_t first = text.find('x');
    const std::size_t second =
        first == std::string::npos ? std::string::npos : text.find('x', first + 1);
    if (first == std::string::npos || second == std::string::npos) { return false; }
    return parseDouble(text.substr(0, first), x) &&
           parseDouble(text.substr(first + 1, second - first - 1), y) &&
           parseDouble(text.substr(second + 1), z);
}

bool parseSize(const std::string& text, double& w, double& h) {
    const std::size_t at = text.find('x');
    if (at == std::string::npos) { return false; }
    return parseDouble(text.substr(0, at), w) && parseDouble(text.substr(at + 1), h);
}

} // namespace qp
