// tests/checks_drill_layout.cpp - the drill points and the console words (10).
//
// Coordinates are fixed by hand:
//
//   grid   3 x 4 from (10, 10), dx = dy = 15:
//          12 points, x in {10, 25, 40}, y in {10, 25, 40, 55}, row by row in X;
//   circle centre (0, 0), radius 25, 6 holes from 0 degrees, clockwise:
//          (25, 0), (12.5, -21.650635...), (-12.5, -21.650635...),
//          (-25, 0), (-12.5, 21.650635...), (12.5, 21.650635...).
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

#include "tests/checks.h"
#include "tests/checks_drill_ops.h"
#include "tests/harness.h"

namespace drill_checks {

using qpcheck::check;
using qpcheck::contains;
using qpcheck::demoProgram;
using qpcheck::near;


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

} // namespace drill_checks

// Runs the point cases: the grid, the ring and the console words of --op drill.
void runDrillLayoutChecks() {
    drill_checks::checkGridPoints();
    drill_checks::checkCirclePoints();
    drill_checks::checkDrillArgs();
}
