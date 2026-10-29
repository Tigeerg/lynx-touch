// tests/checks_pocket_rules.cpp - the pocket rules and the .ngc block.
//
// A mill too wide for the pocket is refused by the check and by the planner;
// the corner radius is not cut (the warning names the tool radius and the
// asked one) but does not block the plan; the emitted block carries the tool
// change and the raster feeds.
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "core/emit.h"
#include "core/feeds.h"
#include "core/ops.h"
#include "core/path.h"
#include "core/path_pocket.h"
#include "core/serialize.h"
#include "core/validate.h"

#include "tests/checks.h"
#include "tests/checks_pocket_ops.h"
#include "tests/harness.h"

namespace pocket_checks {

using qpcheck::check;
using qpcheck::contains;
using qpcheck::count;
using qpcheck::demoProgram;
using qpcheck::near;

// A 40 mm mill in a 40x40 pocket: the tool body alone fills the whole short
// side, so there is no inner rectangle to cut. The check names the geometry
// field and explains the numbers; the planner plans nothing.
void checkPocketTooBigTool() {
    qp::Program program = pocketProgram();
    qp::Operation& op = program.operations.front();
    op.tool.diameterMm = 40.0;
    // Feeds of a 40 mm mill from the table: the diameter is legal for the
    // machine, so the only failure left is the pocket geometry.
    const qp::Feeds table = qp::feedsFor(qp::Material::Aluminium, 40.0, qp::ToolKind::Flat);
    op.cutting.spindleRpm = table.rpm;
    op.cutting.feedMmMin = table.feedMmMin;
    op.cutting.plungeFeedMmMin = table.plungeFeedMmMin;
    // The reference pocket keeps its 0.2 mm allowance: 40 + 0.4 > 40, so the
    // fit fails on the diameter alone and the message has to name it.
    std::get<qp::RectPocketOp>(op.geometry).rect = qp::Rect{0.0, 0.0, 40.0, 40.0};
    const std::vector<qp::Check> problems = qp::validate(program);
    bool named = false;
    std::string reason;
    for (const qp::Check& c : problems) {
        if (c.field == "geometry" && c.reason.find("не влезают") != std::string::npos) {
            named = true;
            reason = c.reason;
        }
    }
    check(named, "pocket.tool_too_big",
          "a 40 mm mill in a 40x40 pocket must be refused on geometry");
    check(reason.find("40") != std::string::npos && reason.find("20") != std::string::npos,
          "pocket.tool_too_big_text",
          "the reason must name the diameter and the limit, got '" + reason + "'");

    // The planner refuses to invent a path as well, so a caller that skipped
    // the check gets no moves instead of a cut through the wall. The layout
    // reports it itself: `fits` false and no rows.
    const qp::PocketStats stats = qp::pocketStats(program, program.operations.front());
    check(!stats.fits && stats.rows == 0, "pocket.tool_too_big_no_moves",
          "the planner must not plan a pocket the tool does not fit");
}

// The rounded corner is not cut in this slice: the planner says so in Russian -
// the tool radius stays in the corner and the asked radius is named - and still
// plans the sharp raster.
void checkPocketCornerWarning() {
    qp::Program program = pocketProgram();
    std::get<qp::RectPocketOp>(program.operations.front().geometry).cornerRadiusMm = 5.0;
    const std::vector<std::string> warnings = qp::pocketWarnings(program, program.operations.front());
    bool warned = false;
    for (const std::string& text : warnings) {
        if (text.find("скругление угла не выполняется") != std::string::npos &&
            text.find("радиус фрезы 4.0 мм") != std::string::npos &&
            text.find("задан радиус 5.0 мм") != std::string::npos) {
            warned = true;
        }
    }
    check(warned, "pocket.corner_warning",
          "a corner radius above zero must warn with the tool radius and the asked radius");
    const std::vector<qp::Check> problems = qp::validate(program);
    check(problems.empty(), "pocket.corner_not_blocking",
          "a rounded corner must not block planning in this slice");

    // No radius asked for, but the tool is still round: the same rule without
    // the asked radius.
    qp::Program sharp = pocketProgram();
    const std::vector<std::string> sharpWarnings =
        qp::pocketWarnings(sharp, sharp.operations.front());
    bool toolRadius = false;
    for (const std::string& text : sharpWarnings) {
        if (text.find("радиус фрезы 4.0 мм") != std::string::npos &&
            text.find("задан радиус") == std::string::npos) {
            toolRadius = true;
        }
    }
    check(toolRadius, "pocket.no_corner_radius_warning",
          "a sharp corner with a round tool must name the tool radius only");
}

// The document of three operations: isometric round-trip. The pocket is in the
// middle, so an operation list is not a single-op special case: the file has to
// come back with the same fields, the same order and the same kind of each
// operation, and the second serialization has to be byte-identical.

// The .ngc of the pocket: the tool change, the spindle, the coolant and the
// moves are in place, and the raster is written as G1 lines.
void checkPocketEmit() {
    const qp::Program program = pocketProgram();
    const qp::Path planned = qp::planPath(program);
    const std::string gcode = qp::emitProgram(program, planned);
    check(contains(gcode, " T3 M6\n"), "pocket.emit_tool", "the pocket must call T3 M6");
    check(contains(gcode, " X-25.800"), "pocket.emit_first_row",
          "the first row must start at X-25.800, the inner edge");
    check(contains(gcode, " Y-15.800"), "pocket.emit_first_row_y",
          "the first row must sit at Y-15.800 next to the wall");
    check(contains(gcode, "Z-6.000"), "pocket.emit_bottom", "the bottom block Z-6.000 must be written");
    // The link cuts keep the working feed: with the plunge feed unchanged the
    // printer writes F only where the feed changes.
    check(contains(gcode, "F215"), "pocket.emit_plunge_feed", "the plunge feed must be written");
    check(contains(gcode, "F859"), "pocket.emit_work_feed", "the working feed must be written");
}

} // namespace pocket_checks

// Runs the rule cases: the tool fit, the corner warning and the emit.
void runPocketRulesChecks() {
    pocket_checks::checkPocketTooBigTool();
    pocket_checks::checkPocketCornerWarning();
    pocket_checks::checkPocketEmit();
}
