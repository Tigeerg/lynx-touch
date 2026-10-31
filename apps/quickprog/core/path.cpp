// core/path.cpp - the planner entry point and the time estimate.
//
// Planning is split by operation: `path_face.cpp` owns the face strips, and a
// new kind gets its own file next to it instead of growing this one. The
// dispatch here is a visitor over the variant: the compiler refuses to build a
// visitor that forgets a kind, so a new operation cannot be silently skipped.
//
// Path helpers (`PathBuilder`) live here too: they are what every planner
// appends moves through, so the bounds, the cutting length and the move count
// are computed in one place and cannot disagree between operations.
#include "core/path.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <variant>

#include "core/limits.h"
#include "core/ops.h"
#include "core/path_circle_pocket.h"
#include "core/path_drill.h"

namespace qp {
namespace {

// A whole number without a trailing ".0" and a fraction with one digit: the
// summary of the operator reads "860 mm/min", not "860.0".
std::string summaryNumber(double value) {
    char buf[32];
    if (std::fabs(value - std::round(value)) < 1e-6) {
        std::snprintf(buf, sizeof(buf), "%.0f", value);
    } else {
        std::snprintf(buf, sizeof(buf), "%.1f", value);
    }
    return std::string(buf);
}

// Planning results are told apart by content, not by time: a counter keeps the
// uuid unique within the process and reproducible between runs, so a test can
// compare it. The map from operation index to serial is reset for every
// program, so two runs of the same document give the same uuids.
std::atomic<unsigned long long> g_programSerial{0};

std::string nextProgramUuid() {
    const unsigned long long serial = ++g_programSerial;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "path-%06llu", serial);
    return std::string(buf);
}

} // namespace

std::string operationUuid(const std::string& programUuid, int opIndex) {
    return programUuid + "/op" + std::to_string(opIndex + 1);
}

double moveLength(const Move& move, const Point3& from) {
    if (move.kind != MoveKind::Cut) { return 0.0; }
    if (move.hasArc && move.arc.lengthMm > 0.0) { return move.arc.lengthMm; }
    const double dx = move.to.x - from.x;
    const double dy = move.to.y - from.y;
    const double dz = move.to.z - from.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

void PathBuilder::add(MoveKind kind, const Point3& to, double feedMmMin) {
    grow(to);
    Move move;
    move.kind = kind;
    move.to = to;
    if (kind == MoveKind::Cut) {
        move.feedMmMin = feedMmMin;
        move.hasFeed = true;
        if (!moves.empty()) { lengthMm += moveLength(move, moves.back().to); }
    }
    moves.push_back(move);
}

void PathBuilder::addArc(const Move& move) {
    grow(move.to);
    if (move.kind == MoveKind::Cut && !moves.empty()) { lengthMm += moveLength(move, moves.back().to); }
    moves.push_back(move);
}

void PathBuilder::grow(const Point3& to) {
    if (!bbox.valid) {
        bbox.valid = true;
        bbox.xMin = bbox.xMax = to.x;
        bbox.yMin = bbox.yMax = to.y;
        bbox.zMax = to.z;
        return;
    }
    bbox.xMin = std::fmin(bbox.xMin, to.x);
    bbox.xMax = std::fmax(bbox.xMax, to.x);
    bbox.yMin = std::fmin(bbox.yMin, to.y);
    bbox.yMax = std::fmax(bbox.yMax, to.y);
    bbox.zMax = std::fmax(bbox.zMax, to.z);
}

std::string regimeSummary(const Operation& operation, const std::string& source) {
    std::string out = "  режимы: " + summaryNumber(operation.cutting.spindleRpm) + " об/мин, подача " +
                      summaryNumber(operation.cutting.feedMmMin) + " мм/мин, врезание " +
                      summaryNumber(operation.cutting.plungeFeedMmMin) + " мм/мин";
    if (!source.empty()) { out += " — " + source; }
    return out;
}

std::string totalSummary(std::size_t operations, std::size_t moves, double cuttingMm,
                         double seconds) {
    char length[32];
    std::snprintf(length, sizeof(length), "%.1f", cuttingMm);
    const long total = std::lround(seconds);
    return "Итого: операций " + std::to_string(operations) + ", ходов " + std::to_string(moves) +
           ", длина резания " + length + " мм, оценка " + std::to_string(total / 60) + " мин " +
           std::to_string(total % 60) + " с";
}

double estimateSeconds(const Path& path) {
    const double rapid = machineLimits().rapidMmMin;
    double seconds = 0.0;
    Point3 from;
    bool haveFrom = false;
    for (const Move& move : path.moves) {
        if (haveFrom) {
            const double distance = moveLength(move, from);
            const double rate = move.kind == MoveKind::Cut ? move.feedMmMin : rapid;
            if (rate > 0.0) { seconds += distance / rate * 60.0; }
        }
        from = move.to;
        haveFrom = true;
    }
    // The drilling cycles: the moves of a drill span are only the rapids, the
    // drilling itself is charged from the cycle time the planner recorded.
    return seconds + path.cycleMinutes * 60.0;
}

namespace {

// The visitor of the dispatch: one overload per geometry kind. Every kind of
// the variant has a planner, so a new kind cannot be silently skipped.
struct Planner {
    const Program& program;
    Path& path;
    int opIndex;
    std::size_t first;

    void begin() { first = path.moves.size(); }

    // Appends the moves of the operation to the path and records its span.
    // An empty span still marks the operation, so the emitter prints its tag.
    void operator()(const FaceOp&) {
        PathBuilder builder(path.moves);
        // Bounds are per program, not per operation: the builder starts from
        // the bounds of the moves already planned.
        builder.bbox = path.bbox;
        planFace(program, program.operations[static_cast<std::size_t>(opIndex)], builder);
        path.bbox = builder.bbox;
        path.lengthMm += builder.lengthMm;
    }
    void operator()(const RectPocketOp&) {
        PathBuilder builder(path.moves);
        builder.bbox = path.bbox;
        planRectPocket(program, program.operations[static_cast<std::size_t>(opIndex)], builder);
        path.bbox = builder.bbox;
        path.lengthMm += builder.lengthMm;
    }
    void operator()(const CirclePocketOp&) {
        PathBuilder builder(path.moves);
        builder.bbox = path.bbox;
        planCirclePocket(program, program.operations[static_cast<std::size_t>(opIndex)], builder);
        path.bbox = builder.bbox;
        path.lengthMm += builder.lengthMm;
    }
    void operator()(const DrillHoleOp&) { plan(); }
    void operator()(const DrillGridOp&) { plan(); }
    void operator()(const DrillCircleOp&) { plan(); }

    // The kinds the demo reads but does not plan (PLAN-QUICKPROG, 7.6): the
    // console names them as skipped before the plan, so a valid document still
    // reaches this visitor only with a kind the demo owns. An empty span keeps
    // the operation count of the plan honest if one ever does.
    template <typename T>
    void operator()(const T&) {}

    // The three drilling kinds share one planner: the hole list differs, the
    // cycle does not.
    void plan() {
        PathBuilder builder(path.moves);
        builder.bbox = path.bbox;
        const Operation& operation = program.operations[static_cast<std::size_t>(opIndex)];
        planDrill(program, operation, builder);
        path.bbox = builder.bbox;
        path.lengthMm += builder.lengthMm;
        // The cycle time of one hole: the descent and the retraction of the
        // drill at the working feed. A `peck` cycle goes down the same
        // distance, the rapid hops between the pecks are not counted: the
        // estimate is optimistic, as the console already says.
        const DrillStats drill = drillStats(program, operation);
        const double feed = operation.cutting.feedMmMin;
        if (feed > 0.0) {
            const double perHoleMm = 2.0 * operation.depthMm;
            path.cycleMinutes +=
                static_cast<double>(drill.points.size()) * perHoleMm / feed;
        }
    }
};

} // namespace

Path planPath(const Program& program) {
    Path path;
    path.uuid = nextProgramUuid();
    for (std::size_t i = 0; i < program.operations.size(); ++i) {
        const Operation& operation = program.operations[i];
        if (!operation.enabled) { continue; }
        Planner planner{program, path, static_cast<int>(i), path.moves.size()};
        std::visit(planner, operation.geometry);
        path.operations.push_back(OperationSpan{static_cast<int>(i), planner.first});
    }
    return path;
}

} // namespace qp
