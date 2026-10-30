// core/option_words.h - the words both console parsers of the operations share.
//
// `--op face` and `--op pocket` are the same console: a number is a number, a
// material word is a material word. The helpers live here, in one place, so a
// later `--op drill` cannot grow a second spelling of "100x80x20" or accept
// `2,5` where the face parser rejects it.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace qp {

// A full-token number with a decimal point; false for "abc", "1x" and "1,5".
bool parseDouble(const std::string& text, double& out);

// A full-token integer.
bool parseInt(const std::string& text, int& out);

// Reads the value of the key at `i` and moves the cursor past both words.
bool takeValue(const std::vector<std::string>& args, std::size_t& i, std::string& value);

// "100x80x20": the stock size; the depth of the stock follows from the top.
bool parseStock(const std::string& text, double& x, double& y, double& z);

// "60x40": width and height of a rectangle.
bool parseSize(const std::string& text, double& w, double& h);

} // namespace qp
