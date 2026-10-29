// core/pocket_options.h - the console arguments of `--op pocket` (7.2).
//
// Same shape as `FaceArgs`: named fields with the defaults of the help text,
// one bare word for the output file, an unknown key or a number that does not
// parse is a usage error. The parser never decides about the toolpath; it only
// turns words into fields.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/feeds.h"

namespace qp {

struct PocketArgs {
    int tool = 1;
    double diameter = 10.0;
    double stockX = 100.0;
    double stockY = 80.0;
    double stockZ = 20.0;
    double topZ = 0.0;
    double depth = 2.0;
    double stepDown = 2.0;
    // Lower-left corner of the pocket in machine coordinates. The pocket is
    // not centred on the stock by default: the operator types the drawing
    // coordinates, and a hidden centring step would move the part.
    double x = 0.0;
    double y = 0.0;
    double width = 60.0;
    double height = 40.0;
    double cornerRadius = 0.0;
    double finishAllowance = 0.0;
    double feed = 0.0;     // zero: take the table value from feeds.cpp
    double plunge = 0.0;   // zero: a quarter of the feed
    double rpm = 0.0;      // zero: take the table value from feeds.cpp
    Material material = Material::Aluminium;
    bool safeGiven = false;
    double safeZ = 0.0;    // used only when safeGiven
    std::string name = "POCKET";
    std::string output;
};

// Parses the words from index `first` (the word after "--op pocket"). False for
// any usage error; the caller then prints the help.
bool parsePocketArgs(const std::vector<std::string>& args, std::size_t first, PocketArgs& out);

} // namespace qp
