// core/circle_pocket_options.h - the console arguments of `--op circle-pocket`
// (7.2). Same shape as `PocketArgs`: named fields with the defaults of the help
// text, one bare word for the output file.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/feeds.h"

namespace qp {

struct CirclePocketArgs {
    int tool = 1;
    double diameter = 10.0;
    double stockX = 100.0;
    double stockY = 80.0;
    double stockZ = 20.0;
    double topZ = 0.0;
    double depth = 2.0;
    double stepDown = 2.0;
    // Centre of the pocket in machine coordinates and its diameter: the
    // operator types the drawing coordinates, the stock is centred on zero.
    double x = 0.0;
    double y = 0.0;
    double diameterPocket = 20.0;
    double finishAllowance = 0.0;
    double feed = 0.0;     // zero: take the table value from feeds.cpp
    double plunge = 0.0;   // zero: a quarter of the feed
    double rpm = 0.0;      // zero: take the table value from feeds.cpp
    Material material = Material::Aluminium;
    bool safeGiven = false;
    double safeZ = 0.0;    // used only when safeGiven
    std::string name = "CIRCLE";
    std::string output;
};

// Parses the words from index `first` (the word after "--op circle-pocket").
bool parseCirclePocketArgs(const std::vector<std::string>& args, std::size_t first,
                           CirclePocketArgs& out);

} // namespace qp
