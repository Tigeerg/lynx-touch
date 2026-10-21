// core/ui_tags.cpp - parsing of the postprocessor UI_* tags.
//
// The patterns are repeated by hand rather than with std::regex: the reference
// re works in Unicode mode and treats \b and \s differently, and the comparison
// must be literal.
#include "ui_tags.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <vector>

#include "text_util.h"

namespace gv {
namespace {

// Case-insensitive ASCII substring position (re.IGNORECASE for ASCII patterns).
size_t findNoCase(const std::string& hay, const char *needle, size_t from = 0) {
    const size_t n = std::strlen(needle);
    if (n == 0 || hay.size() < n) { return std::string::npos; }
    for (size_t i = from; i + n <= hay.size(); ++i) {
        size_t k = 0;
        while (k < n && sameNoCase(hay[i + k], needle[k])) { ++k; }
        if (k == n) { return i; }
    }
    return std::string::npos;
}

// true if a whole word sits at position p, case-insensitively; p advances.
bool eatNoCase(const std::string& s, size_t& p, const char *word) {
    const size_t n = std::strlen(word);
    if (p + n > s.size()) { return false; }
    for (size_t k = 0; k < n; ++k) {
        if (!sameNoCase(s[p + k], word[k])) { return false; }
    }
    p += n;
    return true;
}

// Skip whitespace characters; true if at least one was skipped.
bool skipSpace(const std::string& s, size_t& p) {
    const size_t start = p;
    while (p < s.size() && isSpaceCh(static_cast<unsigned char>(s[p]))) { ++p; }
    return p > start;
}


// The pattern number -?\d+(\.\d+)? (the reference _NUM_RE): no leading plus and
// no exponent; a dot without a digit after it is not part of the number.
bool readNum(const std::string& s, size_t& p, double& out) {
    const size_t start = p;
    if (p < s.size() && s[p] == '-') { ++p; }
    const size_t digitsStart = p;
    while (p < s.size() && isDigitCh(s[p])) { ++p; }
    if (p == digitsStart) { p = start; return false; }
    if (p + 1 < s.size() && s[p] == '.' && isDigitCh(s[p + 1])) {
        ++p;
        while (p < s.size() && isDigitCh(s[p])) { ++p; }
    }
    out = strtodC(s.substr(start, p - start).c_str(), nullptr);
    return true;
}

// All numbers of the _NUM_RE pattern in order (the reference findall).
std::vector<double> findAllNums(const std::string& s) {
    std::vector<double> out;
    size_t p = 0;
    while (p < s.size()) {
        double v = 0.0;
        if (readNum(s, p, v)) { out.push_back(v); }
        else { ++p; }
    }
    return out;
}

// An occurrence of 'NAME:' from position from: returns the start of the tag and
// puts the position right after the colon into `after`. An occurrence without a
// colon is not a match, and the search continues (how re.search with the
// 'NAME:' pattern behaves).
size_t findTagAfterColon(const std::string& text, const char *tag, size_t from, size_t& after) {
    const size_t n = std::strlen(tag);
    size_t at = from;
    while (true) {
        at = findNoCase(text, tag, at);
        if (at == std::string::npos) { return std::string::npos; }
        if (at + n < text.size() && text[at + n] == ':') {
            after = at + n + 1;
            return at;
        }
        ++at;
    }
}

// The tag body 'NAME: <up to ) or end of line>' (the reference [^)\n]*).
bool tagBody(const std::string& text, const char *tag, std::string& body) {
    size_t p = 0;
    if (findTagAfterColon(text, tag, 0, p) == std::string::npos) { return false; }
    skipSpace(text, p);
    const size_t start = p;
    while (p < text.size() && text[p] != ')' && text[p] != '\n') { ++p; }
    body = text.substr(start, p - start);
    return true;
}

} // namespace

ToolInfo parseUiTool(const std::string& text) {
    ToolInfo info;
    std::string body;
    if (!tagBody(text, "UI_TOOL", body)) { return info; }

    std::vector<std::pair<std::string, std::string>> kv;
    size_t q = 0;
    while (q < body.size()) {
        skipSpace(body, q);
        const size_t start = q;
        while (q < body.size() && !isSpaceCh(static_cast<unsigned char>(body[q]))) { ++q; }
        if (q == start) { break; }
        const std::string tok = body.substr(start, q - start);
        const size_t eq = tok.find('=');
        if (eq != std::string::npos) {
            std::string key = tok.substr(0, eq);
            std::string val = tok.substr(eq + 1);
            stripInPlace(key);
            stripInPlace(val);
            for (char& c : key) { c = upperCh(c); }
            kv.emplace_back(key, val);       // a repeated key overrides the earlier value
        }
    }
    auto num = [&kv](const char *key, double def) {
        for (auto it = kv.rbegin(); it != kv.rend(); ++it) {
            if (it->first == key) {
                double v = 0.0;
                return toDouble(it->second, v) ? v : def;
            }
        }
        return def;
    };
    auto str = [&kv](const char *key, const char *def) {
        for (auto it = kv.rbegin(); it != kv.rend(); ++it) {
            if (it->first == key) { return it->second; }
        }
        return std::string(def);
    };

    std::string type = str("TYPE", "FLAT");
    for (char& c : type) { c = upperCh(c); }
    info.number = static_cast<int>(num("T", 0.0));
    info.type = type;
    info.diameter = num("D", 0.0);
    info.corner_radius = num("CR", 0.0);
    info.taper_deg = num("TAPER", 0.0);
    info.tip_angle_deg = num("TIPA", 0.0);
    info.tip_diameter = num("TIPD", 0.0);
    info.flute_length = num("FL", 0.0);
    info.shoulder_length = num("SL", 0.0);
    info.body_length = num("BL", 0.0);
    info.shank_diameter = num("SD", 0.0);
    info.overall_length = num("OL", 0.0);
    info.flutes = static_cast<int>(num("NF", 0.0));
    info.present = true;
    return info;
}

std::string stageLabel(const ToolInfo& info) {
    if (!info.present) { return "\xD0\xAD\xD1\x82\xD0\xB0\xD0\xBF"; }   // RU "Этап"
    std::string out = "T" + std::to_string(info.number);
    static const std::pair<const char *, const char *> kTypeRu[] = {
        {"FLAT", "\xD1\x84\xD1\x80\xD0\xB5\xD0\xB7\xD0\xB0"},                       // RU "фреза"
        {"BULL", "\xD1\x84\xD1\x80\xD0\xB5\xD0\xB7\xD0\xB0 R"},                    // RU "фреза R"
        {"BALL", "\xD1\x81\xD1\x84\xD0\xB5\xD1\x80. \xD1\x84\xD1\x80\xD0\xB5\xD0\xB7\xD0\xB0"}, // RU "сфер. фреза"
        {"CHAMFER", "\xD1\x84\xD0\xB0\xD1\x81\xD0\xBA\xD0\xB0"},                   // RU "фаска"
        {"TAPER", "\xD0\xBA\xD0\xBE\xD0\xBD\xD1\x83\xD1\x81"},                     // RU "конус"
        {"DOVETAIL", "\xD0\xBB\xD0\xB0\xD1\x81\xD1\x82\xD0\xBE\xD1\x87\xD0\xBA\xD0\xB0"}, // RU "ласточка"
        {"LOLLIPOP", "\xD0\xB3\xD1\x80\xD0\xB8\xD0\xB1\xD0\xBE\xD0\xBA"},         // RU "грибок"
        {"THREAD", "\xD1\x80\xD0\xB5\xD0\xB7\xD1\x8C\xD0\xB1\xD0\xB0"},           // RU "резьба"
        {"DRILL", "\xD1\x81\xD0\xB2\xD0\xB5\xD1\x80\xD0\xBB\xD0\xBE"},             // RU "сверло"
        {"TAP", "\xD0\xBC\xD0\xB5\xD1\x82\xD1\x87\xD0\xB8\xD0\xBA"},               // RU "метчик"
        {"PROBE", "\xD1\x89\xD1\x83\xD0\xBF"},                                     // RU "щуп"
    };
    std::string typ = info.type;
    for (const auto& entry : kTypeRu) {
        if (typ == entry.first) { typ = entry.second; break; }
    }
    if (!typ.empty()) { out += " " + typ; }
    const double d = info.diameter;
    if (d != 0.0) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "D%g", d);
        out += " " + std::string(buf);
    }
    return out;
}

std::string commentText(const std::string& raw) {
    const size_t open = raw.find('(');
    if (open == std::string::npos) { return std::string(); }
    const size_t close = raw.find(')', open + 1);
    if (close == std::string::npos) { return std::string(); }
    std::string inner = raw.substr(open + 1, close - open - 1);
    stripInPlace(inner);
    return inner;
}

std::array<double, 3> parseUiRotation(const std::string& text) {
    const std::array<double, 3> zero{0.0, 0.0, 0.0};
    size_t p = 0;
    if (findTagAfterColon(text, "UI_ROTATION", 0, p) == std::string::npos) { return zero; }
    skipSpace(text, p);
    std::array<double, 3> out{0.0, 0.0, 0.0};
    for (int i = 0; i < 3; ++i) {
        const char *axis = (i == 0) ? "RX" : (i == 1) ? "RY" : "RZ";
        if (!eatNoCase(text, p, axis)) { return zero; }
        skipSpace(text, p);
        if (!readNum(text, p, out[i])) { return zero; }
        if (p < text.size() && text[p] == '.') { ++p; }
        if (i < 2 && !skipSpace(text, p)) { return zero; }   // between axes \s+ is required
    }
    return out;
}

std::array<double, 3> parseUiOrigin(const std::string& text) {
    const std::array<double, 3> zero{0.0, 0.0, 0.0};
    size_t p = 0;
    if (findTagAfterColon(text, "UI_ORIGIN", 0, p) == std::string::npos) { return zero; }
    skipSpace(text, p);
    std::array<double, 3> out{0.0, 0.0, 0.0};
    for (int i = 0; i < 3; ++i) {
        const char *axis = (i == 0) ? "X" : (i == 1) ? "Y" : "Z";
        if (!eatNoCase(text, p, axis)) { return zero; }
        skipSpace(text, p);
        if (!readNum(text, p, out[i])) { return zero; }
        if (p < text.size() && text[p] == '.') { ++p; }
        if (i < 2 && !skipSpace(text, p)) { return zero; }
    }
    return out;
}

std::optional<HolderTag> parseUiHolder(const std::string& text) {
    std::string body;
    if (!tagBody(text, "UI_HOLDER", body)) { return std::nullopt; }
    HolderTag out;
    size_t from = 0;
    while (true) {
        const size_t g = findNoCase(body, "GAUGE", from);
        if (g == std::string::npos) { break; }
        size_t p = g + std::strlen("GAUGE");
        skipSpace(body, p);
        if (p < body.size() && body[p] == '=') {
            ++p;
            skipSpace(body, p);
            const size_t start = p;
            if (p < body.size() && (body[p] == '+' || body[p] == '-')) { ++p; }
            while (p < body.size() && (isDigitCh(body[p]) || body[p] == '.')) { ++p; }
            if (p > start) {
                out.gauge = strtodC(body.substr(start, p - start).c_str(), nullptr);
                out.has_gauge = true;
                break;
            }
        }
        from = g + 1;
    }
    from = 0;
    while (true) {
        const size_t n = findNoCase(body, "NAME", from);
        if (n == std::string::npos) { break; }
        size_t p = n + std::strlen("NAME");
        skipSpace(body, p);
        if (p < body.size() && body[p] == '=') {
            ++p;
            skipSpace(body, p);
            // [^=]+?\s*$: the name runs to the end of the body and contains no '='.
            if (body.find('=', p) == std::string::npos) {
                std::string name = body.substr(p);
                stripInPlace(name);
                out.name = name;
                out.has_name = true;
                break;
            }
        }
        from = n + 1;
    }
    return out;
}

std::optional<std::array<double, 6>> parseUiStock(const std::string& text) {
    std::string body;
    if (!tagBody(text, "UI_STOCK", body)) { return std::nullopt; }
    const std::vector<double> nums = findAllNums(body);
    if (nums.size() < 6) { return std::nullopt; }
    std::array<double, 6> out{};
    for (int i = 0; i < 6; ++i) { out[i] = nums[i]; }
    return out;
}

std::optional<std::array<double, 9>> parseUiCadMatrix(const std::string& text) {
    std::string body;
    if (!tagBody(text, "UI_CAD_MATRIX", body)) { return std::nullopt; }
    const std::vector<double> nums = findAllNums(body);
    if (nums.size() < 9) { return std::nullopt; }
    std::array<double, 9> out{};
    for (int i = 0; i < 9; ++i) { out[i] = nums[i]; }
    return out;
}

std::optional<std::array<double, 3>> parseUiCadMatrixOrigin(const std::string& text) {
    size_t p = 0;
    if (findTagAfterColon(text, "UI_CAD_MATRIX_ORIGIN", 0, p) == std::string::npos) { return std::nullopt; }
    skipSpace(text, p);
    std::array<double, 3> out{0.0, 0.0, 0.0};
    for (int i = 0; i < 3; ++i) {
        const char *axis = (i == 0) ? "X" : (i == 1) ? "Y" : "Z";
        if (!eatNoCase(text, p, axis)) { return std::nullopt; }
        skipSpace(text, p);
        if (!readNum(text, p, out[i])) { return std::nullopt; }
        if (p < text.size() && text[p] == '.') { ++p; }
        skipSpace(text, p);
    }
    return out;
}

} // namespace gv
