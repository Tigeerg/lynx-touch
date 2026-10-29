// core/path.h - the planner result: moves, not G-code lines (PLAN-QUICKPROG, 6).
//
// The preview and the emitter read the same Path: one geometry, two
// representations. The planner returns moves in machine coordinates; the
// emitter turns them into G-code and never recomputes a coordinate.
//
// Operations are delimited in the move list instead of being returned in a
// separate vector: an operation block starts at OperationSpan::first and runs
// up to the next span, so the emitter walks one list and prints UI_OP at the
// right place.
#pragma once

#include <string>
#include <vector>

#include "core/geom.h"
#include "core/ops.h"

namespace qp {

// Arc centre offset from the start point of the arc, mm, in the XY plane, as
// the I/J words of G2/G3. The end point is Move::to. `lengthMm` is the arc
// length of the move: the planner knows how far the tool travels along an arc,
// and recovering it from I/J and the end point needs the sign of a rounded
// sine near a half turn (the sine of pi is 1e-16, not zero). The planner sets
// the length on the move it appends, so the report and the time estimate do not
// re-derive it.
struct ArcOffset {
    bool cw = false;
    double i = 0.0;
    double j = 0.0;
    double lengthMm = 0.0;
};

enum class MoveKind { Rapid, Cut };

// One move. `feedMmMin` is set for cuts and empty for rapids: a rapid has no
// programmed feed, and writing F on it would change the modal state by accident.
struct Move {
    MoveKind kind = MoveKind::Rapid;
    Point3 to;
    double feedMmMin = 0.0;
    bool hasFeed = false;
    bool hasArc = false;
    ArcOffset arc;
};

// Bounds of the tool centre path in XY and its highest Z, mm. `valid` is false
// for an empty path: zeros mean "no path", not "a point at the origin".
struct PathBounds {
    bool valid = false;
    double xMin = 0.0;
    double xMax = 0.0;
    double yMin = 0.0;
    double yMax = 0.0;
    double zMax = 0.0;
};

// Which moves belong to which operation: `first` is the index of the first
// move of operation `opIndex` (an index into Program::operations).
struct OperationSpan {
    int opIndex = 0;
    std::size_t first = 0;
};

// Planned trajectory of a program. `uuid` identifies the planning result so a
// stale preview can be told from the fresh one after a debounce.
struct Path {
    std::vector<Move> moves;
    std::vector<OperationSpan> operations;
    PathBounds bbox;
    // Cutting length, mm: the sum of the `cut` moves only. Rapids are not
    // cutting and are not counted, so the number is comparable between two
    // strategies of the same operation.
    double lengthMm = 0.0;
    // Feed time of the drilling cycles of the program, minutes. A cycle is not
    // a move list: the drill goes down the hole and back, once per hole, and
    // the plan cannot see that from the moves. The planner of a drilling
    // operation adds it here, so `estimateSeconds` charges the rapid travel of
    // the holes and the drilling of the holes separately.
    double cycleMinutes = 0.0;
    std::string uuid;
};

// Cutting length of one cut move from `from`: `ArcOffset::lengthMm` when the
// move carries an arc (the planner measured it), the chord otherwise. Rapids
// are not cutting and give zero. One formula for the planner, the time estimate
// and the console summary, so the three numbers agree on the same moves.
double moveLength(const Move& move, const Point3& from);

// Stable identifier of one operation inside a planning result:
// "<path uuid>/op<n>", n counted from 1 over the enabled operations. It is
// unique inside a file and reproducible between runs, so a stale preview and
// a golden file can name the same block.
std::string operationUuid(const std::string& programUuid, int opIndex);

// Collector of moves for one planner. Bounds and the cutting length are
// updated on every append, so a planner cannot forget them; an empty path
// keeps `bbox.valid` false instead of reporting a fake zero box.
struct PathBuilder {
    std::vector<Move>& moves;
    PathBounds bbox;
    double lengthMm = 0.0;  // cutting length, mm

    explicit PathBuilder(std::vector<Move>& target) : moves(target) {}
    // A cut carries its feed; a rapid carries none (Move::hasFeed). Both grow
    // the bounds.
    void add(MoveKind kind, const Point3& to, double feedMmMin = 0.0);
    // An arc move: the same bounds and the same move list as `add`, but the
    // cutting length is the arc (`path::moveLength`), not its chord. The sweep
    // follows from the I/J of the move and the end point; `add` cannot see it,
    // so an arc is appended through here and never through `add`.
    void addArc(const Move& move);
    void grow(const Point3& point);
};

// Face-specific planner: declared here so `path.cpp` and `path_face.cpp` share
// one contract and the dispatch stays a visitor over the variant.
void planFace(const Program& program, const Operation& operation, PathBuilder& out);

// Rectangular pocket planner (`path_pocket.h` declares it next to its stats
// and warnings); the same contract as planFace.
void planRectPocket(const Program& program, const Operation& operation, PathBuilder& out);
// Drilling planner (`path_drill.h`): one move per hole centre, the cycle is
// written by the emitter.
void planDrill(const Program& program, const Operation& operation, PathBuilder& out);

// Filled by the face planner for the console summary and the checks: the
// numbers are the planner's own, not recomputed by the caller.
struct FaceStats {
    int strips = 0;                 // strips per pass
    int passes = 0;                 // passes along Z
    std::vector<double> stepDepthsMm;  // bottom Z of each pass, top down
    double toolRadiusMm = 0.0;
    double stripStepMm = 0.0;       // across step between neighbouring strips, mm
    double edgeOverrunMm = 0.0;     // along-strip lead beyond the stock, per side, mm
    double sideOverrunMm = 0.0;     // across the strips beyond the stock, per side (face: 0)
    // How far below the material top the cutting descends, mm: the descent of
    // the plunge and the ramp of one strip is a cut, never a rapid.
    double leadMm = 0.0;
    int moves = 0;
    double lengthMm = 0.0;          // cutting length, mm
};

// Operation -> moves. Disabled operations are skipped; a valid document is
// assumed (validate first). The face, the rectangular and the circular pocket
// and the three drilling kinds are the planned kinds; every kind of the
// variant has a planner, so no operation is silently dropped.
Path planPath(const Program& program);

// Face layout numbers for one operation: strips, passes, step depths. Uses the
// same arithmetic as the planner, so the console summary and the checks read
// one source. A program is needed for the stock bounds.
FaceStats faceStats(const Program& program, const Operation& operation);

// Russian warnings of the face layout that do not block planning, one line
// each: a thin last pass and a plane above the stock top. Blocking problems
// stay in `validate`; this list is the "planned, but look" set the console
// prints after the checks. Empty when there is nothing to say.
std::vector<std::string> faceWarnings(const Program& program, const Operation& operation);

// Time estimate of the path, seconds: each cut at its programmed feed, each
// rapid at the machine rapid rate of limits.h. An arc is charged by its arc
// length, not its chord: the chord of a half turn understates the time by pi/2.
double estimateSeconds(const Path& path);


// Russian summary lines of the console (PLAN-QUICKPROG, 7.2, Q0). The numbers
// come from the caller - the modelled operation and its table source - so the
// summary cannot invent a regime of its own. They live in the core, not in
// main.cpp, because the wording is part of what the operator and the check read.
std::string regimeSummary(const Operation& operation, const std::string& source);
std::string totalSummary(std::size_t operations, std::size_t moves, double cuttingMm,
                         double seconds);

} // namespace qp
