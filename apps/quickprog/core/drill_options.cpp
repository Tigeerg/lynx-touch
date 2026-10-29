// core/drill_options.cpp - the argument parser of `--op drill`.
//
// The word helpers are the ones of `option_words.h`; the shape words of a
// drilling layout are named after the fields of `ops.h`, so the console, the
// `.qprog` file and the form speak the same words.
#include "core/drill_options.h"

#include <cstddef>
#include <string>
#include <vector>

#include "core/option_words.h"

namespace qp {

bool parseDrillPattern(const std::string& text, DrillPattern& out) {
    if (text == "hole") {
        out = DrillPattern::Hole;
    } else if (text == "grid") {
        out = DrillPattern::Grid;
    } else if (text == "circle") {
        out = DrillPattern::Circle;
    } else {
        return false;
    }
    return true;
}

bool parseDrillArgs(const std::vector<std::string>& args, std::size_t first, DrillArgs& out) {
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
        } else if (key == "--r") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.r)) { return false; }
            out.rGiven = true;
        } else if (key == "--peck") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.peck)) { return false; }
        } else if (key == "--pattern") {
            if (!takeValue(args, i, value) || !parseDrillPattern(value, out.pattern)) {
                return false;
            }
        } else if (key == "--x") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.x)) { return false; }
        } else if (key == "--y") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.y)) { return false; }
        } else if (key == "--cols") {
            if (!takeValue(args, i, value) || !parseInt(value, out.cols)) { return false; }
        } else if (key == "--rows") {
            if (!takeValue(args, i, value) || !parseInt(value, out.rows)) { return false; }
        } else if (key == "--dx") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.dx)) { return false; }
        } else if (key == "--dy") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.dy)) { return false; }
        } else if (key == "--count") {
            if (!takeValue(args, i, value) || !parseInt(value, out.count)) { return false; }
        } else if (key == "--radius") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.radius)) { return false; }
        } else if (key == "--start") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.startDeg)) { return false; }
        } else if (key == "--arc") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.arcDeg)) { return false; }
            out.fullTurn = false;
        } else if (key == "--full-turn") {
            out.fullTurn = true;
        } else if (key == "--feed") {
            if (!takeValue(args, i, value) || !parseDouble(value, out.feed)) { return false; }
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
