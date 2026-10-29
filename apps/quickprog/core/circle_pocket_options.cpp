// core/circle_pocket_options.cpp - the argument parser of `--op circle-pocket`.
//
// The word helpers are the ones of `option_words.h`: the console has one
// spelling of a number, a stock and a material, so a later operation cannot
// accept `2,5` where the face parser rejects it.
#include "core/circle_pocket_options.h"

#include <cstddef>
#include <string>
#include <vector>

#include "core/option_words.h"

namespace qp {

bool parseCirclePocketArgs(const std::vector<std::string>& args, std::size_t first,
                           CirclePocketArgs& out) {
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
        } else if (key == "--pocket-dia") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.diameterPocket)) {
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
