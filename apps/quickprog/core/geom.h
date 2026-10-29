// core/geom.h - named geometry types of the quick program (PLAN-QUICKPROG, 6).
//
// Units: millimetres everywhere, machine coordinates unless a field says
// otherwise. The types are separate instead of tuples: a Rect and a Circle
// travel through the planner, the emitter and later the form, and a pair of
// numbers says nothing about which edge is which (CLAUDE.md, section 4).
#pragma once

#include <vector>

namespace qp {

// A point in machine coordinates, mm. `z` is the tool tip (centre of the
// trajectory): the program is written without cutter radius compensation.
struct Point3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// Axis-aligned rectangle in the XY plane. `x`/`y` is the lower-left corner of
// the pocket contour: the planner cuts inside it and the corners are treated as
// sharp until a corner radius appears (see RoundedRect).
struct Rect {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
};

// A circle in the XY plane: centre and radius, mm.
struct Circle {
    double x = 0.0;
    double y = 0.0;
    double r = 0.0;
};

// Open or closed chain of points in the XY plane at a constant Z. `closed`
// makes the last segment return to the first point; the planner does not repeat
// the first point at the end.
struct Polyline {
    std::vector<Point3> points;
    bool closed = false;
};

// Rectangle with a corner radius, mm. `r` is clamped by the planner to half of
// the shorter side; r = 0 is a sharp rectangle. Declared for the contour and
// pocket work of the next slices: the model keeps the shape, the planner reads
// it when it learns the corners.
struct RoundedRect {
    Rect rect;
    double r = 0.0;
};

} // namespace qp
