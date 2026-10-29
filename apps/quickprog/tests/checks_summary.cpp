// tests/checks_summary.cpp - the operator summary and the technologist's P1/P2
// fixes: the regime line with its source, the corner-radius warning that names
// both radii, the facing lead, the coolant switch, the peck limit and the
// drilling feed.
//
// The summary is a text line of the console that the operator reads before a
// run, so it is checked as text: the exact phrase of `--help` and of a warning
// is part of the contract, not a private wording.
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

#include "tests/harness.h"

namespace summary_checks {

using qpcheck::check;
using qpcheck::contains;
using qpcheck::count;
using qpcheck::demoProgram;
using qpcheck::near;

std::string text(double value) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4g", value);
    return std::string(buf);
}

// The regime line of a mill: every number of the operation and its table
// source, one line, so the operator sees where the numbers came from.
void checkRegimeLine() {
    const qp::Program program = qpcheck::faceProgram();
    const qp::Operation& op = program.operations.front();
    const qp::Feeds table = qp::feedsFor(qp::Material::Aluminium, op.tool.diameterMm, qp::ToolKind::Flat);
    const std::string line = qp::regimeSummary(op, table.source);
    check(contains(line, "режимы: 4775 об/мин"), "summary.regime_rpm",
          "the regime line must carry the spindle speed, got '" + line + "'");
    check(contains(line, "подача 860 мм/мин"), "summary.regime_feed",
          "the regime line must carry the working feed, got '" + line + "'");
    check(contains(line, "врезание 215 мм/мин"), "summary.regime_plunge",
          "the regime line must carry the plunge feed, got '" + line + "'");
    check(contains(line, "таблица: алюминий, ф10"), "summary.regime_source",
          "the regime line must name the source of the numbers, got '" + line + "'");

    // The drill has its own source line: a rule of thumb, not a table row.
    qp::Operation drill;
    drill.tool.number = 5;
    drill.tool.diameterMm = 6.0;
    drill.topZ = 0.0;
    drill.depthMm = 10.0;
    const qp::Feeds drillTable = qp::feedsFor(qp::Material::Aluminium, 6.0, qp::ToolKind::Drill);
    drill.cutting.spindleRpm = drillTable.rpm;
    drill.cutting.feedMmMin = drillTable.feedMmMin;
    drill.cutting.plungeFeedMmMin = drillTable.feedMmMin;
    qp::DrillHoleOp hole;
    hole.hole = qp::Circle{0.0, 0.0, 3.0};
    drill.geometry = hole;
    const std::string drillLine = qp::regimeSummary(drill, drillTable.source);
    check(contains(drillLine, "сверло") && contains(drillLine, "об/мин"),
          "summary.regime_drill", "a drill must print its own source line, got '" + drillLine + "'");
}

// The operation summary of the console: the title, the shape numbers and the
// regime line, one text the operator reads before a run.
void checkOperationSummary() {
    const qp::Program program = qpcheck::faceProgram();
    const qp::Path planned = qp::planPath(program);
    const std::string text =
        qp::operationSummary(program, planned, 0, 0, planned.moves.size());
    check(contains(text, "Операция 1/1: Плоскость, T2 ф10.0 мм"), "summary.op_title",
          "the summary must name the operation and its tool, got '" + text + "'");
    check(contains(text, "полос: 11"), "summary.op_shape",
          "the face summary must carry the strip count");
    check(contains(text, "режимы: 4775 об/мин"), "summary.op_regime",
          "the operation summary must end with the regime line");
    check(!text.empty() && text.back() == '\n', "summary.op_newline",
          "the summary of an operation must end with a newline");
}

// The total line: operations, moves, cutting length and the time in minutes and
// seconds.
void checkTotalLine() {
    const std::string line = qp::totalSummary(4, 213, 4210.5, 760.0);
    check(contains(line, "Итого: операций 4"), "summary.total_ops",
          "the total must count the operations, got '" + line + "'");
    check(contains(line, "ходов 213"), "summary.total_moves", "the total must count the moves");
    check(contains(line, "длина резания 4210.5 мм"), "summary.total_length",
          "the total must print the cutting length");
    check(contains(line, "12 мин 40 с"), "summary.total_time",
          "760 s must be 12 min 40 s, got '" + line + "'");
}

// The corner radius is not cut: the warning names the radius the tool leaves
// and the radius the operator asked for.
void checkCornerWarning() {
    qp::Program program = demoProgram();
    qp::Operation op;
    op.id = "Карман";
    op.tool.number = 3;
    op.tool.diameterMm = 8.0;
    op.cutting.spindleRpm = 5968.0;
    op.cutting.feedMmMin = 859.0;
    op.cutting.plungeFeedMmMin = 215.0;
    op.topZ = 0.0;
    op.depthMm = 6.0;
    op.stepDownMm = 3.0;
    qp::RectPocketOp pocket;
    pocket.rect = qp::Rect{-30.0, -20.0, 60.0, 40.0};
    pocket.cornerRadiusMm = 3.0;
    op.geometry = pocket;
    program.operations.push_back(op);
    const std::vector<std::string> warnings = qp::pocketWarnings(program, program.operations.front());
    bool named = false;
    std::string textOfWarning;
    for (const std::string& one : warnings) {
        if (contains(one, "скругление")) {
            named = true;
            textOfWarning = one;
        }
    }
    check(named, "corner.warning_text", "a corner radius must warn with the two radii");
    check(contains(textOfWarning, "радиус фрезы 4.0 мм") &&
              contains(textOfWarning, "задан радиус 3.0 мм"),
          "corner.both_radii",
          "the warning must name the tool radius and the asked radius, got '" + textOfWarning + "'");
    check(!contains(textOfWarning, "острыми"), "corner.no_sharp_words",
          "the corners are not sharp: the tool radius stays, got '" + textOfWarning + "'");
}

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

void runSummaryChecks() {
    summary_checks::checkRegimeLine();
    summary_checks::checkOperationSummary();
    summary_checks::checkTotalLine();
    summary_checks::checkCornerWarning();
    summary_checks::checkFacingLead();
    summary_checks::checkBoundsRule();
    summary_checks::checkPeckLimit();
    summary_checks::checkDrillFeed();
}
