// core/stock_sim.cpp - stock-remainder height field: removal by the column minimum
// along the path (DESIGN.md, "Симуляция съёма материала").
//
// A segment is walked by positions with a step no larger than half a node: without it
// the column between two positions would stay untouched for a node about the tool
// diameter. The disc under the tool is walked over the bounds, not over the whole grid:
// the price of a position is proportional to the trace area, not to the stock. Here too
// is the t_cut time field, which stores for each node the path length at which it was
// cut.
#include "core/stock_sim.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

#include "core/tooling.h"

namespace gv {

namespace {

// Local tolerance for comparing profile radii, mm: toolProfile vertices lie on circles,
// and a difference smaller than 1e-9 is the same circle.
constexpr double RADIUS_EPS_MM = 1e-9;

// Milliseconds of the monotonic clock: the price of a pass is measured here, not by the
// Qt layer.
double msSince(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

double StockCutter::heightAt(double radiusAtMm) const
{
    const std::size_t n = radiusMm.size();
    if (n == 0 || radiusAtMm < 0.0 || radiusAtMm > maxRadiusMm + RADIUS_EPS_MM) {
        return std::numeric_limits<double>::infinity();
    }
    if (n == 1 || radiusAtMm <= radiusMm[0]) {
        return heightMm[0];
    }
    const auto it = std::upper_bound(radiusMm.begin(), radiusMm.end(), radiusAtMm);
    const std::size_t i = std::size_t(it - radiusMm.begin()) - 1;
    if (i + 1 >= n) {
        return heightMm[n - 1];
    }
    // Between the vertices the profile is a segment: h(r) is linear.
    const double span = radiusMm[i + 1] - radiusMm[i];
    if (span <= 0.0) {
        return heightMm[i];
    }
    const double t = (radiusAtMm - radiusMm[i]) / span;
    return heightMm[i] + (heightMm[i + 1] - heightMm[i]) * t;
}

StockCutter stockCutterFromProfile(const std::vector<ProfilePoint>& profile,
                                   double exposedMm)
{
    StockCutter cutter;
    cutter.exposedMm = exposedMm;
    if (profile.size() < 2) {
        return cutter;
    }
    // Above the gauge length the profile does not cut. A zero gauge length means the
    // gauge length is unknown, and it cuts everything.
    const double topMm = exposedMm > 0.0 ? exposedMm : std::numeric_limits<double>::infinity();

    // Lower envelope: we go from the tip while the radius grows. A falling radius is the
    // neck or the shank, they do not lower the envelope. A radius growth after a fall is
    // an undercut (lollipop): it is not expressed by the map, a flag for the caller.
    // toolProfile starts the profile on the axis: the point (0, 0).
    double maxRadius = 0.0;
    bool shrinking = false;
    cutter.radiusMm.push_back(0.0);
    cutter.heightMm.push_back(profile.front().height_mm);
    for (std::size_t i = 1; i < profile.size(); ++i) {
        const ProfilePoint& p = profile[i];
        if (p.height_mm > topMm + RADIUS_EPS_MM) {
            continue;
        }
        if (p.radius_mm > maxRadius + RADIUS_EPS_MM) {
            if (shrinking) {
                cutter.undercut = true;
            }
            cutter.radiusMm.push_back(p.radius_mm);
            cutter.heightMm.push_back(p.height_mm);
            maxRadius = p.radius_mm;
        } else if (p.radius_mm < maxRadius - RADIUS_EPS_MM) {
            shrinking = true;
        }
    }
    cutter.maxRadiusMm = maxRadius;
    return cutter;
}

StockCutter stockCutter(const ToolInfo& info)
{
    if (!info.present) {
        return StockCutter{};
    }
    StockCutter cutter = stockCutterFromProfile(toolProfile(info), toolExposedLength(info));
    cutter.number = info.number;
    // A shape with an undercut is by definition wider at the top. toolProfile builds it
    // in the FLAT/BULL/LOLLIPOP/DOVETAIL/PROBE branch as a straight end mill, that is,
    // the shape is already lost, - therefore the refusal goes by type, not by profile.
    const std::string& type = info.type;
    if (type == "LOLLIPOP" || type == "DOVETAIL" || type == "T_SLOT" || type == "TSLOT") {
        cutter.undercut = true;
    }
    return cutter;
}

std::vector<StockCutter> stockCutterPlan(const PathData& data, const ToolInfo& initialTool)
{
    std::vector<StockCutter> cutters;
    cutters.reserve(data.stages.size());
    ToolInfo carried = initialTool;
    for (const StageRange& stage : data.stages) {
        if (stage.info.present) {
            carried = stage.info;
        }
        cutters.push_back(stockCutter(carried));
    }
    if (cutters.empty()) {
        cutters.push_back(stockCutter(carried));
    }
    return cutters;
}

int stockUndercutStage(const std::vector<StockCutter>& cutters)
{
    for (std::size_t i = 0; i < cutters.size(); ++i) {
        if (cutters[i].undercut) {
            return int(i);
        }
    }
    return -1;
}

void StockField::reset(const StockBounds& bounds, double cellMm)
{
    bounds_ = bounds;
    cellMm_ = cellMm;
    nx_ = 0;
    ny_ = 0;
    depth_.clear();
    depth_time_.clear();
    const double extentX = bounds.xMaxMm - bounds.xMinMm;
    const double extentY = bounds.yMaxMm - bounds.yMinMm;
    if (!(cellMm > 0.0) || !(extentX > 0.0) || !(extentY > 0.0)) {
        return;
    }
    nx_ = int(std::floor(extentX / cellMm)) + 1;
    ny_ = int(std::floor(extentY / cellMm)) + 1;
    const std::size_t size = std::size_t(nx_) * std::size_t(ny_);
    depth_.assign(size, 0.0f);
    depth_time_.assign(size, 0.0f);
}

double StockField::heightMm(int ix, int iy) const
{
    return bounds_.zMaxMm - double(depth_[std::size_t(iy) * std::size_t(nx_) + std::size_t(ix)]);
}

void StockField::setHeightMm(int ix, int iy, double zMm, double timeMm)
{
    const double thicknessMm = bounds_.zMaxMm - bounds_.zMinMm;
    double depth = bounds_.zMaxMm - zMm;
    depth = std::min(std::max(depth, 0.0), thicknessMm);
    const std::size_t idx = std::size_t(iy) * std::size_t(nx_) + std::size_t(ix);
    depth_[idx] = float(depth);
    depth_time_[idx] = float(timeMm);
}

void StockField::plunge(const StockCutter& cutter, double xMm, double yMm, double tipZMm, double timeMm)
{
    if (empty() || cutter.maxRadiusMm <= 0.0) {
        return;
    }
    // The tip is the lowest point of the tool, and h(r) >= 0: above the stock top nothing
    // is cut. A rapid above the stock wastes no disc walk.
    if (tipZMm >= bounds_.zMaxMm) {
        return;
    }
    const double radius = cutter.maxRadiusMm;
    const int ix0 = std::max(0, int(std::ceil((xMm - radius - bounds_.xMinMm) / cellMm_)));
    const int ix1 = std::min(nx_ - 1, int(std::floor((xMm + radius - bounds_.xMinMm) / cellMm_)));
    const int iy0 = std::max(0, int(std::ceil((yMm - radius - bounds_.yMinMm) / cellMm_)));
    const int iy1 = std::min(ny_ - 1, int(std::floor((yMm + radius - bounds_.yMinMm) / cellMm_)));
    if (ix0 > ix1 || iy0 > iy1) {
        return;
    }
    const double topMm = bounds_.zMaxMm;
    const double thicknessMm = bounds_.zMaxMm - bounds_.zMinMm;
    for (int iy = iy0; iy <= iy1; ++iy) {
        const double dy = nodeYMm(iy) - yMm;
        const std::size_t row = std::size_t(iy) * std::size_t(nx_);
        for (int ix = ix0; ix <= ix1; ++ix) {
            const double dx = nodeXMm(ix) - xMm;
            const double distance = std::sqrt(dx * dx + dy * dy);
            if (distance > radius) {
                continue;
            }
            const double height = cutter.heightAt(distance);
            if (!std::isfinite(height)) {
                continue;
            }
            // The material top above the cut is removed; below the cut the material stays.
            double depth = topMm - (tipZMm + height);
            if (!(depth > 0.0)) {
                continue;
            }
            if (depth > thicknessMm) {
                depth = thicknessMm;            // below the stock bottom there is no material
            }
            // The depth is what is stored, so the removal is the maximum: the deeper the
            // cut, the larger the number. Comparing heights would mean keeping the minimum
            // height, that is 0 (an untouched top) for any column.
            const std::size_t idx = row + std::size_t(ix);
            if (float(depth) > depth_[idx]) {
                depth_[idx] = float(depth);
                depth_time_[idx] = float(timeMm);
            }
        }
    }
}

void StockField::sweep(const StockCutter& cutter,
                       const std::array<double, 3>& aMm,
                       const std::array<double, 3>& bMm,
                       double timeMm)
{
    if (empty() || cutter.maxRadiusMm <= 0.0) {
        return;
    }
    const double dx = bMm[0] - aMm[0];
    const double dy = bMm[1] - aMm[1];
    const double dz = bMm[2] - aMm[2];
    const double lengthMm = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(lengthMm > 0.0)) {
        plunge(cutter, aMm[0], aMm[1], aMm[2], timeMm);
        return;
    }
    const double stepMm = cellMm_ * 0.5;
    const int steps = (stepMm > 0.0 && lengthMm > stepMm)
            ? int(std::ceil(lengthMm / stepMm))
            : 1;
    for (int k = 0; k <= steps; ++k) {
        const double t = double(k) / double(steps);
        plunge(cutter, aMm[0] + dx * t, aMm[1] + dy * t, aMm[2] + dz * t, timeMm);
    }
}

StockSurfaceMesh stockSurfaceMesh(const StockBounds& bounds, double cellMm,
                                  int countX, int countY, int subdiv)
{
    StockSurfaceMesh mesh;
    if (countX < 2 || countY < 2 || !(cellMm > 0.0)) {
        return mesh;
    }
    // Number of subcells per field cell: 1 - the former grid over the nodes.
    const int k = subdiv < 1 ? 1 : subdiv;
    const int drawnX = (countX - 1) * k + 1;
    const int drawnY = (countY - 1) * k + 1;
    mesh.countX = drawnX;
    mesh.countY = drawnY;

    const float zTop = float(bounds.zMaxMm);

    mesh.vertices.reserve(std::size_t(drawnX) * std::size_t(drawnY) * 5);
    for (int jy = 0; jy < drawnY; ++jy) {
        // Coordinate in field nodes: the integer part is the node number, the fraction is
        // the cell fraction. At k = 1 this is exactly the node number, so the former buffer
        // is reproduced bit for bit.
        const double ty = double(jy) / double(k);
        // A field node, and in the last row - stretched to the bounds (see the header):
        // otherwise the top would not reach the box wall.
        const double nodeY = bounds.yMinMm + ty * cellMm;
        const double y = (jy == drawnY - 1) ? std::min(nodeY, bounds.yMaxMm) : nodeY;
        const float v = float((ty + 0.5) / double(countY));
        for (int jx = 0; jx < drawnX; ++jx) {
            const double tx = double(jx) / double(k);
            const double nodeX = bounds.xMinMm + tx * cellMm;
            const double x = (jx == drawnX - 1) ? std::min(nodeX, bounds.xMaxMm) : nodeX;
            const float u = float((tx + 0.5) / double(countX));
            mesh.vertices.push_back(float(x));
            mesh.vertices.push_back(float(y));
            mesh.vertices.push_back(zTop);
            mesh.vertices.push_back(u);
            mesh.vertices.push_back(v);
        }
    }

    const std::size_t quads = std::size_t(drawnX - 1) * std::size_t(drawnY - 1);
    mesh.indices.reserve(quads * 6);
    for (int jy = 0; jy + 1 < drawnY; ++jy) {
        for (int jx = 0; jx + 1 < drawnX; ++jx) {
            const std::uint32_t a = std::uint32_t(jy * drawnX + jx);
            const std::uint32_t b = a + 1;
            const std::uint32_t d = a + std::uint32_t(drawnX);
            const std::uint32_t c = d + 1;
            mesh.indices.push_back(a);
            mesh.indices.push_back(b);
            mesh.indices.push_back(c);
            mesh.indices.push_back(a);
            mesh.indices.push_back(c);
            mesh.indices.push_back(d);
        }
    }
    return mesh;
}

std::vector<float> stockShellVertices(const StockBounds& bounds, double cellMm,
                                      int countX, int countY,
                                      double gapSideMm, double gapTopMm,
                                      double gapBottomMm)
{
    std::vector<float> vertices;
    if (countX < 2 || countY < 2 || !(cellMm > 0.0)) {
        return vertices;
    }
    // The edge of the box is the bounds: the last field node is stretched to it, so the top
    // and the wall meet. The wall overlaps the material top by gapTopMm - just as the former
    // box overlapped it.
    const double x0 = bounds.xMinMm - gapSideMm;
    const double x1 = bounds.xMaxMm + gapSideMm;
    const double y0 = bounds.yMinMm - gapSideMm;
    const double y1 = bounds.yMaxMm + gapSideMm;
    const double z0 = bounds.zMinMm - gapBottomMm;
    const double z1 = bounds.zMaxMm + gapTopMm;

    // Four corners of one face as triangle pairs: 0-1-2, 0-2-3.
    const auto face = [&vertices](const std::array<double, 3>& p0,
                                  const std::array<double, 3>& p1,
                                  const std::array<double, 3>& p2,
                                  const std::array<double, 3>& p3) {
        const std::array<std::array<double, 3>, 6> order{p0, p1, p2, p0, p2, p3};
        for (const std::array<double, 3>& point : order) {
            vertices.push_back(float(point[0]));
            vertices.push_back(float(point[1]));
            vertices.push_back(float(point[2]));
        }
    };

    face({x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0});   // bottom
    face({x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1});   // wall -X
    face({x1, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0});   // wall +X
    face({x0, y0, z0}, {x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0});   // wall -Y
    face({x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1});   // wall +Y
    return vertices;
}

bool StockWalk::start(const PathData& data, const StockBounds& bounds, double cellMm,
                      std::vector<StockCutter> cutters)
{
    data_ = nullptr;
    started_ = false;
    refused_stage_ = -1;
    refused_tool_ = 0;
    cutters_ = std::move(cutters);
    // The refusal is evaluated before the field allocation: for such a tool there is no
    // field at all, rather than an empty field.
    const int refused = stockUndercutStage(cutters_);
    if (refused >= 0) {
        refused_stage_ = refused;
        refused_tool_ = cutters_[std::size_t(refused)].number;
        return false;
    }
    const std::size_t n = data.pts.size() / 3;
    if (n < 2 || data.cum.size() != n || !(data.total > 0.0) || cutters_.empty()) {
        return false;                      // there is nothing to walk
    }
    data_ = &data;
    bounds_ = bounds;
    cellMm_ = cellMm;
    // Start of a stage along the accumulated path length: by the same expression as in the
    // collision scan (the stage fraction is multiplied by the full length).
    starts_.clear();
    starts_.reserve(data.stages.size());
    for (const StageRange& stage : data.stages) {
        starts_.push_back(stage.start * data.total);
    }
    restart();
    started_ = true;
    return true;
}

void StockWalk::restart()
{
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    field_.reset(bounds_, cellMm_);
    last_reset_ms_ = msSince(t0);
    next_ = 1;
    stage_ = 0;
    cursor_mm_ = 0.0;
    moves_ = 0;
    length_mm_ = 0.0;
    advance_ms_ = 0.0;
}

bool StockWalk::advanceTo(double targetMm, const std::function<bool(double)>& cancelled,
                          const std::function<void(double)>& progress)
{
    if (!started_ || data_ == nullptr) {
        return true;
    }
    // Rewind: the field is a whole stock again, the cursor is at the start. The previous
    // field lives until this call - the caller publishes it, not the walk.
    if (targetMm < cursor_mm_) {
        restart();
    }
    if (!(targetMm > cursor_mm_)) {
        return true;
    }
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    const std::size_t n = data_->pts.size() / 3;
    for (std::size_t i = next_; i < n; ++i) {
        const double d = data_->cum[i];
        if (d > targetMm) {
            break;                         // the move ends later than the target: leave it
        }
        if (cancelled && cancelled(d)) {
            advance_ms_ += msSince(t0);
            return false;
        }
        // The stage is determined by the path length before the move start: d does not
        // decrease, so the pointer only moves forward and survives ticks.
        const double d0 = data_->cum[i - 1];
        while (stage_ + 1 < starts_.size() && starts_[stage_ + 1] <= d0) {
            ++stage_;
        }
        // PathData stores scene points (X, Z, -Y): back to machine ones.
        const std::array<double, 3> a{data_->pts[(i - 1) * 3], -data_->pts[(i - 1) * 3 + 2],
                                      data_->pts[(i - 1) * 3 + 1]};
        const std::array<double, 3> b{data_->pts[i * 3], -data_->pts[i * 3 + 2],
                                      data_->pts[i * 3 + 1]};
        const StockCutter& cutter = cutters_[std::min(stage_, cutters_.size() - 1)];
        if (cutter.maxRadiusMm > 0.0) {
            field_.sweep(cutter, a, b, d);
        }
        const double dx = b[0] - a[0];
        const double dy = b[1] - a[1];
        const double dz = b[2] - a[2];
        length_mm_ += std::sqrt(dx * dx + dy * dy + dz * dz);
        ++moves_;
        next_ = i + 1;
        cursor_mm_ = d;
        if (progress) {
            progress(d);
        }
    }
    advance_ms_ += msSince(t0);
    return true;
}

StockRunResult simulateStock(const PathData& data, const StockBounds& bounds, double cellMm,
                             const ToolInfo& initialTool,
                             const std::function<bool(double)>& cancelled,
                             const std::function<void(double)>& progress)
{
    StockRunResult out;
    StockWalk walk;
    if (!walk.start(data, bounds, cellMm, stockCutterPlan(data, initialTool))) {
        out.refused = walk.refusedStage() >= 0;
        out.refusedStage = walk.refusedStage();
        out.refusedTool = walk.refusedTool();
        return out;                        // there is no field: the reason is in refusedStage
    }
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    out.cancelled = !walk.advanceTo(data.total, cancelled, progress);
    out.computeMs = msSince(t0);
    out.resetMs = walk.lastResetMs();
    out.moves = walk.moves();
    out.lengthMm = walk.lengthMm();
    out.field = std::move(walk.field());
    return out;
}

} // namespace gv