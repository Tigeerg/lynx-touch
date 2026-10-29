// core/face_options.cpp - the argument parser of `--op face`.
//
// Every key takes exactly one word; a bare word is the output file. A typed
// number wins over the table, an absent one comes from feeds.cpp (the calc.ts
// numbers), so the console never invents a regime of its own. The word helpers
// are shared with the pocket parser (`option_words.h`).
#include "core/face_options.h"

#include <cstddef>
#include <string>
#include <vector>

#include "core/option_words.h"

namespace qp {

bool parseFaceArgs(const std::vector<std::string>& args, std::size_t first, FaceArgs& out) {
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
        } else if (key == "--plane") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.planeZ)) { return false; }
            out.planeGiven = true;
        } else if (key == "--depth") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.depth)) { return false; }
        } else if (key == "--step-down") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.stepDown)) { return false; }
        } else if (key == "--step-over") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.stepOverPct)) { return false; }
        } else if (key == "--feed") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.feed)) { return false; }
        } else if (key == "--plunge") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.plunge)) { return false; }
        } else if (key == "--rpm") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.rpm)) { return false; }
        } else if (key == "--material") {
            if (!takeValue(args, i, value) || !parseMaterial(value, out.material)) {
                return false;
            }
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
