// core/summary.cpp - the operator summary of the console (PLAN-QUICKPROG, 7.2, Q0).
//
// The summary is what the operator reads before a run: the planned numbers of
// one operation, the regime line with the source of its numbers, and the total
// of the program. The lines live in the core, not in main.cpp, so the wording
// and the arithmetic are part of the checked contract and the console stays a
// thin shell (the same reason the regime and total lines are here).
#include <cmath>
#include <cstdio>
#include <string>
#include <variant>

#include "core/path.h"
#include "core/path_drill.h"
#include "core/path_pocket.h"
#include "core/path_circle_pocket.h"
#include "core/summary.h"

namespace qp {
namespace {

// The name of an operation for the summary: the id, or the kind when the id is
// empty (a document written by hand).
std::string opName(const Operation& op, std::size_t index) {
    if (!op.id.empty()) { return op.id; }
    return "операция " + std::to_string(index + 1);
}

// Cutting length of one span of the move list: the chords of its cuts, without
// the first move of the program (there is no point before it).
double cutLengthOf(const Path& planned, std::size_t first, std::size_t last) {
    double length = 0.0;
    for (std::size_t i = first; i < last; ++i) {
        const Move& move = planned.moves[i];
        if (move.kind != MoveKind::Cut || i == 0) { continue; }
        const Point3& from = planned.moves[i - 1].to;
        const double dx = move.to.x - from.x;
        const double dy = move.to.y - from.y;
        const double dz = move.to.z - from.z;
        length += std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    return length;
}

} // namespace

std::string operationSummary(const Program& program, const Path& planned, std::size_t index,
                             std::size_t first, std::size_t last) {
    std::string out;
    char buf[256];
    const Operation& op = program.operations[index];
    const std::size_t moves = last - first;
    const double cutLength = cutLengthOf(planned, first, last);
    std::snprintf(buf, sizeof(buf), "Операция %zu/%zu: %s, T%d ф%.1f мм\n", index + 1,
                  program.operations.size(), opName(op, index).c_str(), op.tool.number,
                  op.tool.diameterMm);
    out += buf;
    if (std::holds_alternative<FaceOp>(op.geometry)) {
        const FaceStats stats = faceStats(program, op);
        std::snprintf(buf, sizeof(buf),
                      "  полос: %d, проходов по Z: %d, ходов: %zu, длина резания: %.1f мм\n",
                      stats.strips, stats.passes, moves, cutLength);
        out += buf;
    } else if (std::holds_alternative<RectPocketOp>(op.geometry)) {
        const PocketStats stats = pocketStats(program, op);
        const Rect& rect = std::get<RectPocketOp>(op.geometry).rect;
        std::snprintf(buf, sizeof(buf),
                      "  карман %.1fx%.1f, отступ %.2f мм: рядов %d, шаг %.3f мм, "
                      "проходов по Z: %d\n",
                      rect.w, rect.h, stats.insetMm, stats.rows, stats.stepMm, stats.passes);
        out += buf;
        std::snprintf(buf, sizeof(buf), "  ходов: %zu, длина резания: %.1f мм\n", moves,
                      cutLength);
        out += buf;
    } else if (std::holds_alternative<CirclePocketOp>(op.geometry)) {
        const CircleStats stats = circleStats(program, op);
        std::snprintf(buf, sizeof(buf),
                      "  круглый карман ф%.1f, отступ %.2f мм: витков %d, шаг %.3f мм, "
                      "виток до R%.3f, проходов по Z: %d\n",
                      2.0 * std::get<CirclePocketOp>(op.geometry).circle.r, stats.insetMm,
                      stats.turns, stats.stepMm, stats.outerRadiusMm, stats.passes);
        out += buf;
        for (const double z : stats.stepDepthsMm) {
            std::snprintf(buf, sizeof(buf), "    проход Z %.2f\n", z);
            out += buf;
        }
        std::snprintf(buf, sizeof(buf), "  ходов: %zu, длина резания %.1f мм\n", moves,
                      stats.lengthMm);
        out += buf;
    } else if (isDrill(op.geometry)) {
        const DrillStats stats = drillStats(program, op);
        std::snprintf(buf, sizeof(buf), "  отверстий: %zu, %s", stats.points.size(),
                      stats.cycle == DrillKind::Peck ? "G83" : "G81");
        out += buf;
        if (stats.cycle == DrillKind::Peck) {
            std::snprintf(buf, sizeof(buf), " Q%.2f", stats.peckMm);
            out += buf;
        }
        std::snprintf(buf, sizeof(buf), ", R %.2f, дно %.2f\n", stats.rMm, stats.depthMm);
        out += buf;
        // The cutting length of a cycle is not the move list: the drill goes
        // down the hole and back, once per hole, and that length is not a
        // `cut` move of the plan (the cycle expands on the control).
        std::snprintf(buf, sizeof(buf), "  ходов: %zu (быстрые между отверстиями)\n", moves);
        out += buf;
    }
    // The regime line of every kind: the numbers of the operation and the
    // source they came from, so the operator can tell a table row from a typed
    // number before a run.
    const Feeds table = isDrill(op.geometry)
                            ? drillFeeds(op)
                            : feedsFor(Material::Aluminium, op.tool.diameterMm, ToolKind::Flat);
    out += regimeSummary(op, table.source) + "\n";
    return out;
}

} // namespace qp
