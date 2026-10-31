// core/ops.h - the quick program document (PLAN-QUICKPROG, 6).
//
// Common fields of an operation are in Operation; the geometry and the
// parameters that exist only for one kind are in a separate struct reachable
// through std::variant. Why a variant and not `enum kind` plus fields: the
// compiler then makes an operation with the geometry of three kinds at once
// impossible, and a visitor that forgets a kind does not compile, so a new
// kind cannot be silently ignored by the planner or the form. The document
// itself is copyable and immutable by value, as the panel state expects
// (CLAUDE.md, section 3).
#pragma once

#include <string>
#include <variant>
#include <vector>

#include "core/geom.h"

namespace qp {

// Stock as its lower and upper corner, mm, in machine coordinates. Storing two
// corners instead of origin plus size keeps the UI_STOCK tag and the safe Z
// check straightforward: the top of the stock is `max.z`.
struct StockBox {
    Point3 min;
    Point3 max;
};

// Tool number in the machine table; the diameter is repeated in the operation
// because the document must stay readable and checkable without the table.
// `hNumber` is the row of the length compensation table the G43 line names
// (PLAN-QUICKPROG, 7.3). A zero means "the tool number": the common case of a
// stand whose table rows follow the tool numbers, and the value an older
// .qprog without the field gets. The emitter never writes H0: a negative
// number is refused by `validate`, a zero is replaced by the tool number.
struct ToolRef {
    int number = 0;
    double diameterMm = 0.0;
    int hNumber = 0;

    // The row of G43 for this tool: the explicit number, else the tool number.
    int effectiveH() const { return hNumber > 0 ? hNumber : number; }
};

// Cutting data of one operation. `feedMmMin` is the XY feed, `plungeFeedMmMin`
// the Z plunge; separate values because plunging at the cutting feed burns
// small end mills. A drill has one feed: both fields are equal there, and the
// .qprog reader and the console keep them equal (see drillFeed).
struct Cutting {
    double spindleRpm = 0.0;
    double feedMmMin = 0.0;
    double plungeFeedMmMin = 0.0;
};

enum class Coolant { Off, Mist, Flood };

// The material of the stock: the axis of the cutting-data table of feeds.cpp.
// It is a field of the operation, not of the console, because the summary and
// the checks read the row of the document: a saved .qprog keeps the row it was
// planned by. The table has exactly these two rows; the default is aluminium,
// the row every older .qprog and the demo were planned by.
enum class Material { Aluminium, Steel };

// Face milling of the stock top: strips across the stock at `stepOverPct` of
// the tool diameter. `z` is the plane to cut, which is not always stock max.z:
// the operator may take a light pass.
struct FaceOp {
    double z = 0.0;
    double stepOverPct = 70.0;
};

// Rectangular pocket. `cornerRadiusMm` is the radius of the contour corners,
// which the raster does not cut (the corner keeps the tool radius). The
// `finishAllowanceMm` is the stock left on the walls only: the floor is cut to
// the final depth in the same pass, no allowance is left on it. `stepOverPct`
// is the raster step across, % of the tool diameter: a field of the operation
// (PLAN-QUICKPROG, 7.3), not a constant of the planner.
struct RectPocketOp {
    Rect rect;
    double cornerRadiusMm = 0.0;
    double finishAllowanceMm = 0.0;
    double stepOverPct = 70.0;
};

// Round pocket; the allowance is the wall stock, as for the rectangle, and
// `stepOverPct` is the radial step of the spiral, % of the diameter.
struct CirclePocketOp {
    Circle circle;
    double finishAllowanceMm = 0.0;
    double stepOverPct = 70.0;
};

// The material of the drill body: HSS (high-speed steel) or carbide. It is a
// field of the drilling operation, not of ToolRef, because ToolRef is shared by
// the milling kinds and carries only what the G43 table needs (number,
// diameter, H); the drill material changes the cutting-data row only and has no
// mill counterpart. The default is HSS: the number an older .qprog without the
// field gets, and the conservative row of the table.
enum class DrillBody { Hss, Carbide };

// Drilling parameters shared by the three kinds. `peckMm` > 0 selects the
// pecking cycle G83 with that Q, zero leaves the choice to the planner (G83
// over three diameters, G81 otherwise). `rMm` is the R plane of the cycle, and
// `hasR` says whether the document set it: a zero is not "R at the top" but
// "the default", the same way a zero step is not a step. The default is one
// millimetre above the material top, below safe Z. Both fields are part of the
// document, so the .qprog file and the console can write them. `kind` is the
// material of the drill body and selects its row of the cutting-data table.
struct DrillParams {
    double peckMm = 0.0;
    double rMm = 0.0;
    bool hasR = false;
    DrillBody kind = DrillBody::Hss;
};

// A single hole at the centre of `hole`. The radius of `hole` is the drill
// radius the form shows next to the tool number.
struct DrillHoleOp {
    Circle hole;
    DrillParams params;
};

// Grid of holes: `cols` x `rows` starting at (x0, y0) with the spacing given.
struct DrillGridOp {
    double x0 = 0.0;
    double y0 = 0.0;
    int cols = 1;
    int rows = 1;
    double dx = 0.0;
    double dy = 0.0;
    DrillParams params;
};

// Holes on a full circle: the first hole at `startDeg` from the +X axis,
// `count` holes clockwise with `fullTurn` true, or spread over `arcDeg` with
// `fullTurn` false.
struct DrillCircleOp {
    Circle circle;
    int count = 1;
    double startDeg = 0.0;
    double arcDeg = 360.0;
    bool fullTurn = true;
    DrillParams params;
};

// ---- the kinds the demo reads but does not plan (PLAN-QUICKPROG, 7.6) ------
//
// The panel writes every kind of the document into the same schema-1 file; the
// demo reads the whole file (the round-trip is two-way) and plans only face,
// pocket, circle-pocket and drill. The kinds below therefore carry their keys
// and nothing else: no planner, no summary line, no emitter. They are data of
// the reader, so a new key is one field here, one branch of the value reader
// and one of the key table. A default equals the panel document default, so a
// kind with a missing optional line reads as the form would open it.

// A slot milled along a segment: the two end points and the width of the cut.
struct SlotOp {
    Point3 from;
    Point3 to;
    double widthMm = 0.0;
    double stepOverPct = 70.0;
};

// A chamfer along a segment: the leg of the cut, mm.
struct ChamferOp {
    Point3 from;
    Point3 to;
    double chamferMm = 0.0;
};

// A closed or open profile: pairs `x y` at the operation's Z; `closed` joins
// the last point to the first.
struct ContourOp {
    std::vector<Point3> points;
    bool closed = false;
    double allowanceMm = 0.0;
    std::string side = "material";
};

// The cutting pattern shared by a tap and a counterbore: one hole, a grid or a
// ring; the numbers are the drilling ones so the file keeps one spelling.
enum class HolePattern { Hole, Grid, Circle };

struct TapOp {
    HolePattern pattern = HolePattern::Hole;
    Circle hole;
    double x0 = 0.0;
    double y0 = 0.0;
    int cols = 1;
    int rows = 1;
    double dx = 0.0;
    double dy = 0.0;
    int count = 1;
    double startDeg = 0.0;
    double arcDeg = 360.0;
    bool fullTurn = true;
    double pitchMm = 0.0;
    double peckMm = 0.0;
};

// A counterbore: the drilling layout, the seat of the head and the dwell of
// G82. `rMm` is the R plane of the cycle; `hasR` is the panel rule "the plane
// was set" (the panel refuses a counterbore without it).
struct CounterboreOp {
    HolePattern pattern = HolePattern::Hole;
    Circle hole;
    double x0 = 0.0;
    double y0 = 0.0;
    int cols = 1;
    int rows = 1;
    double dx = 0.0;
    double dy = 0.0;
    int count = 1;
    double startDeg = 0.0;
    double arcDeg = 360.0;
    bool fullTurn = true;
    double seatDiameterMm = 0.0;
    double seatDepthMm = 0.0;
    double dwellSec = 0.2;
    double rMm = 0.0;
    bool hasR = false;
};

// A thread mill: the crest circle, the pitch, the side and the depth of the
// thread; `stepOverPct` is the radial step of the finish turns.
struct ThreadOp {
    Circle hole;
    double pitchMm = 0.0;
    bool internal = true;
    double threadDepthMm = 0.0;
    double threadAllowanceMm = 0.0;
    double stepOverPct = 70.0;
};

// An engraved string: the text and the frame of the string on the part.
struct EngraveOp {
    std::string text;
    double x = 0.0;
    double y = 0.0;
    double heightMm = 0.0;
    double angleDeg = 0.0;
};

// A pocket cut around an island: two rectangles and the stock left on the
// island wall.
struct IslandPocketOp {
    Rect rect;
    Rect island;
    double islandAllowanceMm = 0.0;
    double stepOverPct = 70.0;
};

// A rest-milling pass: the operations it follows (`source`, document indexes;
// empty writes a bare `op.source` line) and the footprint of the pass.
struct RestOp {
    std::vector<int> source;
    Rect rect;
    double allowanceMm = 0.0;
    double stepOverPct = 70.0;
};

// A custom block: the program lines as typed, one `op.gcode` line each. A bare
// key is an empty line.
struct CustomOp {
    std::vector<std::string> gcode;
};

// A probe block: the touch points with all three coordinates (a touch along X
// or Y names the surface side too), the search travel, the touch feed and the
// axis of G38.2.
enum class ProbeDirection { X, Y, Z };

struct ProbeOp {
    std::vector<Point3> points;
    double rangeMm = 0.0;
    double touchFeedMmMin = 0.0;
    ProbeDirection axis = ProbeDirection::Z;
};

using OperationGeometry =
    std::variant<FaceOp, RectPocketOp, CirclePocketOp, DrillHoleOp, DrillGridOp, DrillCircleOp, ContourOp,
                 SlotOp, ChamferOp, TapOp, CounterboreOp, ThreadOp, EngraveOp, IslandPocketOp, RestOp,
                 CustomOp, ProbeOp>;

// True for the kinds the demo plans: face, pocket, circle-pocket and the three
// drills. A false here means "read, named in the summary as skipped, never
// planned and never emitted": the console prints the skip line instead of a
// span, so the file the demo hears of is still readable (PLAN-QUICKPROG, 7.6).
bool isPlanned(const OperationGeometry& geometry);


// One operation of the program. `id` is stable across edits: the form and the
// list address an operation by it, not by index.
struct Operation {
    std::string id;
    bool enabled = true;
    ToolRef tool;
    Cutting cutting;
    Coolant coolant = Coolant::Flood;
    // The row of the cutting-data table this operation is rated by, for a mill
    // and for a drill alike.
    Material material = Material::Aluminium;

    // `topZ` is the top of the material for this operation, `depthMm` the depth
    // below it (positive), `stepDownMm` the maximum axial cut per pass.
    double topZ = 0.0;
    double depthMm = 0.0;
    double stepDownMm = 0.0;

    std::string notes;
    OperationGeometry geometry;
};

// The schema of the .qprog file this build writes and reads. One constant, not
// a literal in the writer and a literal in the reader: the saved document
// carries it and the reader names it, so a new schema is one edit and cannot
// leave the writer and the reader disagreeing.
constexpr int kQprogSchema = 1;

// The saved document: schema 1 of the .qprog file. It is not machine state
// (PLAN-QUICKPROG, 6): it is loaded into the machine only by an explicit
// command.
struct Program {
    int schema = kQprogSchema;
    std::string name;
    std::string units = "mm";
    // One of G54..G59 as written in the file, without trailing spaces.
    std::string wcs = "G54";
    StockBox stock;
    // Height the tool retreats to between passes and at the end; must be above
    // the stock top (PLAN-QUICKPROG, 8).
    double safeZ = 0.0;
    // The path tolerance of G64 P.. Q..: P is the corner rounding, Q the
    // following error, both in mm (PLAN-QUICKPROG, 7.3). The reference post
    // writes P0.01 Q0.01; the emitter refuses a value that is not a positive
    // fraction of a millimetre.
    double g64P = 0.01;
    double g64Q = 0.01;
    // An M1 stop before every operation after the first: the operator looks at
    // the channel and clears the chips (PLAN-QUICKPROG, 7.3). On by default;
    // off removes every M1 and keeps the block numbers of the rest.
    bool stopAfterOp = true;
    std::vector<Operation> operations;
};

} // namespace qp
