// core/text_util.h - small string helpers of the core.
//
// Needed by two core modules (ui_tags.cpp and gcode_parser.cpp), so they live
// here. They carry no value of their own: these are exactly the ASCII string
// operations the reference does through str/re.
#pragma once

#include <string>

namespace gv {

bool isSpaceCh(unsigned char c);
bool isDigitCh(char c);
// [A-Za-z] - the letter class in the G-code word patterns.
bool isLetterCh(char c);
// Word boundary for \b: letters, digits, underscore and bytes >= 0x80
// (Cyrillic in UTF-8) - the reference re in Unicode mode treats the latter as a
// letter too.
bool isWordCh(char c);
char upperCh(char c);
bool sameNoCase(char a, char b);

// str.strip for ASCII: spaces and control whitespace at the edges.
void stripInPlace(std::string& s);

// str.upper for an ASCII string.
std::string asciiUpper(const std::string& s);

// strtod by C-locale rules: the value does not depend on the process
// LC_NUMERIC (Qt raises the system locale at start, and under ru_RU the NC
// number parsing broke). The strtod semantics are kept whole - only the locale
// changes.
double strtodC(const char *s, char **end);

// float(value): the whole value must be parsed; false where the reference
// would raise ValueError (the caller then takes the default).
bool toDouble(const std::string& s, double& out);

// Same, but the bytes come from the slice [first, first + len) without
// creating a std::string: parsing goes through the same strtodC, so the result
// matches bit for bit. The string the slice is taken from must not change its
// contents or reallocate its buffer while the value is read.
bool toDouble(const char *first, size_t len, double& out);

} // namespace gv
