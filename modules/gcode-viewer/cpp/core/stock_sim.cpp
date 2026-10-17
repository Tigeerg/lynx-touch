// core/stock_sim.cpp - поле высот остатка заготовки: съём минимумом по колонке
// вдоль пути (DESIGN.md, "Симуляция съёма материала").
//
// Отрезок обходится положениями с шагом не крупнее половины узла: без этого
// колонка между двумя положениями осталась бы нетронутой при узле около
// диаметра инструмента. Диск под инструментом обходится по габариту, а не по
// всей сетке: цена положения пропорциональна площади следа, а не заготовке.
#include "core/stock_sim.h"

#include <algorithm>
#include <cmath>

#include "core/tooling.h"

namespace gv {

namespace {

// Локальный допуск сравнения радиусов профиля, мм: вершины toolProfile лежат
// на окружностях, и разность меньше 1e-9 - это та же окружность.
constexpr double RADIUS_EPS_MM = 1e-9;

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
    // Между вершинами профиль - отрезок: h(r) линейна.
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
    // Выше вылета профиль не режет. Нулевой вылет - вылет неизвестен, режет всё.
    const double topMm = exposedMm > 0.0 ? exposedMm : std::numeric_limits<double>::infinity();

    // Нижняя огибающая: идём от острия, пока радиус растёт. Убывание радиуса -
    // шейка или хвостовик, они огибающую не понижают. Рост радиуса после
    // убывания - поднутрение (лоллипоп): картой оно не выражается, флаг для
    // вызывающего.
    // toolProfile начинает профиль на оси: точка (0, 0).
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
    return stockCutterFromProfile(toolProfile(info), toolExposedLength(info));
}

void StockField::reset(const StockBounds& bounds, double cellMm)
{
    bounds_ = bounds;
    cellMm_ = cellMm;
    nx_ = 0;
    ny_ = 0;
    depth_.clear();
    const double extentX = bounds.xMaxMm - bounds.xMinMm;
    const double extentY = bounds.yMaxMm - bounds.yMinMm;
    if (!(cellMm > 0.0) || !(extentX > 0.0) || !(extentY > 0.0)) {
        return;
    }
    nx_ = int(std::floor(extentX / cellMm)) + 1;
    ny_ = int(std::floor(extentY / cellMm)) + 1;
    depth_.assign(std::size_t(nx_) * std::size_t(ny_), 0.0f);
}

double StockField::heightMm(int ix, int iy) const
{
    return bounds_.zMaxMm - double(depth_[std::size_t(iy) * std::size_t(nx_) + std::size_t(ix)]);
}

void StockField::setHeightMm(int ix, int iy, double zMm)
{
    const double thicknessMm = bounds_.zMaxMm - bounds_.zMinMm;
    double depth = bounds_.zMaxMm - zMm;
    depth = std::min(std::max(depth, 0.0), thicknessMm);
    depth_[std::size_t(iy) * std::size_t(nx_) + std::size_t(ix)] = float(depth);
}

void StockField::plunge(const StockCutter& cutter, double xMm, double yMm, double tipZMm)
{
    if (empty() || cutter.maxRadiusMm <= 0.0) {
        return;
    }
    // Острие - самая низкая точка инструмента, а h(r) >= 0: выше верха заготовки
    // не режет ничего. Быстрый ход над заготовкой не тратит обход диска.
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
            // Верх материала над резом снимается; ниже реза материал остаётся.
            double depth = topMm - (tipZMm + height);
            if (!(depth > 0.0)) {
                continue;
            }
            if (depth > thicknessMm) {
                depth = thicknessMm;            // ниже дна заготовки материала нет
            }
            // Хранится глубина, поэтому съём - максимум: чем глубже рез, тем
            // больше число. Сверять высоты здесь значило бы держать минимум
            // высоты, то есть 0 (нетронутый верх) для любой колонки.
            float& cell = depth_[row + std::size_t(ix)];
            const float value = float(depth);
            if (value > cell) {
                cell = value;
            }
        }
    }
}

void StockField::sweep(const StockCutter& cutter,
                       const std::array<double, 3>& aMm,
                       const std::array<double, 3>& bMm)
{
    if (empty() || cutter.maxRadiusMm <= 0.0) {
        return;
    }
    const double dx = bMm[0] - aMm[0];
    const double dy = bMm[1] - aMm[1];
    const double dz = bMm[2] - aMm[2];
    const double lengthMm = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (!(lengthMm > 0.0)) {
        plunge(cutter, aMm[0], aMm[1], aMm[2]);
        return;
    }
    const double stepMm = cellMm_ * 0.5;       // предложение DESIGN: половина узла
    const int steps = (stepMm > 0.0 && lengthMm > stepMm)
            ? int(std::ceil(lengthMm / stepMm))
            : 1;
    for (int k = 0; k <= steps; ++k) {
        const double t = double(k) / double(steps);
        plunge(cutter, aMm[0] + dx * t, aMm[1] + dy * t, aMm[2] + dz * t);
    }
}

} // namespace gv
