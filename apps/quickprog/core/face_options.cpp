// core/face_options.cpp - the argument parser of `--op face`.
//
// Every key takes exactly one word; a bare word is the output file. A typed
// number wins over the table, an absent one comes from feeds.cpp (the calc.ts
// numbers), so the console never invents a regime of its own.
#include "core/face_options.h"

#include <cstddef>
#include <string>
#include <vector>

namespace qp {
namespace {

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

// Reads the value of the key at `i` and moves the cursor past both words.
bool takeValue(const std::vector<std::string>& args, std::size_t& i, std::string& value) {
    if (i + 1 >= args.size()) { return false; }
    value = args[i + 1];
    i += 2;
    return true;
}

// "100x80x20": the stock size; the depth of the stock follows from `stockZ`.
bool parseStock(const std::string& text, FaceArgs& args) {
    const std::size_t x = text.find('x');
    const std::size_t y = x == std::string::npos ? std::string::npos : text.find('x', x + 1);
    if (x == std::string::npos || y == std::string::npos) { return false; }
    return parseDouble(text.substr(0, x), args.stockX) &&
           parseDouble(text.substr(x + 1, y - x - 1), args.stockY) &&
           parseDouble(text.substr(y + 1), args.stockZ);
}

bool parseMaterial(const std::string& text, Material& out) {
    if (text == "aluminium") {
        out = Material::Aluminium;
        return true;
    }
    if (text == "steel") {
        out = Material::Steel;
        return true;
    }
    return false;
}

} // namespace

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
            if (!takeValue(args, i, value) || !parseStock(value, out)) { return false; }
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
