// core/face_options.h - the console arguments of `--op face` (7.2).
//
// The console is a thin shell over the document: it parses words into named
// fields with defaults and never decides anything about the toolpath. Keeping
// the parser out of main.cpp lets the check exercise the same defaults the
// operator reads in the help text.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/feeds.h"

namespace qp {

struct FaceArgs {
    int tool = 1;
    double diameter = 10.0;
    double stockX = 100.0;
    double stockY = 80.0;
    double stockZ = 20.0;
    double topZ = 0.0;
    bool planeGiven = false;
    double planeZ = 0.0;
    double depth = 2.0;
    double stepDown = 2.0;
    double stepOverPct = 70.0;
    double feed = 0.0;     // zero: take the table value from feeds.cpp
    double plunge = 0.0;   // zero: a quarter of the feed
    double rpm = 0.0;      // zero: take the table value from feeds.cpp
    Material material = Material::Aluminium;
    bool safeGiven = false;
    double safeZ = 0.0;    // used only when safeGiven
    std::string name = "FACE";
    std::string output;
};

// Parses the words of the command line from index `first` (the word after
// "--op face"). False for a missing value, a number that does not parse, an
// unknown key, a second output file or no output file at all: the caller then
// prints the help.
bool parseFaceArgs(const std::vector<std::string>& args, std::size_t first, FaceArgs& out);

} // namespace qp
