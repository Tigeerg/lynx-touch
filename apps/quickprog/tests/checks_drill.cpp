// tests/checks_drill.cpp - drilling: one hole, a grid, a circle, G81/G83 (10).
//
// Coordinates are fixed by hand:
//
//   grid   3 x 4 from (10, 10), dx = dy = 15:
//          12 points, x in {10, 25, 40}, y in {10, 25, 40, 55}, row by row in X;
//   circle centre (0, 0), radius 25, 6 holes from 0 degrees, clockwise:
//          (25, 0), (12.5, -21.650635...), (-12.5, -21.650635...),
//          (-25, 0), (-12.5, 21.650635...), (12.5, 21.650635...);
//   R plane = top + 1, the rapids of a cycle stay at or above it.
//
// G81 when the depth is at most three diameters and no peck is asked for;
// G83 with Q for a deeper hole or an explicit peck; G80 closes the cycle.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "core/drill_options.h"
#include "core/emit.h"
#include "core/feeds.h"
#include "core/ops.h"
#include "core/path.h"
#include "core/path_drill.h"
#include "core/serialize.h"
#include "core/validate.h"

#include "tests/harness.h"

namespace drill_checks {

using qpcheck::check;
using qpcheck::contains;
using qpcheck::demoProgram;
using qpcheck::near;

qp::Operation drillOp(qp::OperationGeometry geometry, double diameter, double depth) {
    qp::Operation op;
    op.id = "Отверстия";
    op.tool.number = 5;
    op.tool.diameterMm = diameter;
    op.topZ = 0.0;
    op.depthMm = depth;
    op.stepDownMm = depth;
    const qp::Feeds table = qp::feedsFor(qp::Material::Aluminium, diameter, qp::ToolKind::Drill);
    op.cutting.spindleRpm = table.rpm;
    op.cutting.feedMmMin = table.feedMmMin;
    op.cutting.plungeFeedMmMin = table.plungeFeedMmMin;
    op.geometry = geometry;
    return op;
}

qp::DrillGridOp grid() {
    qp::DrillGridOp g;
    g.x0 = 10.0;
    g.y0 = 10.0;
    g.cols = 3;
    g.rows = 4;
    g.dx = 15.0;
    g.dy = 15.0;
    return g;
}

qp::Program gridProgram() {
    qp::Program program = demoProgram();
    program.name = "DRILL GRID";
    program.operations.push_back(drillOp(grid(), 6.0, 12.0));
    return program;
}

qp::Program circleDrillProgram() {
    qp::Program program = demoProgram();
    program.name = "DRILL CIRCLE";
    qp::DrillCircleOp ring;
    ring.circle = qp::Circle{0.0, 0.0, 25.0};
    ring.count = 6;
    ring.startDeg = 0.0;
    program.operations.push_back(drillOp(ring, 6.0, 12.0));
    return program;
}

// 3 x 4 grid: twelve points, row by row, in the expected coordinates.
void checkGridPoints() {
    const qp::Program program = gridProgram();
    const qp::DrillStats stats = qp::drillStats(program, program.operations.front());
    check(static_cast<int>(stats.points.size()) == 12, "drill.grid_count",
          "a 3 x 4 grid must have 12 holes, got " + std::to_string(stats.points.size()));
    const std::vector<qp::Point3> expected = [&]() {
        std::vector<qp::Point3> out;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 3; ++col) {
                out.push_back(qp::Point3{10.0 + 15.0 * col, 10.0 + 15.0 * row, 0.0});
            }
        }
        return out;
    }();
    bool same = stats.points.size() == expected.size();
    for (std::size_t i = 0; same && i < expected.size(); ++i) {
        same = near(stats.points[i].x, expected[i].x, 1e-9) &&
               near(stats.points[i].y, expected[i].y, 1e-9);
    }
    check(same, "drill.grid_coords", "the grid must be row by row in the expected coordinates");
    check(stats.points.empty() ||
              (near(stats.points.front().x, 10.0) && near(stats.points.front().y, 10.0)),
          "drill.grid_first", "the first hole must be at the start point (10, 10)");
}

// Circle: six holes on a radius of 25 from 0 degrees, clockwise. The numbers
// carry a cosine and a sine of 60 degrees; 1e-9 is the tolerance of the plan,
// the value is computed with the same libm the planner calls.
void checkCirclePoints() {
    const qp::Program program = circleDrillProgram();
    const qp::DrillStats stats = qp::drillStats(program, program.operations.front());
    check(static_cast<int>(stats.points.size()) == 6, "drill.circle_count",
          "six holes were asked, got " + std::to_string(stats.points.size()));
    const double s = 25.0 * std::sin(M_PI / 3.0);
    const std::vector<std::pair<double, double>> expected = {
        {25.0, 0.0}, {12.5, -s}, {-12.5, -s}, {-25.0, 0.0}, {-12.5, s}, {12.5, s}};
    bool same = stats.points.size() == expected.size();
    for (std::size_t i = 0; same && i < expected.size(); ++i) {
        same = near(stats.points[i].x, expected[i].first, 1e-9) &&
               near(stats.points[i].y, expected[i].second, 1e-9);
    }
    check(same, "drill.circle_coords", "the ring must be six holes from 0 degrees clockwise");
}

// The cycle: G81 for a shallow hole, G83 with Q for a deep one, G80 closes the
// cycle after the last hole.
void checkCycles() {
    // 12 mm through a 6 mm drill: two diameters, G81.
    const qp::Program shallow = gridProgram();
    const std::string shallowG = qp::emitProgram(shallow, qp::planPath(shallow));
    check(contains(shallowG, "G81"), "drill.g81", "a 12 mm hole in a 6 mm drill must use G81");
    check(!contains(shallowG, "G83"), "drill.no_g83_for_shallow",
          "a shallow hole must not be pecked");
    check(contains(shallowG, "G80"), "drill.g80", "the cycle must be closed with G80");
    check(contains(shallowG, "Z-12.000"), "drill.depth", "the hole must be 12 mm deep");
    check(contains(shallowG, "R1.000"), "drill.r_plane",
          "the R plane must be 1 mm above the top");

    // 24 mm is four diameters: G83 with the default peck Q = D = 6.
    qp::Program deep = gridProgram();
    deep.operations.front().depthMm = 24.0;
    deep.operations.front().stepDownMm = 24.0;
    const qp::DrillStats deepStats = qp::drillStats(deep, deep.operations.front());
    check(deepStats.cycle == qp::DrillKind::Peck && near(deepStats.peckMm, 6.0), "drill.g83_auto",
          "a hole deeper than three diameters must be pecked with Q = D");
    const std::string deepG = qp::emitProgram(deep, qp::planPath(deep));
    check(contains(deepG, "G83"), "drill.g83", "a deep hole must use G83");
    check(contains(deepG, "Q6.000"), "drill.q_word", "G83 must carry Q6.000, the tool diameter");
    check(contains(deepG, "G80"), "drill.g83_g80", "the deep cycle must be closed with G80");

    // A shallow hole with an explicit peck: G83 with the asked Q.
    qp::Program pecked = gridProgram();
    std::get<qp::DrillGridOp>(pecked.operations.front().geometry).params.peckMm = 2.5;
    const std::string peckG = qp::emitProgram(pecked, qp::planPath(pecked));
    check(contains(peckG, "G83") && contains(peckG, "Q2.500"), "drill.explicit_peck",
          "an explicit peck must select G83 with its own Q");
}

// The R plane and rapids: R is above the top and never below it; a rapid of a
// cycle does not descend below R.
void checkRPlane() {
    const qp::Program program = gridProgram();
    const qp::DrillStats stats = qp::drillStats(program, program.operations.front());
    check(stats.rMm > program.operations.front().topZ + 1e-9, "drill.r_above_top",
          "the R plane must be above the material top");
    check(stats.rMm < program.safeZ, "drill.r_below_safe",
          "the R plane must be below the safe Z");
    const qp::Path planned = qp::planPath(program);
    bool rapidBelowR = false;
    for (const qp::Move& m : planned.moves) {
        if (m.kind == qp::MoveKind::Rapid && m.to.z < stats.rMm - 1e-6) { rapidBelowR = true; }
    }
    check(!rapidBelowR, "drill.rapid_not_below_r", "a rapid of the cycle descended below R");
}

// Validation: a grid point outside the stock, a count of zero, a hole outside
// the stock. Every rule names the field.
void checkDrillValidation() {
    {
        qp::Program program = gridProgram();
        std::get<qp::DrillGridOp>(program.operations.front().geometry).dx = 40.0;
        const std::vector<qp::Check> bad = qp::validate(program);
        bool named = false;
        for (const qp::Check& c : bad) {
            if (c.field == "geometry" && c.reason.find("вне заготовки") != std::string::npos) {
                named = true;
            }
        }
        check(named, "drill.grid_outside", "a grid point outside the stock must be refused");
    }
    {
        qp::Program program = gridProgram();
        std::get<qp::DrillGridOp>(program.operations.front().geometry).cols = 0;
        const std::vector<qp::Check> bad = qp::validate(program);
        check(!bad.empty() && bad.front().field == "geometry", "drill.grid_zero_count",
              "a zero column count must fail on geometry");
    }
    {
        qp::Program program = circleDrillProgram();
        std::get<qp::DrillCircleOp>(program.operations.front().geometry).count = 0;
        const std::vector<qp::Check> bad = qp::validate(program);
        check(!bad.empty() && bad.front().field == "geometry", "drill.circle_zero_count",
              "a zero hole count must fail on geometry");
    }
    {
        qp::Program program = demoProgram();
        qp::DrillHoleOp hole;
        // The body of the hole is the tool: a 6 mm drill at x = 49 leaves the
        // stock 50 mm wide by its own radius.
        hole.hole = qp::Circle{49.0, 0.0, 3.0};
        program.operations.push_back(drillOp(hole, 6.0, 12.0));
        const std::vector<qp::Check> bad = qp::validate(program);
        check(!bad.empty() && bad.front().field == "geometry", "drill.hole_outside",
              "a hole whose body leaves the stock must be refused");
    }
}

// The single hole: one cycle, one point, the tool change before it.
void checkSingleHole() {
    qp::Program program = demoProgram();
    qp::DrillHoleOp hole;
    hole.hole = qp::Circle{-20.0, 15.0, 3.0};
    program.operations.push_back(drillOp(hole, 6.0, 10.0));
    const qp::DrillStats stats = qp::drillStats(program, program.operations.front());
    check(stats.points.size() == 1 && near(stats.points.front().x, -20.0) &&
              near(stats.points.front().y, 15.0),
          "drill.single_point", "one hole must give one point at its centre");
    const std::string gcode = qp::emitProgram(program, qp::planPath(program));
    check(contains(gcode, " T5 M6\n"), "drill.emit_tool", "the drill must call T5 M6");
    check(contains(gcode, "(UI_TOOL: T=5 TYPE=DRILL D=6.)"), "drill.emit_type",
          "the tool tag must say DRILL");
    check(contains(gcode, "G81"), "drill.emit_g81", "the single hole must use G81");
}

// `--op drill`: the defaults of the help text, the shape words of the three
// patterns and the usage errors.
void checkDrillArgs() {
    {
        qp::DrillArgs args;
        const std::vector<std::string> words = {"--op", "drill", "d.ngc"};
        check(qp::parseDrillArgs(words, 2, args) && args.output == "d.ngc", "drill_args.output",
              "the bare word must be the output file");
        check(args.pattern == qp::DrillPattern::Hole && near(args.diameter, 6.0),
              "drill_args.defaults", "the defaults must be a single hole with a 6 mm drill");
    }
    {
        qp::DrillArgs args;
        const std::vector<std::string> words = {
            "--op", "drill", "--pattern", "grid", "--x", "-20", "--y", "-20", "--cols", "3",
            "--rows", "4", "--dx", "15", "--dy", "13", "--depth", "12", "--r", "2",
            "--peck", "5", "--diameter", "6", "g.ngc"};
        const bool ok = qp::parseDrillArgs(words, 2, args);
        check(ok && args.pattern == qp::DrillPattern::Grid && args.cols == 3 && args.rows == 4 &&
                  near(args.dx, 15.0) && near(args.dy, 13.0) && near(args.depth, 12.0) &&
                  args.rGiven && near(args.r, 2.0) && near(args.peck, 5.0),
              "drill_args.grid", "every grid word must land in its field");
    }
    {
        qp::DrillArgs args;
        const std::vector<std::string> words = {"--op", "drill", "--pattern", "circle", "--count",
                                                "6",   "--radius", "25",     "--start", "30",
                                                "--arc", "180",     "c.ngc"};
        const bool ok = qp::parseDrillArgs(words, 2, args);
        check(ok && args.pattern == qp::DrillPattern::Circle && args.count == 6 &&
                  near(args.radius, 25.0) && near(args.startDeg, 30.0) && !args.fullTurn &&
                  near(args.arcDeg, 180.0),
              "drill_args.circle", "every circle word must land in its field");
    }
    {
        qp::DrillArgs args;
        const std::vector<std::string> bad = {"--op", "drill", "--pattern", "spiral", "x.ngc"};
        check(!qp::parseDrillArgs(bad, 2, args), "drill_args.bad_pattern",
              "an unknown pattern must fail");
        qp::DrillArgs noOut;
        const std::vector<std::string> onlyKeys = {"--op", "drill", "--depth", "2"};
        check(!qp::parseDrillArgs(onlyKeys, 2, noOut), "drill_args.no_output",
              "no output file must fail");
    }
}

// `.qprog`: the three drilling kinds and the circular pocket come back with
// their fields, including the R plane of a drill and the peck.
void checkDrillRoundTrip() {
    qp::Program program = demoProgram();
    program.operations.push_back(drillOp(grid(), 6.0, 12.0));
    qp::DrillCircleOp ring;
    ring.circle = qp::Circle{0.0, 0.0, 25.0};
    ring.count = 6;
    ring.params.peckMm = 4.0;
    ring.params.rMm = 2.0;
    ring.params.hasR = true;
    program.operations.push_back(drillOp(ring, 6.0, 24.0));
    qp::DrillHoleOp hole;
    hole.hole = qp::Circle{-20.0, 15.0, 3.0};
    program.operations.push_back(drillOp(hole, 6.0, 10.0));
    // A circular pocket in the same document: the .qprog of four kinds is the
    // file the console writes for a program of several operations.
    qp::Operation circle;
    circle.id = "Круг";
    circle.tool.number = 4;
    circle.tool.diameterMm = 8.0;
    circle.cutting.spindleRpm = 5968.0;
    circle.cutting.feedMmMin = 859.0;
    circle.cutting.plungeFeedMmMin = 215.0;
    circle.topZ = 0.0;
    circle.depthMm = 4.0;
    circle.stepDownMm = 2.0;
    qp::CirclePocketOp pocket;
    pocket.circle = qp::Circle{0.0, 0.0, 10.0};
    pocket.finishAllowanceMm = 0.1;
    circle.geometry = pocket;
    program.operations.push_back(circle);

    const std::string text = qp::serialize(program);
    const qp::ParseOutcome parsed = qp::parseProgram(text);
    check(parsed.ok, "qprog.drill_round_trip_ok", parsed.ok ? "" : parsed.error);
    if (!parsed.ok) { return; }
    check(parsed.program.operations.size() == 4, "qprog.drill_round_trip_count",
          "four operations must come back");
    bool same = parsed.program.operations.size() == 4;
    if (same) {
        const auto& back = parsed.program.operations;
        same = std::holds_alternative<qp::DrillGridOp>(back[0].geometry) &&
               std::holds_alternative<qp::DrillCircleOp>(back[1].geometry) &&
               std::holds_alternative<qp::DrillHoleOp>(back[2].geometry) &&
               std::holds_alternative<qp::CirclePocketOp>(back[3].geometry);
    }
    check(same, "qprog.drill_round_trip_kinds", "every kind must come back as itself");
    if (!same) { return; }
    const qp::DrillCircleOp& r = std::get<qp::DrillCircleOp>(parsed.program.operations[1].geometry);
    check(r.params.hasR && near(r.params.rMm, 2.0) && near(r.params.peckMm, 4.0),
          "qprog.drill_round_trip_r", "the R plane and the peck must come back");
    // A drill without an explicit R leaves the key out; the reader then has
    // the default R and `hasR` false.
    check(!std::get<qp::DrillGridOp>(parsed.program.operations[0].geometry).params.hasR,
          "qprog.drill_default_r", "a drill without op.r must read as the default R");
    check(qp::serialize(parsed.program) == text, "qprog.drill_round_trip_stable",
          "a second serialization must be byte-identical");
}

} // namespace drill_checks

void runDrillChecks() {
    drill_checks::checkGridPoints();
    drill_checks::checkCirclePoints();
    drill_checks::checkCycles();
    drill_checks::checkRPlane();
    drill_checks::checkDrillValidation();
    drill_checks::checkSingleHole();
    drill_checks::checkDrillArgs();
    drill_checks::checkDrillRoundTrip();
}
