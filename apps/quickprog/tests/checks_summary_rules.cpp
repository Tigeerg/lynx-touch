// tests/checks_summary_rules.cpp - the facing lead, the bounds and the drill rules.
//
// The fix of the technologist's review: the facing lead is max(10 % D, 1 mm)
// with no upper bound of R, the stock plus the lead is the honest bound of a
// cut, and the drill obeys two rules - a peck deeper than the hole (or under
// 0.1 mm) is refused, and a drill has one feed for the cut and the plunge.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "core/emit.h"
#include "core/feeds.h"
#include "core/ops.h"
#include "core/path.h"
#include "core/path_drill.h"
#include "core/path_pocket.h"
#include "core/serialize.h"
#include "core/summary.h"
#include "core/validate.h"

#include "tests/checks.h"
#include "tests/harness.h"

namespace summary_checks {

using qpcheck::check;
using qpcheck::contains;
using qpcheck::count;
using qpcheck::demoProgram;
using qpcheck::near;


// The facing lead: max(10 % D, 1 mm), no upper bound of R. A D2 tool with a
// 1 mm radius leaves 1 mm, not 0.2 and not the radius.
void checkFacingLead() {
    qp::Program small = qpcheck::faceProgram();
    small.operations.front().tool.diameterMm = 2.0;
    const qp::FaceStats smallStats = qp::faceStats(small, small.operations.front());
    check(near(smallStats.edgeOverrunMm, 1.0), "facing.lead_d2",
          "a D2 tool must lead 1 mm (10 % D is 0.2), got " +
              std::to_string(smallStats.edgeOverrunMm));

    qp::Program big = qpcheck::faceProgram();
    big.operations.front().tool.diameterMm = 40.0;
    big.operations.front().cutting.spindleRpm = 1194.0;
    big.operations.front().cutting.feedMmMin = 215.0;
    big.operations.front().cutting.plungeFeedMmMin = 54.0;
    const qp::FaceStats bigStats = qp::faceStats(big, big.operations.front());
    check(near(bigStats.edgeOverrunMm, 4.0), "facing.lead_d40",
          "a D40 tool must lead 10 % D = 4 mm, above its 20 mm radius half, got " +
              std::to_string(bigStats.edgeOverrunMm));
}

// The stock plus the lead is the honest bound: the check recomputes the lead
// by the rule of the planner, not by a constant, so a D2 path is checked with
// its own 1 mm.
void checkBoundsRule() {
    qp::Program program = qpcheck::faceProgram();
    program.operations.front().tool.diameterMm = 2.0;
    const qp::Path planned = qp::planPath(program);
    const double lead = std::max(0.10 * 2.0, 1.0);
    bool inside = true;
    for (const qp::Move& move : planned.moves) {
        if (move.kind != qp::MoveKind::Cut) { continue; }
        if (move.to.x < program.stock.min.x - lead - 1e-6 ||
            move.to.x > program.stock.max.x + lead + 1e-6) {
            inside = false;
        }
    }
    check(inside, "facing.bounds_rule",
          "a cut left the stock plus max(10 % D, 1 mm) of the D2 tool");
}

qp::Operation drillOp(double diameter, double depth, double peck) {
    qp::Operation op;
    op.id = "Отверстия";
    op.tool.number = 5;
    op.tool.diameterMm = diameter;
    op.topZ = 0.0;
    op.depthMm = depth;
    op.stepDownMm = 0.0;
    const qp::Feeds table = qp::feedsFor(qp::Material::Aluminium, diameter, qp::ToolKind::Drill);
    op.cutting.spindleRpm = table.rpm;
    op.cutting.feedMmMin = table.feedMmMin;
    op.cutting.plungeFeedMmMin = table.feedMmMin;
    qp::DrillHoleOp hole;
    hole.hole = qp::Circle{0.0, 0.0, diameter / 2.0};
    hole.params.peckMm = peck;
    op.geometry = hole;
    return op;
}

// A peck deeper than the hole is a broken cycle: it is a blocking check on
// `peck`, and a peck under 0.1 mm is refused by the same rule.
void checkPeckLimit() {
    {
        qp::Program program = demoProgram();
        program.operations.push_back(drillOp(6.0, 10.0, 99.0));
        const std::vector<qp::Check> bad = qp::validate(program);
        bool named = false;
        std::string reason;
        for (const qp::Check& c : bad) {
            if (c.field == "peck") {
                named = true;
                reason = c.reason;
            }
        }
        check(named, "peck.above_depth", "Q99 in a 10 mm hole must fail on peck");
        check(contains(reason, "Q99") && contains(reason, "Q99 мм больше глубины отверстия 10"),
              "peck.above_depth_text",
              "the reason must name Q and the depth, got '" + reason + "'");
    }
    {
        qp::Program program = demoProgram();
        program.operations.push_back(drillOp(6.0, 10.0, 0.05));
        const std::vector<qp::Check> bad = qp::validate(program);
        bool named = false;
        for (const qp::Check& c : bad) {
            if (c.field == "peck") { named = true; }
        }
        check(named, "peck.below_min", "a peck under 0.1 mm must fail on peck");
    }
    {
        qp::Program program = demoProgram();
        program.operations.push_back(drillOp(6.0, 10.0, 3.0));
        check(qp::validate(program).empty(), "peck.legal", "a 3 mm peck in a 10 mm hole is legal");
    }
}

// A drill has one feed: the plunge equals the working feed in the document the
// console builds and after a .qprog round-trip. The cycle F is the same feed.
void checkDrillFeed() {
    qp::Program program = demoProgram();
    program.operations.push_back(drillOp(6.0, 10.0, 0.0));
    const qp::Operation& op = program.operations.front();
    check(near(op.cutting.plungeFeedMmMin, op.cutting.feedMmMin), "drill.feed_single",
          "the plunge feed of a drill must be its working feed");

    const qp::ParseOutcome back = qp::parseProgram(qp::serialize(program));
    check(back.ok, "drill.feed_round_trip", back.ok ? "" : back.error);
    if (!back.ok) { return; }
    const qp::Operation& read = back.program.operations.front();
    check(near(read.cutting.plungeFeedMmMin, read.cutting.feedMmMin), "drill.feed_read",
          "a .qprog drill must read back with one feed");
    const std::string gcode = qp::emitProgram(program, qp::planPath(program));
    char feed[32];
    std::snprintf(feed, sizeof(feed), " F%g", op.cutting.feedMmMin);
    check(contains(gcode, feed), "drill.cycle_feed",
          "the cycle F must be the working feed of the drill, got '" + std::string(feed) + "'");
}



} // namespace summary_checks

// Runs the rule cases: the facing lead, the bounds, the peck limit and the feed.
void runSummaryRulesChecks() {
    summary_checks::checkFacingLead();
    summary_checks::checkBoundsRule();
    summary_checks::checkPeckLimit();
    summary_checks::checkDrillFeed();
}
