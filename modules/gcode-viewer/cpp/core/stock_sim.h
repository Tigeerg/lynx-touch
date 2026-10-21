// core/stock_sim.h - stock-remainder height field: a grid over machine X/Y, and a
// node holds the material top in the column (DESIGN.md, "Симуляция съёма материала").
// The top-of-field mesh for drawing (stockSurfaceMesh) is here too: it is built from
// the same nodes, and the height is subtracted by the vertex shader from the field as
// a texture.
//
// The layer is pure, like core/collision: neither Qt nor OCCT. The node step comes as
// an argument: the core does not read CNC_QUALITY, the caller passes it the quality
// level. Only the lower envelope of the cutting-part profile (gv::toolProfile) cuts,
// up to the gauge length; the holder never cuts - otherwise the removal would hide the
// very collision the scan is looking for.
//
// Taken from DESIGN: the node as the preset tolerance, removal - the minimum down the
// column along the path, the step along the path - half a node (a proposal, hence the
// rule "a column between positions does not stay untouched"), storing depth from the
// stock top (a proposal for R16F), all moves run, including rapids.
// Mine: the node lies in the corner of the bounds, not in the centre of the cell; the
// node count is floor(extent / node) + 1 (401 at 200 mm and 0.5 mm, as in DESIGN); the
// step is computed from the 3D length of the segment, so the XY shift between
// neighbouring positions is no larger than the step; depth is clipped by the stock
// thickness; h(r) - linear interpolation over the profile vertices (a ball has 12 of
// them, hence the deviation from a true sphere of about 0.006 mm at r = 3).
//
// Live publication (StockWalk): the field and the walk cursor live between calls, the
// removal up to the current pathDist continues from the last whole move instead of
// being recomputed; backwards along the path - a reset and a walk from the start. A
// tool with undercut (lollipop, dovetail, T-slot) is not expressed by the map: the walk
// refuses entirely and no field is published (DESIGN.md, "Симуляция съёма материала").
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

#include "core/path_prep.h"
#include "gv_types.h"

namespace gv {

// Cutting profile as a radial table h(r), mm: at distance r from the axis the tip cuts
// at height tipZ + h(r), and everything above it in this column is removed. The table
// is the lower envelope of the profile up to the gauge length, computed once per stage
// tool.
struct StockCutter {
    std::vector<double> radiusMm;      // table nodes in ascending order
    std::vector<double> heightMm;      // height above the tip at the node
    double maxRadiusMm = 0.0;          // it does not cut wider
    double exposedMm = 0.0;            // tool gauge length, mm
    int number = 0;                    // UI_TOOL number: the refusal is named by it
    bool undercut = false;             // wider at the top: not expressed by the map

    // h(r), mm; +infinity outside maxRadiusMm.
    double heightAt(double radiusAtMm) const;
};

// Tool from UI_TOOL: profile gv::toolProfile, cuts up to toolExposedLength. A type
// with undercut (lollipop, dovetail, T-slot) is marked undercut: toolProfile builds a
// profile of that shape as a straight end mill, and the removal would cut away material
// that stays on the machine.
StockCutter stockCutter(const ToolInfo& info);

// Table from a ready profile (r, y), mm. Needed by the probe: the check builds the
// profile itself, not via UI_TOOL.
StockCutter stockCutterFromProfile(const std::vector<ProfilePoint>& profile,
                                   double exposedMm);

// Tools of the program stages: UI_TOOL replaces the previous one, a stage without the
// tag carries the previous one (on the machine the tool changes only on M6); before the
// first tag `initialTool` is in force. No stages - a single tool `initialTool`.
std::vector<StockCutter> stockCutterPlan(const PathData& data, const ToolInfo& initialTool);

// The stage whose tool the height map does not express, or -1: it has an undercut (the
// profile is wider at the top), and a per-column map would cut away material that stays
// on the machine. The program is counted as a whole: one such stage makes the entire
// field a lie, not just its own span.
int stockUndercutStage(const std::vector<StockCutter>& cutters);

// Stock bounds in machine coordinates, mm.
struct StockBounds {
    double xMinMm = 0.0;
    double xMaxMm = 0.0;
    double yMinMm = 0.0;
    double yMaxMm = 0.0;
    double zMinMm = 0.0;
    double zMaxMm = 0.0;
};

// Height field. The depth is stored from the stock top (float32, the way it will go
// into the R32F texture): 0 - the top is untouched. The node index is iy * countX + ix,
// row by Y, as in the texture.
class StockField {
public:
    void reset(const StockBounds& bounds, double cellMm);

    bool empty() const { return nx_ <= 0 || ny_ <= 0; }
    double cellMm() const { return cellMm_; }
    const StockBounds& bounds() const { return bounds_; }
    int countX() const { return nx_; }
    int countY() const { return ny_; }

    double nodeXMm(int ix) const { return bounds_.xMinMm + double(ix) * cellMm_; }
    double nodeYMm(int iy) const { return bounds_.yMinMm + double(iy) * cellMm_; }

    // Material top at the node, machine Z, mm.
    double heightMm(int ix, int iy) const;
    void setHeightMm(int ix, int iy, double zMm);

    // Removal at a single position: the tip at (xMm, yMm, tipZMm),
    // timeMm - the accumulated path length at the moment of the cut.
    void plunge(const StockCutter& cutter, double xMm, double yMm, double tipZMm, double timeMm);

    // Removal along segment A -> B.
    void sweep(const StockCutter& cutter,
               const std::array<double, 3>& aMm,
               const std::array<double, 3>& bMm,
               double timeMm);

    // Depths for the texture: countX * countY float32.
    const std::vector<float>& depthMm() const { return depth_; }
    // Cut times for the texture: countX * countY float32.
    const std::vector<float>& depthTimeMm() const { return depth_time_; }

private:
    StockBounds bounds_;
    double cellMm_ = 0.5;
    int nx_ = 0;
    int ny_ = 0;
    std::vector<float> depth_;
    std::vector<float> depth_time_;
};

// Walk over the program with the field and a cursor: the removal is monotone, so the
// walk continues from the last whole move, and a live-publication tick pays only for the
// new section of the path. Backwards along the path (the target behind the cursor) the
// field is reset and the walk starts from the beginning. The object is not thread-safe:
// one thread at a time touches the field and the cursor.
class StockWalk {
public:
    // Start the walk from scratch: the field for the bounds and the cell. False - there
    // will be no field: the stage tool is not expressed by the map (refusedStage) or
    // there is nothing to walk (fewer than two moves). The field is then empty.
    bool start(const PathData& data, const StockBounds& bounds, double cellMm,
               std::vector<StockCutter> cutters);

    bool started() const { return started_; }
    int refusedStage() const { return refused_stage_; }
    int refusedTool() const { return refused_tool_; }
    // Path length up to which the removal is already in the field, mm.
    double cursorMm() const { return cursor_mm_; }

    // Continue the walk up to path length targetMm. Moves are taken whole: a move that
    // ends later than the target waits for the next call. `cancelled(pathDist)` - as in
    // simulateStock, a false return means the walk was interrupted by the token and is
    // incomplete. `progress(pathDist)` - after a whole move, as in simulateStock.
    bool advanceTo(double targetMm,
                   const std::function<bool(double pathDist)>& cancelled = {},
                   const std::function<void(double pathDist)>& progress = {});

    StockField& field() { return field_; }
    const StockField& field() const { return field_; }
    int moves() const { return moves_; }
    double lengthMm() const { return length_mm_; }
    // Time of the last field reset (allocation) and of the walk after it, ms: they
    // measure the rewind, and their sum is the price of a full pass.
    double lastResetMs() const { return last_reset_ms_; }
    double advanceMs() const { return advance_ms_; }

private:
    // The field from scratch, the cursor at the start of the program.
    void restart();

    const PathData* data_ = nullptr;
    std::vector<StockCutter> cutters_;
    std::vector<double> starts_;        // start of the stage along the path length, mm
    StockBounds bounds_;
    double cellMm_ = 0.5;
    StockField field_;
    std::size_t next_ = 1;              // index of the end point of the next move
    std::size_t stage_ = 0;
    double cursor_mm_ = 0.0;
    int moves_ = 0;
    double length_mm_ = 0.0;
    double last_reset_ms_ = 0.0;
    double advance_ms_ = 0.0;
    bool started_ = false;
    int refused_stage_ = -1;
    int refused_tool_ = 0;
};

// Top grid of the field for drawing: one vertex per field node, texcoord - the centre
// of its texel. Format of 20 B per vertex (position 3f + texcoord 2f), as in the
// toolpath buffers; positions are machine mm, and a node holds the stock top
// (zMaxMm): the height is subtracted by the vertex shader, which reads the field as an
// R32F texture, - that is why the grid is the same one that is computed, and there is
// no second level of sampling.
// The last node along X and along Y is stretched to the bounds: the node grid ends at
// (node count - 1) * node, and without stretching the top would not cover the box up to
// its wall (a gap of no more than a node). The indices are two triangles per cell.
//
// subdiv > 1 splits each field cell into subdiv x subdiv subcells: the subcell nodes lie
// on the field node grid (step node/subdiv), the texcoord is the same cell fraction, and
// countX/countY are the counts of the ALREADY DRAWN grid. This is needed for a smooth
// look: the vertex shader takes the height bilinearly over the field nodes, and the
// denser the vertices, the finer the shading ledge (stocksurface.vert). The subdivision
// does not touch the field, its bytes or its checks - only the drawn mesh changes.
struct StockSurfaceMesh {
    std::vector<float> vertices;       // 5 float per vertex
    std::vector<std::uint32_t> indices;
    int countX = 0;
    int countY = 0;

    int triangleCount() const { return int(indices.size() / 3); }
};

// Mesh of the field top, machine mm. Empty if there are fewer than two nodes along
// either axis: there is nothing to draw. subdiv = 1 (the default) - the former grid over
// the field nodes, byte for byte the same: it is checked by the stocksurface case of the
// stock_sim_probe probe.
StockSurfaceMesh stockSurfaceMesh(const StockBounds& bounds, double cellMm,
                                  int countX, int countY, int subdiv = 1);

// Walls and bottom of the stock box: five rectangles, there is no top face - the field
// draws it. Positions are machine mm, the box is larger than the bounds by the gaps:
// they separate coincident faces of the stock and the part (the former fractions of a
// pixel), and they do not move the physical bounds. The edge of the box is the bounds
// itself, not the node grid: the last field node is stretched to the bounds (see
// stockSurfaceMesh), so the top and the walls meet without a gap.
std::vector<float> stockShellVertices(const StockBounds& bounds, double cellMm,
                                      int countX, int countY,
                                      double gapSideMm, double gapTopMm,
                                      double gapBottomMm);

// Result of a pass over the toolpath: the field and its price. `cancelled` - the pass was
// interrupted by the token, the field is incomplete and must not be published. `refused` -
// the program tool is not expressed by the height map (undercut): there is no field at
// all, `refusedStage` names the stage (from zero), `refusedTool` - the UI_TOOL number.
struct StockRunResult {
    StockField field;
    int moves = 0;
    double lengthMm = 0.0;
    double resetMs = 0.0;
    double computeMs = 0.0;
    bool cancelled = false;
    bool refused = false;
    int refusedStage = -1;
    int refusedTool = 0;
};

// The field for a ready toolpath: all moves, including rapids, the stage tool from
// stage.info (a stage without UI_TOOL carries the previous one, `initialTool` - before
// the first tag). `pts` is in scene coordinates, as in PathData: here the points are
// converted back to machine ones (x, -z, y) so that the removal runs over the machine
// grid.
//
// The walk runs via StockWalk to the end of the program; the same object with a target
// smaller than the full length gives the field up to the current pathDist (live
// publication, see StockWalk).
//
// `cancelled(pathDist)` is called before each move; true - the pass breaks off with no
// result. `progress(pathDist)` - after a move, along the same accumulated path length as
// in the collision scan. Both callbacks are optional. The thread is single: the order of
// moves is the order of the program, inside the removal there are no threads or
// reorderings, so the same path, tool and node give the same field bit for bit.
StockRunResult simulateStock(const PathData& data, const StockBounds& bounds, double cellMm,
                             const ToolInfo& initialTool,
                             const std::function<bool(double pathDist)>& cancelled = {},
                             const std::function<void(double pathDist)>& progress = {});

} // namespace gv
