// core/text_util.cpp - implementation of the core string helpers.
#include "text_util.h"

#include <cstdlib>
#include <cstring>
#include <locale.h>

namespace gv {

namespace {

// float(value) for a null-terminated string: parse in full or fail. Both
// toDouble overloads call this parse, so the value matches bit for bit.
bool parseFull(const char *s, double& out) {
    if (s == nullptr || *s == '\0') { return false; }
    char *end = nullptr;
    const double v = strtodC(s, &end);
    if (end == s) { return false; }
    while (*end != '\0' && isSpaceCh(static_cast<unsigned char>(*end))) { ++end; }
    if (*end != '\0') { return false; }
    out = v;
    return true;
}

} // namespace

// Parse a number by C-locale rules regardless of the process LC_NUMERIC. Qt
// raises the system locale at start: under ru_RU std::strtod reads "X2.081" as 2
// and stops at the dot, and NC parsing broke at 785 segments instead of 70914
// (Test_model.ngc). The parse is the same strtod, only the locale changes, so
// the values match the reference bit for bit.
double strtodC(const char *s, char **end) {
#ifdef _WIN32
    static const _locale_t loc = _create_locale(LC_NUMERIC, "C");
    return _strtod_l(s, end, loc);
#else
    static const locale_t loc = newlocale(LC_NUMERIC_MASK, "C", static_cast<locale_t>(nullptr));
    const locale_t prev = uselocale(loc);
    const double v = std::strtod(s, end);
    uselocale(prev);
    return v;
#endif
}

bool isSpaceCh(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

bool isDigitCh(char c) { return c >= '0' && c <= '9'; }

bool isLetterCh(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool isWordCh(char c) {
    // Word boundary of the reference re in Unicode mode: letters, digits,
    // underscore and anything else the reference counts as a word. In UTF-8 a
    // byte >= 0x80 is always part of a non-ASCII character, and in practice that
    // is Cyrillic (a letter, not a sign); single-byte signs like '·' would
    // diverge, but they never appear next to T..M6 in an NC program.
    if (static_cast<unsigned char>(c) >= 0x80) { return true; }
    return isLetterCh(c) || isDigitCh(c) || c == '_';
}

char upperCh(char c) { return (c >= 'a' && c <= 'z') ? char(c - 'a' + 'A') : c; }

bool sameNoCase(char a, char b) { return upperCh(a) == upperCh(b); }

void stripInPlace(std::string& s) {
    size_t b = 0;
    while (b < s.size() && isSpaceCh(static_cast<unsigned char>(s[b]))) { ++b; }
    size_t e = s.size();
    while (e > b && isSpaceCh(static_cast<unsigned char>(s[e - 1]))) { --e; }
    // Nothing to trim: substr would copy the buffer anyway. In Test_model.ngc no
    // line out of 110585 needs trimming.
    if (b == 0 && e == s.size()) { return; }
    s = s.substr(b, e - b);
}

std::string asciiUpper(const std::string& s) {
    std::string out = s;
    for (char& c : out) { c = upperCh(c); }
    return out;
}

bool toDouble(const std::string& s, double& out) {
    return parseFull(s.c_str(), out);
}

// The same float(value), but the bytes come from a string slice (without
// creating a std::string per token). A slice shorter than the stack buffer is
// copied into the buffer and null-terminated; a longer slice is parsed through
// std::string.
bool toDouble(const char *first, size_t len, double& out) {
    if (first == nullptr || len == 0) { return false; }
    char buf[64];
    if (len >= sizeof(buf)) { return toDouble(std::string(first, len), out); }
    std::memcpy(buf, first, len);
    buf[len] = '\0';
    return parseFull(buf, out);
}

} // namespace gv
