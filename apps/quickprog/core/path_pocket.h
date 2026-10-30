// core/path_pocket.h - the rectangular pocket planner (PLAN-QUICKPROG, 7.2, 10).
//
// Strategy, in short: a zigzag (raster) at every Z pass, inside the rectangle
// shrunk by `toolRadius + finishAllowanceMm`. The rows run along the long side
// of the pocket; the first and the last row sit on the inner boundary, and the
// rows between them are spread evenly, so the real step never exceeds
// `D * stepOverPct` and the last row touches the wall with the required
// allowance. The tool body of the last row reaches exactly the wall minus the
// allowance: that is the point of the inset.
//
// The tool enters the material only with a vertical cut at the plunge feed on
// the edge of the pocket, inside the cutting zone, and leaves it with a cut up.
// Between two Z passes the tool goes to safe Z with a rapid.
//
// Not in this slice: `cornerRadiusMm` is not cut as an arc or a rounded corner
// - the corners stay sharp and the planner says so with a warning.
//
// The step across is the field `stepOverPct` of `RectPocketOp` (PLAN-QUICKPROG,
// 7.3): the operator picks it in the document and the console, and the planner
// reads it from the operation. `kDefaultPocketStepOverPct` is only the default
// the model and the nodes of the form start from.
#pragma once

#include <string>
#include <vector>

#include "core/ops.h"
#include "core/path.h"

namespace qp {

// The default step across the raster, % of the diameter: the value of a new
// pocket operation. The planner never reads this constant - it reads the field
// of the operation - so a document that carries a step plans by that step.
constexpr double kDefaultPocketStepOverPct = 70.0;

// Numbers of the pocket layout for the console summary and the checks: the
// planner's own, not recomputed by the caller. A layout that cannot be planned
// (the tool does not fit the inset rectangle) is reported with `fits` false and
// empty rows; the check `validate` blocks it before planning.
struct PocketStats {
    bool fits = false;
    int rows = 0;                  // raster rows per Z pass
    int passes = 0;                // Z passes
    std::vector<double> stepDepthsMm;  // bottom Z of each pass, top down
    double innerXMm = 0.0;         // x of the first and the last row, the inset low edge
    double innerYMm = 0.0;         // y of the first row, the inset low edge
    double innerWidthMm = 0.0;     // span of the rows, along X
    double innerHeightMm = 0.0;    // span across the rows, along Y
    double stepMm = 0.0;           // real step between rows, mm
    double toolRadiusMm = 0.0;
    double insetMm = 0.0;          // toolRadius + finishAllowance, mm
    int moves = 0;
    double lengthMm = 0.0;         // cutting length, mm
};

// Pocket layout numbers for one operation. The arithmetic is the planner's,
// so the console summary, the warnings and the moves read one source.
PocketStats pocketStats(const Program& program, const Operation& operation);

// Russian warnings of the pocket layout that do not block planning: a rounded
// corner that will not be cut, and a raster step at 100 % of the diameter.
std::vector<std::string> pocketWarnings(const Program& program, const Operation& operation);

// Appends the moves of the pocket to `out`. The document is assumed valid
// (`validate` ran first); a pocket that cannot be inset appends nothing.
void planRectPocket(const Program& program, const Operation& operation, PathBuilder& out);

} // namespace qp
