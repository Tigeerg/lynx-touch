// core/drill_options.h - the console arguments of `--op drill` (7.2).
//
// Three shapes behind one command: `--pattern hole|grid|circle`. A hole is one
// point, a grid is (`--cols` x `--rows`, `--dx`, `--dy`), a circle is
// (`--count`, `--radius`, `--start`, `--arc`). The default is a single hole in
// the stock centre, the shape an operator wants most often.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "core/feeds.h"

namespace qp {

enum class DrillPattern { Hole, Grid, Circle };

struct DrillArgs {
    int tool = 1;
    double diameter = 6.0;
    double stockX = 100.0;
    double stockY = 80.0;
    double stockZ = 20.0;
    double topZ = 0.0;
    double depth = 12.0;
    // The R plane: `rGiven` false means top + 1 mm (the planner clamps it).
    bool rGiven = false;
    double r = 0.0;
    // An explicit peck, mm; zero means "the planner decides (G83 when deep)".
    double peck = 0.0;
    // The material of the drill body: `--drill-kind hss|carbide`, HSS default.
    DrillBody kind = DrillBody::Hss;
    DrillPattern pattern = DrillPattern::Hole;

    // Hole: the centre. Grid: the first point and the counts and steps.
    double x = 0.0;
    double y = 0.0;
    int cols = 1;
    int rows = 1;
    double dx = 0.0;
    double dy = 0.0;
    // Circle: the centre, the ring radius, the count, the start angle.
    int count = 4;
    double radius = 20.0;
    double startDeg = 0.0;
    double arcDeg = 360.0;
    bool fullTurn = true;

    double feed = 0.0;     // zero: take the table value from feeds.cpp (drill row)
    double rpm = 0.0;      // zero: take the table value from feeds.cpp
    Material material = Material::Aluminium;
    bool safeGiven = false;
    double safeZ = 0.0;    // used only when safeGiven
    std::string name = "DRILL";
    std::string output;
};

// Parses the words from index `first` (the word after "--op drill").
bool parseDrillArgs(const std::vector<std::string>& args, std::size_t first, DrillArgs& out);

// The pattern word: "hole", "grid", "circle".
bool parseDrillPattern(const std::string& text, DrillPattern& out);

} // namespace qp
