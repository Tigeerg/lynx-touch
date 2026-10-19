// core/stock_front.cpp - хроника реза: обход программы точным съёмом с записью
// метки времени на узел (core/stock_sim, StockField::recordEvents).
#include "core/stock_front.h"

#include <algorithm>
#include <chrono>

namespace gv {

namespace {

double msSince(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

bool StockChronicle::build(const PathData& data, const StockBounds& bounds, double cellMm,
                           const ToolInfo& initialTool, double targetMm)
{
    built_ = false;
    refused_ = false;
    refused_stage_ = -1;
    refused_tool_ = 0;
    repeated_cut_nodes_ = -1;
    StockWalk walk;
    if (!walk.start(data, bounds, cellMm, stockCutterPlan(data, initialTool), true)) {
        refused_ = walk.refusedStage() >= 0;
        refused_stage_ = walk.refusedStage();
        refused_tool_ = walk.refusedTool();
        return false;
    }
    const double target = targetMm > 0.0 ? std::min(targetMm, data.total) : data.total;
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    walk.advanceTo(target);
    build_ms_ = msSince(t0);
    moves_ = walk.moves();
    length_mm_ = walk.lengthMm();
    field_ = std::move(walk.field());
    repeated_cut_nodes_ = field_.eventOverwrites();
    built_ = true;
    return true;
}

double StockChronicle::depthAt(double tMm, int ix, int iy) const
{
    if (!built_ || field_.empty()) {
        return 0.0;
    }
    if (ix < 0 || iy < 0 || ix >= field_.countX() || iy >= field_.countY()) {
        return 0.0;
    }
    const std::size_t idx = std::size_t(iy) * std::size_t(field_.countX()) + std::size_t(ix);
    const std::vector<float>& events = field_.eventMm();
    if (idx >= events.size() || events[idx] > float(tMm)) {
        return 0.0;                        // узел углублён позже момента t
    }
    return double(field_.depthMm()[idx]);
}

double StockChronicle::heightAt(double tMm, int ix, int iy) const
{
    return field_.bounds().zMaxMm - depthAt(tMm, ix, iy);
}

const std::vector<float>& StockChronicle::viewDepthMm(double tMm) const
{
    const std::vector<float>& depth = field_.depthMm();
    const std::vector<float>& events = field_.eventMm();
    view_.assign(depth.size(), 0.0f);
    if (events.size() != depth.size()) {
        return view_;                      // хроники нет: поле пустое
    }
    const float limit = float(tMm);
    for (std::size_t i = 0; i < depth.size(); ++i) {
        view_[i] = events[i] <= limit ? depth[i] : 0.0f;
    }
    return view_;
}

} // namespace gv