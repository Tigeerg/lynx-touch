// tests/checks_circle.cpp - the circular pocket planner and its arcs (10).
//
// The reference case: a F20 pocket, centre at the origin, an 8 mm end mill, no
// allowance. The arithmetic fixed here by hand, so a planner that drifts from
// it fails on a number:
//
//   inset     D/2 + allowance                        = 4.0 mm
//   centre line radius = pocket radius - inset       = 10 - 4 = 6.0 mm
//   nominal   D * 70 / 100                           = 5.6 mm
//   turns     ceil(6.0 / 5.6 - eps)                  = ceil(1.0714) = 2
//   step      6.0 / 2                                = 3.0 mm (never over 5.6)
//   spiral    a half turn of 1.5 mm, a half turn of 4.5 mm and the full turn
//             of 6 mm in two half arcs: pi * (1.5 + 4.5 + 6 + 6) = 56.55 mm
//
// The first move of a pass descends in the centre and then runs the spiral to
// its outer radius; the last turn is the full circle 37.7 mm long: 2 * pi * 6.
// Every turn is written as two half arcs G3 with I/J, the end point of each
// half arc is a point of the circle, so the viewer and the control read the
// same full circle.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "core/emit.h"
#include "core/ops.h"
#include "core/path.h"
#include "core/path_circle_pocket.h"

#include "tests/checks.h"
#include "tests/checks_circle_ops.h"
#include "tests/harness.h"

namespace circle_checks {

using qpcheck::check;
using qpcheck::contains;
using qpcheck::near;

// The layout numbers: two turns, a step of 3 mm, the outer radius 6 mm.
void checkCircleLayout() {
    const qp::Program program = circleProgram();
    const qp::CircleStats stats = qp::circleStats(program, program.operations.front());
    check(stats.fits, "circle.fits", "an 8 mm mill must fit a F20 pocket");
    check(near(stats.insetMm, 4.0), "circle.inset",
          "the inset must be D/2 + allowance = 4 mm, got " + std::to_string(stats.insetMm));
    check(near(stats.outerRadiusMm, 6.0), "circle.outer_radius",
          "the centre line radius must be 10 - 4 = 6 mm, got " +
              std::to_string(stats.outerRadiusMm));
    check(stats.turns == 2, "circle.turns",
          "6 mm over the 5.6 mm nominal step needs two turns, got " +
              std::to_string(stats.turns));
    check(stats.stepMm <= 5.6 + 1e-9, "circle.step_bounded",
          "the real step must not exceed D * 70 % = 5.6 mm, got " + std::to_string(stats.stepMm));
    check(near(stats.stepMm, 3.0), "circle.step",
          "the real step must be 6 / 2 = 3 mm, got " + std::to_string(stats.stepMm));
    // The spiral of this layout is a half turn of the mean radius 1.5 mm, a half
    // turn of 4.5 mm and the outer turn of 6 mm in two half arcs:
    // pi * (1.5 + 4.5 + 12) = 56.55 mm, plus the 1 mm plunge.
    const double arcs = 3.14159265358979323846 * (1.5 + 4.5 + 2.0 * 6.0);
    // One pass: the plunge to the bottom, the arcs, the return along the axis
    // to the centre and the cut up: arcs + 1 + 6 + 1.
    check(near(stats.lengthMm, arcs + 8.0, 1e-6), "circle.length",
          "one pass must cut the spiral arcs, the exit line and the vertical travel, got " +
              std::to_string(stats.lengthMm));
}

// The deepest pass: every arc is a half turn of a circle around the pocket
// centre, the end points stay on the centre line radius, and no point of the
// arc body leaves the pocket shrunk by D/2. The arc centre from I/J is the
// point `end - (I, J)`; the start of the arc is the end of the previous move.
void checkCircleArcs() {
    const qp::Program program = circleProgram();
    const qp::Path planned = qp::planPath(program);
    const double limit = 10.0 - 4.0 + 1e-6;
    int arcs = 0;
    bool inside = true;
    bool centreOk = true;
    bool halfTurn = true;
    for (const qp::Move& m : planned.moves) {
        if (!m.hasArc) { continue; }
        ++arcs;
        // The arc centre is the start plus I/J; the start is the end point
        // minus the chord of the arc, which for a half turn is the diameter.
        // The centre of every arc of the spiral is the pocket centre, so the
        // start point is the mirror image of the end point.
        const double centreX = m.to.x - m.arc.i;
        const double centreY = m.to.y - m.arc.j;
        if (std::fabs(centreX) > 1e-6 || std::fabs(centreY) > 1e-6) { centreOk = false; }
        const double startX = centreX - m.arc.i;
        const double startY = centreY - m.arc.j;
        const double startRadius = std::hypot(startX, startY);
        const double endRadius = std::hypot(m.to.x, m.to.y);
        // A half turn: the start and the end are a diameter apart.
        const double chord = std::hypot(m.to.x - startX, m.to.y - startY);
        if (std::fabs(chord - 2.0 * startRadius) > 1e-6 ||
            std::fabs(startRadius - endRadius) > 1e-6) {
            halfTurn = false;
        }
        if (endRadius > limit || startRadius > limit) { inside = false; }
    }
    check(arcs > 0, "circle.arcs_exist", "the spiral of a circular pocket must be written as arcs");
    check(centreOk, "circle.arc_centre",
          "the I/J of every half arc must put the arc centre on the pocket centre");
    check(halfTurn, "circle.half_turns",
          "every arc must be a half turn of a constant radius");
    check(inside, "circle.arcs_inside",
          "an arc point left the pocket shrunk by D/2 (radius over 6 + 1e-6 mm)");
}

// The full circle of the last turn is two half arcs: I/J are the offsets from
// the start of the arc, the end of the first half arc is the start of the
// second, and the end of the second closes the circle.
void checkCircleHalfArcs() {
    const qp::Program program = circleProgram();
    const qp::Path planned = qp::planPath(program);
    // The arcs at the outer radius, in order, of the last pass.
    std::vector<const qp::Move*> outer;
    for (const qp::Move& m : planned.moves) {
        if (m.hasArc && near(std::hypot(m.to.x, m.to.y), 6.0, 1e-6)) { outer.push_back(&m); }
    }
    check(outer.size() >= 2, "circle.full_turn_two_arcs",
          "the outer turn must be at least two half arcs, got " + std::to_string(outer.size()));
    if (outer.size() < 2) { return; }
    // The first half arc of the outer turn starts on the +X axis at (6, 0) -
    // the spiral point where the radius reaches the outer turn - and ends on
    // the -X axis. I/J are the offsets from that start, not from the centre
    // of the pocket.
    const qp::Move& first = *outer[0];
    const qp::Move& second = *outer[1];
    check(near(first.arc.i, -6.0, 1e-6) && near(first.arc.j, 0.0, 1e-6), "circle.ij_first",
          "I/J of the first half arc must be the centre offset -6, 0 from its start");
    check(near(first.to.x, -6.0, 1e-6) && near(first.to.y, 0.0, 1e-6), "circle.first_half_end",
          "the first half arc must end at (-6, 0)");
    check(near(second.arc.i, 6.0, 1e-6) && near(second.arc.j, 0.0, 1e-6), "circle.ij_second",
          "I/J of the second half arc must be the offset 6, 0 from its start");
    check(near(second.to.x, 6.0, 1e-6) && near(second.to.y, 0.0, 1e-6), "circle.second_half_end",
          "the second half arc must close the circle at (6, 0)");
    check(!first.arc.cw && !second.arc.cw, "circle.counterclockwise",
          "the spiral climbs counter-clockwise: both half arcs must be G3");
}

// Emit: the arcs are written as G3 with I/J and an end point, the plunge is a
// cut at the plunge feed, and the tool does not rapid into the material.
void checkCircleEmit() {
    const qp::Program program = circleProgram();
    const qp::Path planned = qp::planPath(program);
    const std::string gcode = qp::emitProgram(program, planned);
    check(contains(gcode, " G3 "), "circle.emit_g3", "the circular pocket must be written with G3");
    check(contains(gcode, " I-6.000 J0.000"), "circle.emit_ij",
          "the first half arc must carry I-6.000 J0.000 as offsets from the start");
    bool rapidIntoMaterial = false;
    for (const qp::Move& m : planned.moves) {
        if (m.kind == qp::MoveKind::Rapid && m.to.z < -1e-6) { rapidIntoMaterial = true; }
    }
    check(!rapidIntoMaterial, "circle.rapid_above_top", "a rapid went below the material top");
    const qp::CircleStats stats = qp::circleStats(program, program.operations.front());
    check(near(stats.lengthMm, planned.lengthMm, 1e-6), "circle.length_matches_moves",
          "the stats length must match the planned cutting length within 0.5 mm, got " +
              std::to_string(stats.lengthMm) + " vs " + std::to_string(planned.lengthMm));
}

// A mill that does not fit: D/2 + allowance >= pocket radius. The check names

} // namespace circle_checks

// Runs the arc cases of the circular pocket.
void runCircleChecks() {
    circle_checks::checkCircleLayout();
    circle_checks::checkCircleArcs();
    circle_checks::checkCircleHalfArcs();
    circle_checks::checkCircleEmit();
}
