// tests/checks_circle_rules.cpp - the circular pocket rules and its words (10).
//
// A mill that does not fit (D/2 + allowance >= pocket radius) is refused by
// the check with both numbers and by the planner with no turns; the console
// words of `--op circle-pocket` carry the defaults of the help text and fail
// on a missing output or a key of another operation.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "core/circle_pocket_options.h"
#include "core/ops.h"
#include "core/path_circle_pocket.h"
#include "core/validate.h"

#include "tests/checks.h"
#include "tests/checks_circle_ops.h"
#include "tests/harness.h"

namespace circle_checks {

using qpcheck::check;
using qpcheck::near;

// the geometry field with both numbers; the planner plans nothing.
void checkCircleTooBigTool() {
    qp::Program program = circleProgram();
    qp::Operation& op = program.operations.front();
    op.tool.diameterMm = 20.0;
    std::get<qp::CirclePocketOp>(op.geometry).finishAllowanceMm = 1.0;
    const std::vector<qp::Check> problems = qp::validate(program);
    bool named = false;
    std::string reason;
    for (const qp::Check& c : problems) {
        if (c.field == "geometry" && c.reason.find("не влезают") != std::string::npos) {
            named = true;
            reason = c.reason;
        }
    }
    check(named, "circle.tool_too_big", "an 11 mm inset in a F20 pocket must be refused");
    check(reason.find("20") != std::string::npos && reason.find("11") != std::string::npos,
          "circle.tool_too_big_text",
          "the reason must name the pocket diameter and the inset, got '" + reason + "'");
    const qp::CircleStats stats = qp::circleStats(program, program.operations.front());
    check(!stats.fits && stats.turns == 0, "circle.tool_too_big_no_moves",
          "the planner must not plan a pocket the tool does not fit");
}

// `--op circle-pocket`: the defaults of the help text, the shape words and the
// usage errors.
void checkCirclePocketArgs() {
    {
        qp::CirclePocketArgs args;
        const std::vector<std::string> words = {"--op", "circle-pocket", "c.ngc"};
        check(qp::parseCirclePocketArgs(words, 2, args) && args.output == "c.ngc",
              "circle_args.output", "the bare word must be the output file");
        check(args.tool == 1 && near(args.diameter, 10.0) && near(args.diameterPocket, 20.0),
              "circle_args.defaults", "the defaults must be T1, D10, a F20 pocket");
    }
    {
        qp::CirclePocketArgs args;
        const std::vector<std::string> words = {
            "--op", "circle-pocket", "--x", "-5", "--y", "7", "--pocket-dia", "32",
            "--allowance", "0.3", "--depth", "4", "--step-down", "2", "c2.ngc"};
        const bool ok = qp::parseCirclePocketArgs(words, 2, args);
        check(ok && near(args.x, -5.0) && near(args.y, 7.0) && near(args.diameterPocket, 32.0) &&
                  near(args.finishAllowance, 0.3) && near(args.depth, 4.0),
              "circle_args.shape", "every shape word must land in its field");
    }
    {
        qp::CirclePocketArgs args;
        const std::vector<std::string> noOut = {"--op", "circle-pocket", "--depth", "2"};
        check(!qp::parseCirclePocketArgs(noOut, 2, args), "circle_args.no_output",
              "no output file must fail");
        const std::vector<std::string> badKey = {"--op", "circle-pocket", "--size", "5", "c.ngc"};
        check(!qp::parseCirclePocketArgs(badKey, 2, args), "circle_args.unknown_key",
              "a key of another operation must fail");
    }
}



} // namespace circle_checks

// Runs the rule cases: the mill that does not fit and the console words.
void runCircleRuleChecks() {
    circle_checks::checkCircleTooBigTool();
    circle_checks::checkCirclePocketArgs();
}
