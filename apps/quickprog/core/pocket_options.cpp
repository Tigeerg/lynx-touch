// core/pocket_options.cpp - the argument parser of `--op pocket`.
//
// The word parser helpers (`parseDouble`, `parseInt`, `parseStock`,
// `parseMaterial`, `takeValue`) are the ones of face_options.cpp: the two
// parsers are the same console, and a second spelling of "a number" would make
// `--depth 2,5` work in one place and not in the other. They live in their own
// file, `option_words`, so neither parser owns the other.
#include "core/pocket_options.h"

#include <cstddef>
#include <string>
#include <vector>

#include "core/option_words.h"

namespace qp {

bool parsePocketArgs(const std::vector<std::string>& args, std::size_t first, PocketArgs& out) {
    std::size_t i = first;
    while (i < args.size()) {
        const std::string& key = args[i];
        std::string value;
        if (key == "--tool") {
            if (!takeValue(args, i, value) || !parseInt(value, out.tool)) { return false; }
        } else if (key == "--diameter") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.diameter)) { return false; }
        } else if (key == "--stock") {
            if (!takeValue(args, i, value) ||
                !parseStock(value, out.stockX, out.stockY, out.stockZ)) {
                return false;
            }
        } else if (key == "--top") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.topZ)) { return false; }
        } else if (key == "--depth") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.depth)) { return false; }
        } else if (key == "--step-down") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.stepDown)) { return false; }
        } else if (key == "--x") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.x)) { return false; }
        } else if (key == "--y") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.y)) { return false; }
        } else if (key == "--width") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.width)) { return false; }
        } else if (key == "--height") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.height)) { return false; }
        } else if (key == "--size") {
            if (!takeValue(args, i, value) || !parseSize(value, out.width, out.height)) {
                return false;
            }
        } else if (key == "--corner") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.cornerRadius)) {
                return false;
            }
        } else if (key == "--allowance") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.finishAllowance)) {
                return false;
            }
        } else if (key == "--feed") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.feed)) { return false; }
        } else if (key == "--plunge") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.plunge)) { return false; }
        } else if (key == "--rpm") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.rpm)) { return false; }
        } else if (key == "--material") {
            if (!takeValue(args, i, value) || !parseMaterial(value, out.material)) { return false; }
        } else if (key == "--safe") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.safeZ)) { return false; }
            out.safeGiven = true;
        } else if (key == "--name") {
            if (!takeValue(args, i, value)) { return false; }
            out.name = value;
        } else if (!key.empty() && key[0] == '-') {
            return false;
        } else if (out.output.empty()) {
            out.output = key;
            ++i;
        } else {
            return false;
        }
    }
    return !out.output.empty();
}

} // namespace qp
