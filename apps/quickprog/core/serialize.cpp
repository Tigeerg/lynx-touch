// core/serialize.cpp - the .qprog writer (PLAN-QUICKPROG, 6, 7.2).
//
// Format rules that the writer and the reader share:
//
//   * one field per line, `key value ...`, whitespace separated;
//   * the header keys come first (schema, name, units, wcs, stock, safeZ),
//     then the operation list;
//   * `op <kind> <id>` starts an operation; a free-text id is the rest of the
//     line, so it may carry spaces; the writer puts it in double quotes and the
//     reader drops one pair of outer quotes. Inside the quotes nothing is
//     escaped: a line break cannot be an id, and a quote is legal as long as
//     the id does not end with one;
//   * `op.*` keys belong to the current operation;
//   * numbers use a decimal point and no thousands separator.
//
// The writer is deterministic: the same document gives the same bytes, keys in
// a fixed order, numbers in one spelling. Defaults of the model (a zero
// allowance, a disabled operation) are written out, so the reader never has to
// guess what the writer left out. Words and the key table live in
// `serialize_shared.cpp`; `serialize_read.cpp` is the other side.
#include "core/serialize.h"

#include <string>
#include <variant>
#include <vector>

#include "core/path_drill.h"
#include "core/serialize_internal.h"

namespace qp {

std::string serialize(const Program& program) {
    std::string out;
    out += "schema " + number(program.schema) + "\n";
    out += "name " + quoteText(program.name) + "\n";
    out += "units " + program.units + "\n";
    out += "wcs " + program.wcs + "\n";
    out += "stock " + number(program.stock.min.x) + " " + number(program.stock.min.y) + " " +
           number(program.stock.min.z) + " " + number(program.stock.max.x) + " " +
           number(program.stock.max.y) + " " + number(program.stock.max.z) + "\n";
    out += "safeZ " + number(program.safeZ) + "\n";
    for (const Operation& op : program.operations) {
        out += "op " + std::string(kindName(op.geometry));
        if (!op.id.empty()) { out += " " + quoteText(op.id); }
        out += "\n";
        out += "op.enabled " + std::string(op.enabled ? "on" : "off") + "\n";
        out += "op.tool " + std::to_string(op.tool.number) + " " +
               number(op.tool.diameterMm) + "\n";
        out += "op.spindle " + number(op.cutting.spindleRpm) + "\n";
        out += "op.feed " + number(op.cutting.feedMmMin) + "\n";
        // A drill has one feed: its plunge line repeats the working feed, so a
        // hand edit of one number cannot leave a stale second one behind.
        const double plunge = isDrill(op.geometry) ? op.cutting.feedMmMin
                                                   : op.cutting.plungeFeedMmMin;
        out += "op.plunge " + number(plunge) + "\n";
        out += "op.coolant " + std::string(coolantName(op.coolant)) + "\n";
        out += "op.topZ " + number(op.topZ) + "\n";
        out += "op.depth " + number(op.depthMm) + "\n";
        out += "op.stepDown " + number(op.stepDownMm) + "\n";
        // An empty note is the absence of the line: the reader treats it as a
        // missing optional field, so the file does not carry an empty pair of
        // quotes on every operation.
        if (!op.notes.empty()) { out += "op.notes " + quoteText(op.notes) + "\n"; }
        if (const auto* face = std::get_if<FaceOp>(&op.geometry)) {
            out += "op.z " + number(face->z) + "\n";
            out += "op.stepOver " + number(face->stepOverPct) + "\n";
        } else if (const auto* pocket = std::get_if<RectPocketOp>(&op.geometry)) {
            const Rect& r = pocket->rect;
            out += "op.rect " + number(r.x) + " " + number(r.y) + " " + number(r.w) + " " +
                   number(r.h) + "\n";
            out += "op.cornerRadius " + number(pocket->cornerRadiusMm) + "\n";
            out += "op.finishAllowance " + number(pocket->finishAllowanceMm) + "\n";
        } else if (const auto* circle = std::get_if<CirclePocketOp>(&op.geometry)) {
            out += "op.circle " + number(circle->circle.x) + " " + number(circle->circle.y) + " " +
                   number(circle->circle.r) + "\n";
            out += "op.finishAllowance " + number(circle->finishAllowanceMm) + "\n";
        } else if (const auto* hole = std::get_if<DrillHoleOp>(&op.geometry)) {
            out += "op.hole " + number(hole->hole.x) + " " + number(hole->hole.y) + " " +
                   number(hole->hole.r) + "\n";
            out += "op.peck " + number(hole->params.peckMm) + "\n";
            if (hole->params.hasR) { out += "op.r " + number(hole->params.rMm) + "\n"; }
        } else if (const auto* grid = std::get_if<DrillGridOp>(&op.geometry)) {
            out += "op.grid " + number(grid->x0) + " " + number(grid->y0) + " " +
                   std::to_string(grid->cols) + " " + std::to_string(grid->rows) + " " +
                   number(grid->dx) + " " + number(grid->dy) + "\n";
            out += "op.peck " + number(grid->params.peckMm) + "\n";
            if (grid->params.hasR) { out += "op.r " + number(grid->params.rMm) + "\n"; }
        } else {
            const auto& ring = std::get<DrillCircleOp>(op.geometry);
            out += "op.circle " + number(ring.circle.x) + " " + number(ring.circle.y) + " " +
                   number(ring.circle.r) + "\n";
            out += "op.count " + std::to_string(ring.count) + "\n";
            out += "op.start " + number(ring.startDeg) + "\n";
            out += "op.arc " + number(ring.arcDeg) + "\n";
            out += "op.fullTurn " + std::string(ring.fullTurn ? "on" : "off") + "\n";
            out += "op.peck " + number(ring.params.peckMm) + "\n";
            if (ring.params.hasR) { out += "op.r " + number(ring.params.rMm) + "\n"; }
        }
    }
    return out;
}

} // namespace qp
