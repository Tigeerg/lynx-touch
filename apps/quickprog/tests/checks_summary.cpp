// tests/checks_summary.cpp - the operator summary of the program.
//
// The summary is a text line of the console that the operator reads before a
// run, so it is checked as text: the regime line with its table source, the
// operation summary, the total line and the corner-radius warning that names
// both radii. The exact phrase of `--help` and of a warning is part of the
// contract, not a private wording.
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

    // The drill has its own source line: the drill table row with the material
    // of the drill, not the mill row.
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
    check(contains(drillLine, "таблица: сверло HSS") && contains(drillLine, "об/мин"),
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

} // namespace summary_checks

// Runs the summary cases: the regime line, the operation, the total and the corner.
void runSummaryChecks() {
    summary_checks::checkRegimeLine();
    summary_checks::checkOperationSummary();
    summary_checks::checkTotalLine();
    summary_checks::checkCornerWarning();
}
