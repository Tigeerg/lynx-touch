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
struct ToolRef {
    int number = 0;
    double diameterMm = 0.0;
};

// Cutting data of one operation. `feedMmMin` is the XY feed, `plungeFeedMmMin`
// the Z plunge; separate values because plunging at the cutting feed burns
// small end mills.
struct Cutting {
    double spindleRpm = 0.0;
    double feedMmMin = 0.0;
    double plungeFeedMmMin = 0.0;
};

enum class Coolant { Off, Mist, Flood };

// Face milling of the stock top: strips across the stock at `stepOverPct` of
// the tool diameter. `z` is the plane to cut, which is not always stock max.z:
// the operator may take a light pass.
struct FaceOp {
    double z = 0.0;
    double stepOverPct = 70.0;
};

// Rectangular pocket. `cornerRadiusMm` is the radius of the contour corners,
// `finishAllowanceMm` the stock left on the wall and the floor for a later
// finishing pass.
struct RectPocketOp {
    Rect rect;
    double cornerRadiusMm = 0.0;
    double finishAllowanceMm = 0.0;
};

struct CirclePocketOp {
    Circle circle;
    double finishAllowanceMm = 0.0;
};

// A single hole. `peckMm` > 0 selects the pecking cycle G83 with that Q, zero
// selects the simple G81.
struct DrillHoleOp {
    Circle hole;
    double peckMm = 0.0;
};

// Grid of holes: `cols` x `rows` starting at (x0, y0) with the spacing given.
struct DrillGridOp {
    double x0 = 0.0;
    double y0 = 0.0;
    int cols = 1;
    int rows = 1;
    double dx = 0.0;
    double dy = 0.0;
    double peckMm = 0.0;
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
    double peckMm = 0.0;
};

using OperationGeometry =
    std::variant<FaceOp, RectPocketOp, CirclePocketOp, DrillHoleOp, DrillGridOp, DrillCircleOp>;

// One operation of the program. `id` is stable across edits: the form and the
// list address an operation by it, not by index.
struct Operation {
    std::string id;
    bool enabled = true;
    ToolRef tool;
    Cutting cutting;
    Coolant coolant = Coolant::Flood;

    // `topZ` is the top of the material for this operation, `depthMm` the depth
    // below it (positive), `stepDownMm` the maximum axial cut per pass.
    double topZ = 0.0;
    double depthMm = 0.0;
    double stepDownMm = 0.0;

    std::string notes;
    OperationGeometry geometry;
};

// The saved document: schema 1 of the .qprog file. It is not machine state
// (PLAN-QUICKPROG, 6): it is loaded into the machine only by an explicit
// command.
struct Program {
    int schema = 1;
    std::string name;
    std::string units = "mm";
    // One of G54..G59 as written in the file, without trailing spaces.
    std::string wcs = "G54";
    StockBox stock;
    // Height the tool retreats to between passes and at the end; must be above
    // the stock top (PLAN-QUICKPROG, 8).
    double safeZ = 0.0;
    std::vector<Operation> operations;
};

} // namespace qp
