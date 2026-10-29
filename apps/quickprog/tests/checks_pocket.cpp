// tests/checks_pocket.cpp - the rectangular pocket planner and its numbers.
//
// The reference case of PLAN-QUICKPROG, 10: a 60x40 pocket, an 8 mm end mill,
// 0.2 mm allowance, a 2 mm raster step, 6 mm deep in 2.5 mm passes. The numbers
// below are computed by hand and fixed here, so a planner that drifts from the
// arithmetic fails on a number, not on a mood:
//
//   inset     D/2 + allowance            = 4.2 mm
//   inner     width 60 - 8.4, height 40 - 8.4 = 51.6 x 31.6 mm
//   nominal   D * 70 / 100               = 5.6 mm
//   rows      ceil(31.6 / 5.6) + 1 = ceil(5.64) + 1 = 7
//   step      31.6 / 6                   = 5.2666... mm
//   first row y = -20 + 4.2 = -15.8, last row y = 15.8: the centre of the tool
//   body is exactly the inset from the wall, so the body touches the wall minus
//   the allowance.
//
// 6 mm in 2.5 mm steps: 2.5, 5, then 6 (the last pass is exactly the bottom).
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

#include "tests/harness.h"

namespace pocket_checks {

using qpcheck::check;
using qpcheck::contains;
using qpcheck::count;
using qpcheck::demoProgram;
using qpcheck::near;

// The reference pocket of the plan: 60x40 at the origin of the stock, an 8 mm
// end mill, 0.2 mm allowance, 6 mm deep in 2.5 mm passes.
qp::Operation pocketOp() {
    qp::Operation op;
    op.id = "Карман";
    op.tool.number = 3;
    op.tool.diameterMm = 8.0;
    op.cutting.spindleRpm = 5968.0;
    op.cutting.feedMmMin = 859.0;
    op.cutting.plungeFeedMmMin = 215.0;
    op.topZ = 0.0;
    op.depthMm = 6.0;
    op.stepDownMm = 2.5;
    qp::RectPocketOp pocket;
    pocket.rect = qp::Rect{-30.0, -20.0, 60.0, 40.0};
    pocket.finishAllowanceMm = 0.2;
    op.geometry = pocket;
    return op;
}

qp::Program pocketProgram() {
    qp::Program program = demoProgram();
    program.name = "POCKET DEMO";
    program.operations.push_back(pocketOp());
    return program;
}

// The Y values of the raster rows at the deepest pass, in the order they are
// cut. A row is a long cut at a constant Z along X; the link between two rows
// is a shorter cut along Y. Only the rows of the deepest pass count.
std::vector<double> deepestRows(const qp::Path& planned) {
    bool havePlane = false;
    double minZ = 0.0;
    for (const qp::Move& m : planned.moves) {
        if (m.kind == qp::MoveKind::Cut && (!havePlane || m.to.z < minZ)) {
            minZ = m.to.z;
            havePlane = true;
        }
    }
    std::vector<double> ys;
    for (std::size_t i = 1; i < planned.moves.size(); ++i) {
        const qp::Move& m = planned.moves[i];
        const qp::Move& prev = planned.moves[i - 1];
        if (m.kind != qp::MoveKind::Cut || !near(m.to.z, minZ) || !near(prev.to.z, minZ)) { continue; }
        // A row changes X by the inner width; a link changes Y by one step.
        if (std::fabs(m.to.x - prev.to.x) < 10.0) { continue; }
        if (ys.empty() || !near(ys.back(), m.to.y)) { ys.push_back(m.to.y); }
    }
    return ys;
}

// The rows, passes and step of the reference pocket.
void checkPocketLayout() {
    const qp::Program program = pocketProgram();
    const qp::PocketStats stats = qp::pocketStats(program, program.operations.front());
    check(stats.fits, "pocket.fits", "an 8 mm mill must fit a 60x40 pocket with a 0.2 mm allowance");
    check(near(stats.insetMm, 4.2), "pocket.inset",
          "the inset must be D/2 + allowance = 4.2 mm, got " + std::to_string(stats.insetMm));
    check(stats.rows == 7, "pocket.rows",
          "31.6 mm of inner height over the 5.6 mm nominal step needs 7 rows, got " +
              std::to_string(stats.rows));
    check(near(stats.stepMm, 31.6 / 6.0), "pocket.step",
          "the real step must be 31.6 / 6 = 5.2667 mm, got " + std::to_string(stats.stepMm));
    check(stats.stepMm <= 8.0 * 0.7 + 1e-9, "pocket.step_bounded",
          "the real step must not exceed D * 70 % = 5.6 mm");
    check(near(stats.innerXMm, -25.8) && near(stats.innerYMm, -15.8), "pocket.inner_origin",
          "the inner rectangle must start at (-25.8, -15.8)");
    check(near(stats.innerWidthMm, 51.6) && near(stats.innerHeightMm, 31.6), "pocket.inner_size",
          "the inner rectangle must be 51.6 x 31.6 mm");
}

// 6 mm in 2.5 mm steps: 2.5, 5, 6. The last pass is exactly the bottom.
void checkPocketPasses() {
    const qp::Program program = pocketProgram();
    const qp::PocketStats stats = qp::pocketStats(program, program.operations.front());
    check(stats.passes == 3, "pocket.passes",
          "6 mm in 2.5 mm steps must be 3 passes, got " + std::to_string(stats.passes));
    check(stats.stepDepthsMm.size() == 3, "pocket.step_count",
          "the stats must list three pass depths");
    check(stats.stepDepthsMm.size() == 3 && near(stats.stepDepthsMm[0], -2.5) &&
              near(stats.stepDepthsMm[1], -5.0) && near(stats.stepDepthsMm[2], -6.0),
          "pocket.step_depths", "the pass depths must be -2.5, -5, -6");

    // The same numbers read from the moves: a row is cut at the pass Z.
    const qp::Path planned = qp::planPath(program);
    std::vector<double> planes;
    for (const qp::Move& m : planned.moves) {
        if (m.kind != qp::MoveKind::Cut) { continue; }
        if (planes.empty() || !near(planes.back(), m.to.z)) { planes.push_back(m.to.z); }
    }
    bool hasDeepest = false;
    for (const double z : planes) {
        if (near(z, -6.0)) { hasDeepest = true; }
    }
    check(hasDeepest, "pocket.moves_bottom", "the path must cut at the bottom -6");
}

// The rows of the deepest pass: the first and the last sit on the inner
// boundary, the neighbours are closer than the tool diameter, so no band is
// left uncut.
void checkPocketRows() {
    const qp::Program program = pocketProgram();
    const qp::Path planned = qp::planPath(program);
    const std::vector<double> ys = deepestRows(planned);
    check(static_cast<int>(ys.size()) == 7, "pocket.last_pass_rows",
          "the deepest pass must sweep 7 rows, got " + std::to_string(ys.size()));
    check(!ys.empty() && near(ys.front(), -15.8), "pocket.first_row_wall",
          "the first row must sit 4.2 mm inside the low wall Y=-20, got " +
              (ys.empty() ? std::string("no row") : std::to_string(ys.front())));
    check(!ys.empty() && near(ys.back(), 15.8), "pocket.last_row_wall",
          "the last row must sit 4.2 mm inside the high wall Y=+20, got " +
              (ys.empty() ? std::string("no row") : std::to_string(ys.back())));

    const double diameter = program.operations.front().tool.diameterMm;
    int wideGap = 0;
    for (std::size_t i = 0; i + 1 < ys.size(); ++i) {
        if (ys[i + 1] - ys[i] > diameter + 1e-9) { ++wideGap; }
    }
    check(wideGap == 0, "pocket.no_wide_strip",
          "a band between two rows is wider than the tool: " + std::to_string(wideGap) + " gap(s)");

    // The rows alternate direction: the raster is a zigzag, not a one-way pass
    // with a return leg outside the material.
    bool alternates = true;
    double previousEndX = std::nan("");
    for (std::size_t i = 1; i < planned.moves.size(); ++i) {
        const qp::Move& m = planned.moves[i];
        const qp::Move& prev = planned.moves[i - 1];
        if (m.kind != qp::MoveKind::Cut || !near(m.to.z, -6.0) || !near(prev.to.z, -6.0)) { continue; }
        if (std::fabs(m.to.x - prev.to.x) < 10.0) { continue; }
        if (!std::isnan(previousEndX) && near(m.to.x, previousEndX)) { alternates = false; }
        previousEndX = m.to.x;
    }
    check(alternates, "pocket.zigzag", "two neighbouring rows must run in opposite directions");
}

// Every cut stays inside the pocket shrunk by the inset: the tool body never
// crosses the wall minus the allowance. No rapid goes below the material top.
void checkPocketBounds() {
    const qp::Program program = pocketProgram();
    const qp::Path planned = qp::planPath(program);
    const qp::PocketStats stats = qp::pocketStats(program, program.operations.front());
    const qp::Rect& rect = std::get<qp::RectPocketOp>(program.operations.front().geometry).rect;
    const double xMin = rect.x + stats.insetMm - 1e-6;
    const double xMax = rect.x + rect.w - stats.insetMm + 1e-6;
    const double yMin = rect.y + stats.insetMm - 1e-6;
    const double yMax = rect.y + rect.h - stats.insetMm + 1e-6;
    bool inside = true;
    bool rapidSafe = true;
    double deepestZ = 0.0;
    for (const qp::Move& m : planned.moves) {
        if (m.kind == qp::MoveKind::Cut) {
            if (m.to.x < xMin || m.to.x > xMax || m.to.y < yMin || m.to.y > yMax) { inside = false; }
            deepestZ = std::fmin(deepestZ, m.to.z);
        } else if (m.to.z < program.operations.front().topZ - 1e-6) {
            rapidSafe = false;
        }
    }
    check(inside, "pocket.cuts_inside",
          "a cutting move left the pocket shrunk by D/2 + allowance");
    check(rapidSafe, "pocket.rapid_above_top", "a rapid went below the material top");
    check(near(deepestZ, -6.0), "pocket.deepest_cut", "the deepest cut must be the bottom -6");
}

// The entry into the material is a vertical cut at the plunge feed on the edge
// of the pocket; the exit is a cut up. No rapid meets the material: a rapid
// ends at the top, the descent below it is a cut.
void checkPocketEntry() {
    const qp::Program program = pocketProgram();
    const qp::Path planned = qp::planPath(program);
    const qp::Operation& op = program.operations.front();
    int plungedPasses = 0;
    bool plungeFeed = true;
    bool entryInZone = true;
    const qp::PocketStats stats = qp::pocketStats(program, op);
    for (std::size_t i = 1; i < planned.moves.size(); ++i) {
        const qp::Move& m = planned.moves[i];
        const qp::Move& prev = planned.moves[i - 1];
        if (m.kind != qp::MoveKind::Cut) { continue; }
        // A plunge: a vertical cut downwards from the material top.
        if (near(m.to.x, prev.to.x) && near(m.to.y, prev.to.y) && m.to.z < prev.to.z &&
            near(prev.to.z, op.topZ)) {
            ++plungedPasses;
            if (!near(m.feedMmMin, op.cutting.plungeFeedMmMin)) { plungeFeed = false; }
            if (m.to.x < -30.0 + stats.insetMm - 1e-6 || m.to.x > 30.0 - stats.insetMm + 1e-6 ||
                m.to.y < -20.0 + stats.insetMm - 1e-6 || m.to.y > 20.0 - stats.insetMm + 1e-6) {
                entryInZone = false;
            }
        }
        // A rapid that lands below the top is a rapid into the material.
        if (m.kind == qp::MoveKind::Rapid && m.to.z < op.topZ - 1e-6) { plungeFeed = false; }
    }
    check(plungedPasses == stats.passes, "pocket.one_plunge_per_pass",
          "every Z pass must start with one vertical plunge, got " + std::to_string(plungedPasses) +
              " of " + std::to_string(stats.passes));
    check(plungeFeed, "pocket.plunge_feed",
          "the plunge must use the plunge feed and no rapid may enter the material");
    check(entryInZone, "pocket.plunge_in_zone",
          "the plunge must stand on the edge of the pocket inside the cutting zone");

    // Between two passes the tool goes up to safe Z with a rapid: after the
    // exit cut it stands at the top, then a rapid takes it to safe Z.
    bool safeBetweenPasses = false;
    for (std::size_t i = 1; i < planned.moves.size(); ++i) {
        const qp::Move& m = planned.moves[i];
        if (m.kind == qp::MoveKind::Rapid && near(m.to.z, program.safeZ)) { safeBetweenPasses = true; }
    }
    check(safeBetweenPasses, "pocket.rapid_to_safe", "the tool must go to safe Z between the passes");
}

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
void checkPocketRoundTrip() {
    qp::Program program = pocketProgram();
    program.name = "Документ с карманом";
    qp::Operation face = program.operations.front();
    face.id = "Плоскость";
    face.notes = "верхняя операция";
    program.operations.insert(program.operations.begin(), face);
    qp::Operation second = pocketOp();
    second.id = "Второй карман";
    second.tool.number = 4;
    second.tool.diameterMm = 6.0;
    std::get<qp::RectPocketOp>(second.geometry).rect = qp::Rect{-10.0, -5.0, 20.0, 10.0};
    program.operations.push_back(second);

    const std::string text = qp::serialize(program);
    const qp::ParseOutcome parsed = qp::parseProgram(text);
    check(parsed.ok, "qprog.round_trip_ok", parsed.ok ? "" : parsed.error);
    if (!parsed.ok) { return; }
    const qp::Program& back = parsed.program;
    check(back.name == program.name && back.wcs == program.wcs && back.schema == program.schema,
          "qprog.round_trip_header", "the header fields must come back unchanged");
    check(back.operations.size() == program.operations.size(), "qprog.round_trip_count",
          "the operation count must come back unchanged");
    bool same = back.operations.size() == program.operations.size();
    for (std::size_t i = 0; same && i < program.operations.size(); ++i) {
        const qp::Operation& a = program.operations[i];
        const qp::Operation& b = back.operations[i];
        same = a.id == b.id && a.tool.number == b.tool.number &&
               near(a.tool.diameterMm, b.tool.diameterMm) && near(a.depthMm, b.depthMm) &&
               near(a.stepDownMm, b.stepDownMm) && a.geometry.index() == b.geometry.index();
        if (same && std::holds_alternative<qp::RectPocketOp>(a.geometry)) {
            const qp::RectPocketOp& pa = std::get<qp::RectPocketOp>(a.geometry);
            const qp::RectPocketOp& pb = std::get<qp::RectPocketOp>(b.geometry);
            same = near(pa.rect.x, pb.rect.x) && near(pa.rect.y, pb.rect.y) &&
                   near(pa.rect.w, pb.rect.w) && near(pa.rect.h, pb.rect.h) &&
                   near(pa.cornerRadiusMm, pb.cornerRadiusMm) &&
                   near(pa.finishAllowanceMm, pb.finishAllowanceMm);
        }
    }
    check(same, "qprog.round_trip_fields", "every operation field must come back unchanged");
    check(qp::serialize(back) == text, "qprog.round_trip_stable",
          "a second serialization of the parsed document must be byte-identical");

    // Determinism: the same document twice gives the same bytes.
    check(qp::serialize(program) == text, "qprog.deterministic",
          "two serializations of one document must be equal");
}

// An unknown key is an error with the line number; a missing key is an error
// with the field name. The reader never applies a default of its own.
void checkQprogErrors() {
    qp::Program program = pocketProgram();
    const std::string good = qp::serialize(program);

    std::string unknown = good;
    unknown += "op.wobble 3\n";
    const qp::ParseOutcome badUnknown = qp::parseProgram(unknown);
    check(!badUnknown.ok && badUnknown.error.find("строка") != std::string::npos &&
              badUnknown.error.find("op.wobble") != std::string::npos,
          "qprog.unknown_field", "an unknown field must name the line and the key, got '" +
                                      badUnknown.error + "'");

    std::string missing;
    for (const std::string& line : [&]() {
             std::vector<std::string> out;
             std::string rest = good;
             while (!rest.empty()) {
                 const std::size_t end = rest.find('\n');
                 out.push_back(rest.substr(0, end == std::string::npos ? rest.size() : end));
                 if (end == std::string::npos) { break; }
                 rest = rest.substr(end + 1);
             }
             return out;
         }()) {
        if (line.compare(0, 7, "op.topZ") == 0) { continue; }
        missing += line + "\n";
    }
    const qp::ParseOutcome badMissing = qp::parseProgram(missing);
    check(!badMissing.ok && badMissing.error.find("op.topZ") != std::string::npos,
          "qprog.missing_field", "a missing field must name it, got '" + badMissing.error + "'");

    std::string broken = good;
    const std::size_t at = broken.find("safeZ ");
    if (at != std::string::npos) { broken.replace(at, 6, "safeZ abc"); }
    const qp::ParseOutcome badNumber = qp::parseProgram(broken);
    check(!badNumber.ok && badNumber.error.find("safeZ") != std::string::npos,
          "qprog.bad_number", "a broken number must name the field");

    std::string schema = good;
    schema.replace(0, 8, "schema 2");
    const qp::ParseOutcome badSchema = qp::parseProgram(schema);
    check(!badSchema.ok, "qprog.bad_schema", "an unsupported schema must be an error");
}

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

// Runs the cases of this file: the entry point calls the four runners in the
// order of the slices.
void runPocketChecks() {
    pocket_checks::checkPocketLayout();
    pocket_checks::checkPocketPasses();
    pocket_checks::checkPocketRows();
    pocket_checks::checkPocketBounds();
    pocket_checks::checkPocketEntry();
    pocket_checks::checkPocketTooBigTool();
    pocket_checks::checkPocketCornerWarning();
    pocket_checks::checkPocketEmit();
    pocket_checks::checkPocketRoundTrip();
    pocket_checks::checkQprogErrors();
}
