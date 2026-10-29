// tests/checks_face.cpp - the face planner and its numbers (PLAN-QUICKPROG, 10).
//
// The strips, the coverage, the pass depths and the warnings of the face
// operation on a 100x80x20 stock. The cutting data case sits here too: the feed
// table and the face planner are one path from calc.ts to a strip.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "core/feeds.h"
#include "core/ops.h"
#include "core/path.h"
#include "core/validate.h"

#include "tests/harness.h"

namespace face_checks {

using qpcheck::check;
using qpcheck::contains;
using qpcheck::faceOp;
using qpcheck::faceProgram;
using qpcheck::near;

// The cutting data comes from calc.ts through feeds: Vc 150 m/min on a 10 mm
// tool is 4775 rpm, fz 0.06 on three flutes is 860 mm/min (calc.test.ts, lines
// 7 and 26). The number of flutes is fixed at three, as in the React defaults.
void checkFeeds() {
    const qp::Feeds alu = qp::feedsFor(qp::Material::Aluminium, 10.0, qp::ToolKind::Flat);
    check(alu.rpm == 4775.0, "feeds.rpm_alu_d10", "Vc 150 on D10 must be 4775 rpm, got " +
                                                     std::to_string(alu.rpm));
    check(alu.feedMmMin == 860.0, "feeds.feed_alu_d10", "fz 0.06 x 3 x 4775 must be 860 mm/min, got " +
                                                            std::to_string(alu.feedMmMin));
    check(alu.plungeFeedMmMin == 215.0, "feeds.plunge_alu_d10",
          "the plunge is a quarter of the feed, expected 215 mm/min");
    check(alu.source == "таблица: алюминий, ф10", "feeds.source_alu",
          "the source must name the material and the diameter, got '" + alu.source + "'");

    const qp::Feeds steel = qp::feedsFor(qp::Material::Steel, 10.0, qp::ToolKind::Flat);
    check(steel.rpm == 2865.0, "feeds.rpm_steel_d10", "Vc 90 on D10 must be 2865 rpm");
    check(steel.feedMmMin == 258.0, "feeds.feed_steel_d10", "fz 0.03 x 3 x 2865 must be 258 mm/min");

    // A drill is rated per revolution, not per tooth: the `rule of thumb` row
    // is f = 0.02 * D mm/rev and Vc 25 m/min on an HSS drill (feeds.cpp), so
    // 0.02 * 10 = 0.2 mm/rev at round(1000 * 25 / (pi * 10)) = 796 rpm gives
    // 0.2 * 796 = 159.2 -> 159 mm/min. The previous numbers (fz 0.03 on two
    // flutes, Vc 150) were not a drill rule at all and are replaced here.
    const qp::Feeds drill = qp::feedsFor(qp::Material::Aluminium, 10.0, qp::ToolKind::Drill);
    check(drill.rpm == 796.0, "feeds.drill_rpm_d10",
          "Vc 25 on D10 must be 796 rpm, got " + std::to_string(drill.rpm));
    check(drill.feedMmMin == 159.0, "feeds.drill_alu_d10",
          "a drill takes f = 0.02 * D = 0.2 mm/rev at 796 rpm, 159 mm/min, got " +
              std::to_string(drill.feedMmMin));
    check(drill.source.find("правило большого пальца") != std::string::npos,
          "feeds.drill_rule_of_thumb", "the drill source must say it is a rule of thumb");

    const qp::Feeds none = qp::feedsFor(qp::Material::Aluminium, 0.0, qp::ToolKind::Flat);
    check(none.rpm == 0.0 && none.feedMmMin == 0.0, "feeds.zero_diameter", "a zero diameter gives zeros");
}

// The stock is 100x80: the strips run along X, so the cross direction is Y.
// The tool centre must span the whole usable width `80 - 10 = 70 mm` from one
// `D/2` line to the other, so a 70 % step of 7 mm needs
// ceil(70 / 7) + 1 = 12 strips (11 intervals of 6.3636 mm), not 11 centred ones.
void checkFaceStrips() {
    const qp::Program program = faceProgram();
    const qp::Path planned = qp::planPath(program);
    const qp::FaceStats stats = qp::faceStats(program, program.operations.front());
    check(stats.strips == 11, "face.strips",
          "100x80 stock, D10, 70 % covers with 11 strips, got " + std::to_string(stats.strips));
    check(stats.passes == 3, "face.passes", "5 mm in 2 mm steps must be 3 passes, got " +
                                               std::to_string(stats.passes));
    check(!planned.moves.empty(), "face.moves", "the face planner returned no moves");
}

// Strips run along X: the Y coordinate of every cutting strip is fixed and the
// X coordinate sweeps the whole strip. The first and the last strip sit half a
// tool inside the stock edge.
void checkFaceStripsAlongX() {
    const qp::Program program = faceProgram();
    const qp::Path planned = qp::planPath(program);
    // A strip is a cut at a constant Z; the plunge and the retract are cuts at
    // a constant X/Y. Collect the Z of the deepest cutting plane and the
    // distinct Y values of the long cuts at that plane.
    bool havePlane = false;
    double minZ = 0.0;
    for (const qp::Move& m : planned.moves) {
        if (m.kind == qp::MoveKind::Cut && (!havePlane || m.to.z < minZ)) {
            minZ = m.to.z;
            havePlane = true;
        }
    }
    std::vector<double> stripY;
    for (std::size_t i = 1; i < planned.moves.size(); ++i) {
        const qp::Move& m = planned.moves[i];
        if (m.kind != qp::MoveKind::Cut || !near(m.to.z, minZ) || m.hasArc) { continue; }
        const qp::Move& prev = planned.moves[i - 1];
        // A strip moves along X at one Z; the plunge and the ramp change Z.
        if (!near(m.to.z, prev.to.z)) { continue; }
        if (std::fabs(m.to.x - prev.to.x) < 1.0) { continue; }
        if (stripY.empty() || !near(stripY.back(), m.to.y)) { stripY.push_back(m.to.y); }
    }
    // One pass is a plunge plus one long cut per strip, so the last pass at the
    // deepest Z is exactly as many strip cuts as there are strips.
    check(static_cast<int>(stripY.size()) == 11, "face.strips_along_x",
          "the last pass must sweep 11 strips along X, got " + std::to_string(stripY.size()) +
              " cuts at the deepest Z");
    // First and last strip sit exactly half a tool inside each edge: the tool
    // body reaches the edge with its centre, not its far flank.
    check(!stripY.empty() && near(stripY.front(), -35.0), "face.first_strip_edge",
          "the first strip must sit 5 mm inside Y=-40, got " +
              (stripY.empty() ? std::string("no strip") : std::to_string(stripY.front())));
    check(!stripY.empty() && near(stripY.back(), 35.0), "face.last_strip_edge",
          "the last strip must sit 5 mm inside Y=+40, got " +
              (stripY.empty() ? std::string("no strip") : std::to_string(stripY.back())));
    // The tool centre goes 10 % of D past both ends, clamped to 1..D/2 mm: on
    // D10 that is 1 mm per side, so the centre span is 100 + 2 * 1 = 102 mm.
    check(near(planned.bbox.xMax - planned.bbox.xMin, 102.0), "face.bbox_x",
          "the tool centre must span 102 mm along X, got " +
              std::to_string(planned.bbox.xMax - planned.bbox.xMin));
    check(planned.bbox.valid && near(planned.bbox.zMax, 5.0), "face.bbox_z",
          "the highest point of the path must be safe Z");
}

// Every cut stays inside the stock grown by the required edge overrun; no
// rapid goes below safe Z.
void checkFaceBounds() {
    const qp::Program program = faceProgram();
    const qp::Path planned = qp::planPath(program);
    const qp::FaceStats stats = qp::faceStats(program, program.operations.front());
    // The overrun along X is 10 % of the diameter = 1 mm per side, and the
    // strip centre stays half a tool inside Y: the tool body touches the edge.
    const double overrun = stats.edgeOverrunMm;
    const double xMin = program.stock.min.x - overrun;
    const double xMax = program.stock.max.x + overrun;
    const double yMin = program.stock.min.y + stats.toolRadiusMm;
    const double yMax = program.stock.max.y - stats.toolRadiusMm;

    bool inBounds = true;
    bool rapidSafe = true;
    for (const qp::Move& m : planned.moves) {
        if (m.kind == qp::MoveKind::Cut) {
            if (m.to.x < xMin - 1e-6 || m.to.x > xMax + 1e-6 || m.to.y < yMin - 1e-6 ||
                m.to.y > yMax + 1e-6) {
                inBounds = false;
            }
        } else if (m.to.z < program.operations.front().topZ - 1e-6 &&
                   m.to.z < program.stock.max.z - 1e-6) {
            // A rapid may leave safe Z only to land on the material top: below
            // the top inside the stock is a crash, the descent there is a cut.
            rapidSafe = false;
        }
    }
    check(inBounds, "face.cuts_in_bounds", "a cutting move left the stock plus the edge overrun");
    check(rapidSafe, "face.rapid_not_below_top", "a rapid went below the material top");
}

// 5 mm depth in 2 mm steps: 2 + 2 + 1. The last pass is exactly the bottom,
// every earlier one is a multiple of the step.
void checkFacePasses() {
    const qp::Program program = faceProgram();
    const qp::Path planned = qp::planPath(program);
    // The Z planes of the cutting passes, ordered from the top down: a strip
    // cut moves in XY only at the plane, so it marks the pass bottom.
    std::vector<double> planes;
    for (std::size_t i = 1; i < planned.moves.size(); ++i) {
        const qp::Move& m = planned.moves[i];
        if (m.kind != qp::MoveKind::Cut) { continue; }
        const qp::Move& prev = planned.moves[i - 1];
        if (!near(m.to.z, prev.to.z)) { continue; }
        // A plunge and a ramp change Z; only a long cut in XY at one Z is a
        // strip. The length separates them without knowing the planner.
        const double dx = m.to.x - prev.to.x;
        const double dy = m.to.y - prev.to.y;
        if (std::sqrt(dx * dx + dy * dy) < 1.0) { continue; }
        if (planes.empty() || !near(planes.back(), m.to.z)) { planes.push_back(m.to.z); }
    }
    // Even a single-plane pass shows up once; the pass count comes from the
    // stats, the planes from the moves.
    const qp::FaceStats stats = qp::faceStats(program, program.operations.front());
    check(stats.passes == 3, "face.passes_5mm", "5 mm / 2 mm must be 3 passes");
    check(!planes.empty() && near(planes.back(), -5.0), "face.last_pass_bottom",
          "the last pass must reach the bottom -5, got " +
              (planes.empty() ? std::string("no strip cut at a plane") : std::to_string(planes.back())));
    check(stats.stepDepthsMm.size() == 3, "face.step_count",
          "the stats must list three pass depths, got " + std::to_string(stats.stepDepthsMm.size()));
    check(near(stats.stepDepthsMm[0], -2.0) && near(stats.stepDepthsMm[1], -4.0) &&
              near(stats.stepDepthsMm[2], -5.0),
          "face.step_depths", "the pass depths must be -2, -4, -5");
}

} // namespace face_checks

// Runs the cases of this file: the entry point calls the four runners in the
// order of the slices.
void runFaceChecks() {
    face_checks::checkFeeds();
    face_checks::checkFaceStrips();
    face_checks::checkFaceStripsAlongX();
    face_checks::checkFaceBounds();
    face_checks::checkFacePasses();
}
