// tests/checks_drill_rules.cpp - the drill rules and the .qprog of the kinds (10).
//
// The validation of a grid point outside the stock, a count of zero and a hole
// outside the stock; and the four kinds of a document (grid, ring, hole,
// circular pocket) that come back from the .qprog with their R plane and peck.
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

// Runs the rule cases: the validation fields and the .qprog round-trip.
void runDrillRulesChecks() {
    drill_checks::checkDrillValidation();
    drill_checks::checkDrillRoundTrip();
}
