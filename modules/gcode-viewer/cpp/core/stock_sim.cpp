// core/stock_sim.cpp - поле высот остатка заготовки: съём минимумом по колонке
// вдоль пути (DESIGN.md, "Симуляция съёма материала").
//
// Отрезок обходится положениями с шагом не крупнее половины узла: без этого
// колонка между двумя положениями осталась бы нетронутой при узле около
// диаметра инструмента. Диск под инструментом обходится по габариту, а не по
// всей сетке: цена положения пропорциональна площади следа, а не заготовке.
#include "core/stock_sim.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

#include "core/tooling.h"

namespace gv {

namespace {

// Локальный допуск сравнения радиусов профиля, мм: вершины toolProfile лежат
// на окружностях, и разность меньше 1e-9 - это та же окружность.
constexpr double RADIUS_EPS_MM = 1e-9;

// Пи как в tooling.cpp: углы профиля считаются в градусах.
constexpr double PI = 3.14159265358979323846;

// Шагов золотого сечения на дугу в точном минимуме: 60 сужают отрезок поиска
// в 1e-12, чего хватает при требовании 1e-6 мм (stock_sim_probe,
// inclined_ball_quadrature). Число фиксировано - минимум детерминирован.
constexpr int ARC_SEARCH_STEPS = 60;

// Миллисекунды монотонных часов: цена прохода меряется здесь, а не слоем Qt.
double msSince(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

namespace {

// Нижняя огибающая профиля до вылета: пары (r, h) от острия, пока радиус растёт.
// Убывание радиуса - шейка или хвостовик, они огибающую не понижают; рост радиуса
// после убывания - поднутрение. Общий вход радиальной таблицы и аналитического
// профиля: обе обязаны описывать одну форму.
struct Envelope {
    std::vector<double> radiusMm;
    std::vector<double> heightMm;
    bool undercut = false;
};

Envelope lowerEnvelope(const std::vector<ProfilePoint>& profile, double exposedMm)
{
    Envelope out;
    if (profile.size() < 2) {
        return out;
    }
    const double topMm = exposedMm > 0.0 ? exposedMm : std::numeric_limits<double>::infinity();
    double maxRadius = 0.0;
    bool shrinking = false;
    out.radiusMm.push_back(0.0);
    out.heightMm.push_back(profile.front().height_mm);
    for (std::size_t i = 1; i < profile.size(); ++i) {
        const ProfilePoint& p = profile[i];
        if (p.height_mm > topMm + RADIUS_EPS_MM) {
            continue;
        }
        if (p.radius_mm > maxRadius + RADIUS_EPS_MM) {
            if (shrinking) {
                out.undercut = true;
            }
            out.radiusMm.push_back(p.radius_mm);
            out.heightMm.push_back(p.height_mm);
            maxRadius = p.radius_mm;
        } else if (p.radius_mm < maxRadius - RADIUS_EPS_MM) {
            shrinking = true;
        }
    }
    return out;
}

// Участок-отрезок по двум точкам (r, h), мм. r1 <= r0 - участок пуст.
StockProfilePiece linePiece(double r0, double h0, double r1, double h1)
{
    StockProfilePiece p;
    p.kind = StockProfilePiece::Kind::Line;
    p.r0Mm = r0;
    p.r1Mm = r1;
    p.h0Mm = h0;
    p.h1Mm = h1;
    return p;
}

// Участок-дуга: h(r) = centerH - sqrt(arcR^2 - (r - centerR)^2), нижняя половина.
StockProfilePiece arcPiece(double r0, double r1, double centerR, double centerH, double arcR)
{
    StockProfilePiece p;
    p.kind = StockProfilePiece::Kind::Arc;
    p.r0Mm = r0;
    p.r1Mm = r1;
    p.centerRMm = centerR;
    p.centerHMm = centerH;
    p.arcRadiusMm = arcR;
    return p;
}

// Радиус, где h участка доходит до cutMm, мм: рез выше вылета материал не
// снимает, и участок обрезается там же, где обрезалась таблица.
double pieceRadiusAtHeight(const StockProfilePiece& p, double cutMm)
{
    if (!std::isfinite(cutMm)) {
        return p.r1Mm;
    }
    if (p.kind == StockProfilePiece::Kind::Line) {
        const double span = p.r1Mm - p.r0Mm;
        if (span <= 0.0) {
            return p.r1Mm;
        }
        const double b = (p.h1Mm - p.h0Mm) / span;
        if (b <= 0.0) {
            return p.r1Mm;
        }
        return p.r0Mm + (cutMm - p.h0Mm) / b;
    }
    const double dq = p.centerHMm - cutMm;
    const double inside = p.arcRadiusMm * p.arcRadiusMm - dq * dq;
    if (inside <= 0.0) {
        return p.r1Mm;
    }
    return p.centerRMm + std::sqrt(inside);
}

} // namespace

double StockProfilePiece::heightAt(double radiusMm) const
{
    if (kind == Kind::Line) {
        const double span = r1Mm - r0Mm;
        if (span <= 0.0) {
            return h0Mm;
        }
        const double t = (radiusMm - r0Mm) / span;
        return h0Mm + (h1Mm - h0Mm) * t;
    }
    const double dr = radiusMm - centerRMm;
    const double inside = arcRadiusMm * arcRadiusMm - dr * dr;
    if (inside <= 0.0) {
        return centerHMm;              // вершина дуги: край участка
    }
    return centerHMm - std::sqrt(inside);
}

double StockProfilePiece::slopeAt(double radiusMm) const
{
    if (kind == Kind::Line) {
        const double span = r1Mm - r0Mm;
        return span <= 0.0 ? 0.0 : (h1Mm - h0Mm) / span;
    }
    const double dr = radiusMm - centerRMm;
    const double inside = arcRadiusMm * arcRadiusMm - dr * dr;
    if (inside <= 1e-18) {
        return std::numeric_limits<double>::infinity();
    }
    return dr / std::sqrt(inside);
}

double StockProfile::heightAt(double radiusAtMm) const
{
    if (pieces.empty() || radiusAtMm < 0.0 || radiusAtMm > maxRadiusMm + RADIUS_EPS_MM) {
        return std::numeric_limits<double>::infinity();
    }
    if (radiusAtMm <= pieces.front().r0Mm) {
        return pieces.front().h0Mm;
    }
    for (const StockProfilePiece& piece : pieces) {
        if (radiusAtMm <= piece.r1Mm + RADIUS_EPS_MM) {
            return piece.heightAt(radiusAtMm);
        }
    }
    return pieces.back().heightAt(pieces.back().r1Mm);
}

StockProfile stockProfileFromPoints(const std::vector<ProfilePoint>& profile, double exposedMm)
{
    StockProfile out;
    const Envelope env = lowerEnvelope(profile, exposedMm);
    if (env.radiusMm.size() < 2) {
        return out;
    }
    for (std::size_t i = 0; i + 1 < env.radiusMm.size(); ++i) {
        out.pieces.push_back(linePiece(env.radiusMm[i], env.heightMm[i],
                                       env.radiusMm[i + 1], env.heightMm[i + 1]));
    }
    out.maxRadiusMm = env.radiusMm.back();
    return out;
}

StockProfile stockProfileFromTool(const ToolInfo& info)
{
    StockProfile out;
    const double r = info.diameter / 2.0;
    if (!info.present || !(r > 0.0)) {
        return out;
    }
    std::string typ = info.type;
    const double cr = info.corner_radius;
    const double taper = info.taper_deg;
    const double tipa = info.tip_angle_deg;
    const double tipd = info.tip_diameter;
    if (typ == "FLAT" && taper > 0.0 && cr <= 0.0) {
        typ = "DRILL";
    }
    const double exposed = toolExposedLength(info);
    const double cutMm = exposed > 0.0 ? exposed : std::numeric_limits<double>::infinity();

    // Участки режущей части до вылета. Формы повторяют toolProfile, но дуги
    // остаются дугами: у шара - истинная сфера, у скругления угла - истинный тор.
    std::vector<StockProfilePiece> pieces;
    if (typ == "BALL") {
        pieces.push_back(arcPiece(0.0, r, 0.0, r, r));
    } else if (typ == "DRILL" || typ == "TAP" || typ == "CHAMFER" || typ == "TAPER") {
        const double ang = (typ == "DRILL" || typ == "TAP")
                ? (taper > 0.0 ? taper : (tipa > 0.0 ? tipa : 118.0))
                : (taper > 0.0 ? taper : 45.0);
        const double divisor = (typ == "DRILL" || typ == "TAP") ? 2.0 : 1.0;
        const double tr = std::tan((std::max(1.0, std::min(89.0, ang)) / divisor) * (PI / 180.0));
        const double r0 = tipd > 0.0 ? std::min(tipd / 2.0, r) : 0.0;
        const double tipH = tr > 1e-9 ? (r - r0) / tr : r;
        if (r0 > 0.0) {
            pieces.push_back(linePiece(0.0, 0.0, r0, 0.0));
        }
        pieces.push_back(linePiece(r0, 0.0, r, tipH));
    } else if (cr > 0.0 && cr < r) {          // радиусная концевая: скругление угла
        pieces.push_back(linePiece(0.0, 0.0, r - cr, 0.0));
        pieces.push_back(arcPiece(r - cr, r, r - cr, cr, cr));
    } else {                                   // FLAT и прочие прямые формы
        pieces.push_back(linePiece(0.0, 0.0, r, 0.0));
    }

    // Обрезка по вылету: участок кончается там, где его высота доходит до вылета.
    for (const StockProfilePiece& piece : pieces) {
        StockProfilePiece p = piece;
        if (std::isfinite(cutMm)) {
            p.r1Mm = std::min(p.r1Mm, pieceRadiusAtHeight(piece, cutMm));
            if (p.kind == StockProfilePiece::Kind::Line) {
                p.h1Mm = piece.heightAt(p.r1Mm);
            }
        }
        if (p.r1Mm > p.r0Mm + RADIUS_EPS_MM) {
            out.pieces.push_back(p);
        }
    }
    if (out.pieces.empty()) {
        return out;                        // вся режущая часть выше вылета
    }
    // Огибающая начинается на оси острия: если первый участок не от нуля, он
    // терял бы материал на оси.
    if (out.pieces.front().r0Mm > RADIUS_EPS_MM) {
        out.pieces.insert(out.pieces.begin(),
                          linePiece(0.0, out.pieces.front().h0Mm,
                                    out.pieces.front().r0Mm, out.pieces.front().h0Mm));
    }
    out.maxRadiusMm = out.pieces.back().r1Mm;
    return out;
}

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
    // Нижняя огибающая профиля: идём от острия, пока радиус растёт. Убывание
    // радиуса - шейка или хвостовик, они огибающую не понижают. Рост радиуса
    // после убывания - поднутрение (лоллипоп): картой оно не выражается, флаг
    // для вызывающего.
    const Envelope env = lowerEnvelope(profile, exposedMm);
    cutter.radiusMm = env.radiusMm;
    cutter.heightMm = env.heightMm;
    cutter.undercut = env.undercut;
    cutter.maxRadiusMm = env.radiusMm.empty() ? 0.0 : env.radiusMm.back();
    // Аналитическая огибающая по тем же узлам: каждый интервал - отрезок. Ею
    // считается точный съём; форма та же, что у таблицы, - сверка случая
    // flat_sweep держит обе.
    cutter.profile = stockProfileFromPoints(profile, exposedMm);
    return cutter;
}

StockCutter stockCutter(const ToolInfo& info)
{
    if (!info.present) {
        return StockCutter{};
    }
    StockCutter cutter = stockCutterFromProfile(toolProfile(info), toolExposedLength(info));
    cutter.number = info.number;
    // Примитивы вместо хорд: у шара - истинная сфера, у скругления угла -
    // истинный тор, у фаски - отрезок. Таблица остаётся эталоном формы.
    const StockProfile analytic = stockProfileFromTool(info);
    if (!analytic.empty()) {
        cutter.profile = analytic;
        cutter.maxRadiusMm = analytic.maxRadiusMm;
    }
    // Форма с поднутрением по определению шире кверху. toolProfile строит её
    // веткой FLAT/BULL/LOLLIPOP/DOVETAIL/PROBE как прямую фрезу, то есть форма
    // уже потеряна, - поэтому отказ идёт по типу, а не по профилю.
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
    const double extentX = bounds.xMaxMm - bounds.xMinMm;
    const double extentY = bounds.yMaxMm - bounds.yMinMm;
    if (!(cellMm > 0.0) || !(extentX > 0.0) || !(extentY > 0.0)) {
        return;
    }
    nx_ = int(std::floor(extentX / cellMm)) + 1;
    ny_ = int(std::floor(extentY / cellMm)) + 1;
    depth_.assign(std::size_t(nx_) * std::size_t(ny_), 0.0f);
    event_overwrites_ = 0;
    if (record_events_) {
        event_mm_.assign(depth_.size(), 0.0f);   // хроника реза вместе с полем
        event_count_.assign(depth_.size(), 0);
    } else {
        event_mm_.clear();
        event_count_.clear();
    }
    event_at_ = 0.0f;
}

void StockField::recordEvents(bool on)
{
    record_events_ = on;
    if (on && event_mm_.size() != depth_.size()) {
        event_mm_.assign(depth_.size(), 0.0f);
        event_count_.assign(depth_.size(), 0);
    }
    if (!on) {
        event_mm_.clear();
        event_count_.clear();
    }
    event_at_ = 0.0f;
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
                if (!event_mm_.empty()) {
                    const std::size_t eventIdx = row + std::size_t(ix);
                    std::uint8_t& count = event_count_[eventIdx];
                    if (count < 255) {
                        ++count;
                    }
                    if (count == 2) {
                        ++event_overwrites_;
                    }
                    event_mm_[eventIdx] = event_at_;
                }
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

namespace {

// Точка минимума f(t) = z(t) + h(r(t)) на участке-дуге со смещённым центром
// (скругление нижнего угла): замкнутой формы нет, поэтому защищённый Ньютон с
// фиксированным числом шагов - ответ детерминирован, отрезок сужается вдвое при
// каждом выходе шага за скобку. Выпуклость f гарантирует единственный минимум.
double shiftedArcCritical(const StockProfilePiece& piece, double l2, double d2,
                          double tStar, double dz, double lo, double hi)
{
    // Производные f по t: g' = dz + h'(r) * dr/dt, dr/dt = l2 * u / r.
    const auto slope = [&](double t) {
        const double u = t - tStar;
        const double r = std::sqrt(l2 * u * u + d2);
        if (!(r > 1e-12)) {
            return dz;
        }
        return dz + piece.slopeAt(r) * (l2 * u / r);
    };
    const auto curvature = [&](double t) {
        const double u = t - tStar;
        const double r2 = l2 * u * u + d2;
        const double r = std::sqrt(r2);
        if (!(r > 1e-9)) {
            return 0.0;
        }
        const double q = r - piece.centerRMm;
        const double phi2 = piece.arcRadiusMm * piece.arcRadiusMm - q * q;
        if (!(phi2 > 1e-18)) {
            return 0.0;
        }
        const double phi = std::sqrt(phi2);
        const double a = l2 * u / r;                      // dr/dt
        const double b = q / phi;                         // h'(r)
        const double da = l2 * d2 / (r2 * r);             // d(dr/dt)/dt
        const double db = piece.arcRadiusMm * piece.arcRadiusMm * a / (phi2 * phi);
        return da * b + a * db;
    };
    double a = lo;
    double b = hi;
    double t = std::min(hi, std::max(lo, tStar));
    for (int i = 0; i < 40; ++i) {
        const double g = slope(t);
        if (g > 0.0) {
            b = t;
        } else {
            a = t;
        }
        const double g2 = curvature(t);
        double step = g2 > 1e-18 ? -g / g2 : 0.0;
        double next = t + step;
        if (!(next > a && next < b)) {
            next = 0.5 * (a + b);
        }
        if (std::fabs(next - t) < 1e-14) {
            t = next;
            break;
        }
        t = next;
    }
    return t;
}

} // namespace

void StockField::sweepExact(const StockCutter& cutter,
                            const std::array<double, 3>& aMm,
                            const std::array<double, 3>& bMm)
{
    const StockProfile& prof = cutter.profile;
    if (empty() || prof.empty()) {
        return;
    }
    const double radius = prof.maxRadiusMm;
    if (!(radius > 0.0)) {
        return;
    }
    const double ax = aMm[0];
    const double ay = aMm[1];
    const double az = aMm[2];
    const double dx = bMm[0] - ax;
    const double dy = bMm[1] - ay;
    const double dz = bMm[2] - az;

    // Ход целиком выше заготовки: h >= 0, значит верх материала не понижается.
    if (std::min(az, bMm[2]) >= bounds_.zMaxMm) {
        return;
    }

    const double l2 = dx * dx + dy * dy;
    const double topMm = bounds_.zMaxMm;
    const double thicknessMm = bounds_.zMaxMm - bounds_.zMinMm;

    if (!(l2 > 0.0)) {
        // Ход по вертикали: расстояние до узла не меняется, режет низ хода.
        const int ix0 = std::max(0, int(std::ceil((ax - radius - bounds_.xMinMm) / cellMm_)));
        const int ix1 = std::min(nx_ - 1, int(std::floor((ax + radius - bounds_.xMinMm) / cellMm_)));
        const int iy0 = std::max(0, int(std::ceil((ay - radius - bounds_.yMinMm) / cellMm_)));
        const int iy1 = std::min(ny_ - 1, int(std::floor((ay + radius - bounds_.yMinMm) / cellMm_)));
        const double tipZ = std::min(az, bMm[2]);
        for (int iy = iy0; iy <= iy1; ++iy) {
            const double ny = nodeYMm(iy) - ay;
            for (int ix = ix0; ix <= ix1; ++ix) {
                const double nx = nodeXMm(ix) - ax;
                const double r = std::sqrt(nx * nx + ny * ny);
                if (r > radius) {
                    continue;
                }
                double depth = topMm - (tipZ + prof.heightAt(r));
                if (!(depth > 0.0)) {
                    continue;
                }
                depth = std::min(depth, thicknessMm);
                const std::size_t idx = std::size_t(iy) * std::size_t(nx_) + std::size_t(ix);
                float& cell = depth_[idx];
                if (float(depth) > cell) {
                    cell = float(depth);
                    if (!event_mm_.empty()) {
                        std::uint8_t& count = event_count_[idx];
                        if (count < 255) {
                            ++count;
                        }
                        if (count == 2) {
                            ++event_overwrites_;
                        }
                        event_mm_[idx] = event_at_;
                    }
                }
            }
        }
        return;
    }

    // Капсула хода: отрезок плюс диск радиуса. Вне её bbox узлов нет.
    const int ix0 = std::max(0, int(std::ceil(
            (std::min(ax, bMm[0]) - radius - bounds_.xMinMm) / cellMm_)));
    const int ix1 = std::min(nx_ - 1, int(std::floor(
            (std::max(ax, bMm[0]) + radius - bounds_.xMinMm) / cellMm_)));
    const int iy0 = std::max(0, int(std::ceil(
            (std::min(ay, bMm[1]) - radius - bounds_.yMinMm) / cellMm_)));
    const int iy1 = std::min(ny_ - 1, int(std::floor(
            (std::max(ay, bMm[1]) + radius - bounds_.yMinMm) / cellMm_)));
    if (ix0 > ix1 || iy0 > iy1) {
        return;
    }

    const double rootL2 = std::sqrt(l2);
    const double rootL2dz = std::sqrt(l2 + dz * dz);   // для дуги шара: один раз на ход
    for (int iy = iy0; iy <= iy1; ++iy) {
        const double py = nodeYMm(iy) - ay;
        const std::size_t row = std::size_t(iy) * std::size_t(nx_);
        for (int ix = ix0; ix <= ix1; ++ix) {
            const double px = nodeXMm(ix) - ax;
            // Проекция узла на прямую хода: tStar; d2 - квадрат расстояния до
            // прямой. Через них r(t)^2 = l2 * (t - tStar)^2 + d2.
            const double tStar = (px * dx + py * dy) / l2;
            const double d2 = std::max(0.0, px * px + py * py - l2 * tStar * tStar);
            const double d = std::sqrt(d2);
            // Минимум H(P) по участкам. На каждом участке f(t) выпукла, поэтому
            // минимум стоит в критической точке, зажатой в под-интервал радиуса,
            // либо на его конце. h считается прямо по участку: обращение к общей
            // огибающей на каждого кандидата повторяло бы обход участков.
            const auto pieceHeight = [](const StockProfilePiece& piece, double r) {
                if (piece.kind == StockProfilePiece::Kind::Line) {
                    const double spanR = piece.r1Mm - piece.r0Mm;
                    return spanR > 0.0
                            ? piece.h0Mm + (piece.h1Mm - piece.h0Mm) * (r - piece.r0Mm) / spanR
                            : piece.h0Mm;
                }
                const double q = r - piece.centerRMm;
                const double inside = piece.arcRadiusMm * piece.arcRadiusMm - q * q;
                return inside > 0.0 ? piece.centerHMm - std::sqrt(inside) : piece.centerHMm;
            };
            const auto heightAtT = [&](const StockProfilePiece& piece, double t) {
                const double u = t - tStar;
                return az + dz * t + pieceHeight(piece, std::sqrt(l2 * u * u + d2));
            };
            double best = std::numeric_limits<double>::infinity();

            // Один участок - почти все инструменты (плоская, шар, фаска, сверло):
            // под-интервал радиуса один, и минимум берётся за одно обращение к h.
            if (prof.pieces.size() == 1) {
                const StockProfilePiece& piece = prof.pieces.front();
                const double r1sq = piece.r1Mm * piece.r1Mm;
                if (r1sq + 1e-9 < d2) {
                    continue;                  // узел дальше инструмента: не режет
                }
                const double w = r1sq > d2 ? std::sqrt((r1sq - d2) / l2) : 0.0;
                const double lo = std::max(0.0, tStar - w);
                const double hi = std::min(1.0, tStar + w);
                if (!(hi >= lo)) {
                    continue;
                }
                if (piece.kind == StockProfilePiece::Kind::Line) {
                    const double spanR = piece.r1Mm - piece.r0Mm;
                    const double slope = spanR > 0.0 ? (piece.h1Mm - piece.h0Mm) / spanR : 0.0;
                    if (!(slope > 0.0)) {
                        // Плоское острие: h постоянна на всём резе, минимум z на
                        // конце под-интервала - ни sqrt, ни обращения к профилю.
                        best = std::min(az + dz * lo, az + dz * hi) + piece.h0Mm;
                    } else {
                        double u = 0.0;
                        const double denom = slope * slope * l2 - dz * dz;
                        if (denom > 0.0 && d > 0.0) {
                            u = -dz * d / (rootL2 * std::sqrt(denom));
                        }
                        const double t = std::min(hi, std::max(lo, tStar + u));
                        best = heightAtT(piece, t);
                    }
                } else if (std::fabs(piece.centerRMm) <= RADIUS_EPS_MM
                           && piece.r0Mm <= RADIUS_EPS_MM) {
                    // Дуга с центром на оси - истинная сфера: минимум замкнут.
                    double u = 0.0;
                    const double inside = piece.arcRadiusMm * piece.arcRadiusMm - d2;
                    if (inside > 0.0) {
                        u = -dz * std::sqrt(inside) / (rootL2 * rootL2dz);
                    }
                    const double t = std::min(hi, std::max(lo, tStar + u));
                    best = heightAtT(piece, t);
                } else {
                    const double t = shiftedArcCritical(piece, l2, d2, tStar, dz, lo, hi);
                    best = heightAtT(piece, t);
                }
            } else {
                for (const StockProfilePiece& piece : prof.pieces) {
                    const double r0sq = piece.r0Mm * piece.r0Mm;
                    const double r1sq = piece.r1Mm * piece.r1Mm;
                    if (r1sq + 1e-9 < d2) {
                        continue;              // узел дальше участка: он не режет
                    }
                    const double w0 = r0sq > d2 ? std::sqrt((r0sq - d2) / l2) : 0.0;
                    const double w1 = r1sq > d2 ? std::sqrt((r1sq - d2) / l2) : 0.0;
                    // Критическая точка участка (u = t - tStar) - один раз на узел.
                    double uCritical = std::numeric_limits<double>::infinity();
                    bool shiftedArc = false;
                    if (piece.kind == StockProfilePiece::Kind::Line) {
                        const double spanR = piece.r1Mm - piece.r0Mm;
                        const double slope = spanR > 0.0 ? (piece.h1Mm - piece.h0Mm) / spanR : 0.0;
                        if (slope > 0.0 && slope * slope * l2 > dz * dz && d > 0.0) {
                            uCritical = -dz * d / (rootL2 * std::sqrt(slope * slope * l2 - dz * dz));
                        }
                    } else if (std::fabs(piece.centerRMm) <= RADIUS_EPS_MM
                               && piece.r0Mm <= RADIUS_EPS_MM) {
                        const double inside = piece.arcRadiusMm * piece.arcRadiusMm - d2;
                        if (inside > 0.0) {
                            uCritical = -dz * std::sqrt(inside)
                                    / (rootL2 * rootL2dz);
                        }
                    } else {
                        shiftedArc = true;     // скругление угла: Ньютон на интервале
                    }
                    // Под-интервалы, где r(t) лежит в [r0, r1]: tStar + [w0, w1] и
                    // tStar - [w1, w0].
                    for (int side = 0; side < 2; ++side) {
                        const double lo = std::max(0.0, tStar + (side == 0 ? w0 : -w1));
                        const double hi = std::min(1.0, tStar + (side == 0 ? w1 : -w0));
                        if (!(hi >= lo)) {
                            continue;
                        }
                        best = std::min(best, heightAtT(piece, lo));
                        if (hi > lo) {
                            best = std::min(best, heightAtT(piece, hi));
                        }
                        if (std::isfinite(uCritical)) {
                            const double t = std::min(hi, std::max(lo, tStar + uCritical));
                            best = std::min(best, heightAtT(piece, t));
                        } else if (shiftedArc) {
                            const double t = shiftedArcCritical(piece, l2, d2, tStar, dz, lo, hi);
                            best = std::min(best, heightAtT(piece, t));
                        }
                    }
                }
            }
            if (!std::isfinite(best)) {
                continue;
            }
            double depth = topMm - best;
            if (!(depth > 0.0)) {
                continue;
            }
            depth = std::min(depth, thicknessMm);
            float& cell = depth_[row + std::size_t(ix)];
            if (float(depth) > cell) {
                cell = float(depth);
                if (!event_mm_.empty()) {
                    const std::size_t eventIdx = row + std::size_t(ix);
                    std::uint8_t& count = event_count_[eventIdx];
                    if (count < 255) {
                        ++count;
                    }
                    if (count == 2) {
                        ++event_overwrites_;
                    }
                    event_mm_[eventIdx] = event_at_;
                }
            }
        }
    }
}

namespace {

// Глубина поля в точке, мм: билинейно по узлам клетки. Кромка прижата к крайним
// узлам, как в рисовании.
double bilinearDepth(const std::vector<float>& depth, int countX, int countY,
                     double xMm, double yMm, const StockBounds& bounds, double cellMm)
{
    const double px = (xMm - bounds.xMinMm) / cellMm;
    const double py = (yMm - bounds.yMinMm) / cellMm;
    const int ix0 = std::max(0, std::min(countX - 1, int(std::floor(px))));
    const int iy0 = std::max(0, std::min(countY - 1, int(std::floor(py))));
    const int ix1 = std::min(countX - 1, ix0 + 1);
    const int iy1 = std::min(countY - 1, iy0 + 1);
    const double fx = std::max(0.0, std::min(1.0, px - double(ix0)));
    const double fy = std::max(0.0, std::min(1.0, py - double(iy0)));
    const auto at = [&](int ix, int iy) {
        return double(depth[std::size_t(iy) * std::size_t(countX) + std::size_t(ix)]);
    };
    const double a = at(ix0, iy0) * (1.0 - fx) + at(ix1, iy0) * fx;
    const double b = at(ix0, iy1) * (1.0 - fx) + at(ix1, iy1) * fx;
    return a * (1.0 - fy) + b * fy;
}

} // namespace

std::vector<StockContourSegment> stockCutContour(const StockBounds& bounds, double cellMm,
                                                 const std::vector<float>& depthMm,
                                                 int countX, int countY, double cutMm)
{
    std::vector<StockContourSegment> out;
    if (countX < 2 || countY < 2 || !(cellMm > 0.0)
            || depthMm.size() < std::size_t(countX) * std::size_t(countY)) {
        return out;
    }
    const auto at = [&](int ix, int iy) {
        return double(depthMm[std::size_t(iy) * std::size_t(countX) + std::size_t(ix)]);
    };
    for (int iy = 0; iy + 1 < countY; ++iy) {
        for (int ix = 0; ix + 1 < countX; ++ix) {
            const double x0 = bounds.xMinMm + double(ix) * cellMm;
            const double x1 = x0 + cellMm;
            const double y0 = bounds.yMinMm + double(iy) * cellMm;
            const double y1 = y0 + cellMm;
            // Углы 0..3: 0 - (x0, y0), 1 - (x1, y0), 2 - (x1, y1), 3 - (x0, y1).
            const double f[4] = {at(ix, iy) - cutMm, at(ix + 1, iy) - cutMm,
                                 at(ix + 1, iy + 1) - cutMm, at(ix, iy + 1) - cutMm};
            const double cx[4] = {x0, x1, x1, x0};
            const double cy[4] = {y0, y0, y1, y1};
            // Рёбра 0..3: низ, право, верх, лево. Переход через порог даёт точку
            // на ребре, положение - доля линейной интерполяции.
            double px[4];
            double py[4];
            int n = 0;
            for (int e = 0; e < 4; ++e) {
                const int a = e;
                const int b = (e + 1) & 3;
                if ((f[a] < 0.0) == (f[b] < 0.0)) {
                    continue;
                }
                const double t = f[a] / (f[a] - f[b]);
                px[n] = cx[a] + (cx[b] - cx[a]) * t;
                py[n] = cy[a] + (cy[b] - cy[a]) * t;
                ++n;
            }
            if (n == 2) {
                out.push_back(StockContourSegment{px[0], py[0], px[1], py[1]});
            } else if (n == 4) {
                // Седло: пара отрезков разделяет углы одного знака. Знак среднего
                // по клетке решает, какая пара - детерминированно.
                const double center = 0.25 * (f[0] + f[1] + f[2] + f[3]);
                if (center < 0.0) {
                    out.push_back(StockContourSegment{px[0], py[0], px[1], py[1]});
                    out.push_back(StockContourSegment{px[2], py[2], px[3], py[3]});
                } else {
                    out.push_back(StockContourSegment{px[0], py[0], px[3], py[3]});
                    out.push_back(StockContourSegment{px[1], py[1], px[2], py[2]});
                }
            }
        }
    }
    return out;
}

StockWallMesh stockCutWall(const StockBounds& bounds, double cellMm,
                           const std::vector<float>& depthMm, int countX, int countY,
                           double cutMm)
{
    StockWallMesh mesh;
    const std::vector<StockContourSegment> contour =
            stockCutContour(bounds, cellMm, depthMm, countX, countY, cutMm);
    if (contour.empty()) {
        return mesh;
    }
    const float zTop = float(bounds.zMaxMm);
    mesh.vertices.reserve(contour.size() * 4 * 4);
    mesh.indices.reserve(contour.size() * 6);
    for (const StockContourSegment& segment : contour) {
        const double d0 = bilinearDepth(depthMm, countX, countY,
                                        segment.x0Mm, segment.y0Mm, bounds, cellMm);
        const double d1 = bilinearDepth(depthMm, countX, countY,
                                        segment.x1Mm, segment.y1Mm, bounds, cellMm);
        // Низ на дне реза, верх на плоскости нетронутого верха: стык верх/дно
        // идёт по одному контуру, щели нет.
        const std::uint32_t base = std::uint32_t(mesh.vertices.size() / 4);
        const float quad[4][4] = {
            {float(segment.x0Mm), float(segment.y0Mm), float(double(zTop) - d0), float(d0)},
            {float(segment.x1Mm), float(segment.y1Mm), float(double(zTop) - d1), float(d1)},
            {float(segment.x1Mm), float(segment.y1Mm), zTop, float(d1)},
            {float(segment.x0Mm), float(segment.y0Mm), zTop, float(d0)},
        };
        for (int v = 0; v < 4; ++v) {
            for (int c = 0; c < 4; ++c) {
                mesh.vertices.push_back(quad[v][c]);
            }
        }
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 1);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 0);
        mesh.indices.push_back(base + 2);
        mesh.indices.push_back(base + 3);
    }
    return mesh;
}

StockSurfaceMesh stockSurfaceMesh(const StockBounds& bounds, double cellMm,
                                  int countX, int countY, int subdiv)
{
    StockSurfaceMesh mesh;
    if (countX < 2 || countY < 2 || !(cellMm > 0.0)) {
        return mesh;
    }
    // Число подклеток на клетку поля: 1 - прежняя сетка по узлам.
    const int k = subdiv < 1 ? 1 : subdiv;
    const int drawnX = (countX - 1) * k + 1;
    const int drawnY = (countY - 1) * k + 1;
    mesh.countX = drawnX;
    mesh.countY = drawnY;

    const float zTop = float(bounds.zMaxMm);

    mesh.vertices.reserve(std::size_t(drawnX) * std::size_t(drawnY) * 5);
    for (int jy = 0; jy < drawnY; ++jy) {
        // Координата в узлах поля: целая часть - номер узла, дробная - доля
        // клетки. При k = 1 это ровно номер узла, поэтому прежний буфер
        // воспроизводится бит в бит.
        const double ty = double(jy) / double(k);
        // Узел поля, а в последней строке - растянутый до габарита (см.
        // заголовок): иначе верх не дошёл бы до стенки коробки.
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
    // Край коробки - габарит: последний узел поля растянут до него, поэтому
    // верх и стенка сходятся. Верх материала стенка перекрывает на gapTopMm -
    // так же, как перекрывала прежняя коробка.
    const double x0 = bounds.xMinMm - gapSideMm;
    const double x1 = bounds.xMaxMm + gapSideMm;
    const double y0 = bounds.yMinMm - gapSideMm;
    const double y1 = bounds.yMaxMm + gapSideMm;
    const double z0 = bounds.zMinMm - gapBottomMm;
    const double z1 = bounds.zMaxMm + gapTopMm;

    // Четыре угла одной грани парами треугольников: 0-1-2, 0-2-3.
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

    face({x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0});   // дно
    face({x0, y0, z0}, {x0, y1, z0}, {x0, y1, z1}, {x0, y0, z1});   // стенка -X
    face({x1, y0, z0}, {x1, y0, z1}, {x1, y1, z1}, {x1, y1, z0});   // стенка +X
    face({x0, y0, z0}, {x0, y0, z1}, {x1, y0, z1}, {x1, y0, z0});   // стенка -Y
    face({x0, y1, z0}, {x1, y1, z0}, {x1, y1, z1}, {x0, y1, z1});   // стенка +Y
    return vertices;
}

bool StockWalk::start(const PathData& data, const StockBounds& bounds, double cellMm,
                      std::vector<StockCutter> cutters, bool recordEvents)
{
    data_ = nullptr;
    started_ = false;
    refused_stage_ = -1;
    refused_tool_ = 0;
    record_events_ = recordEvents;
    cutters_ = std::move(cutters);
    // Отказ считается до аллокации поля: для такого инструмента поля нет вовсе,
    // а не пустое поле.
    const int refused = stockUndercutStage(cutters_);
    if (refused >= 0) {
        refused_stage_ = refused;
        refused_tool_ = cutters_[std::size_t(refused)].number;
        return false;
    }
    const std::size_t n = data.pts.size() / 3;
    if (n < 2 || data.cum.size() != n || !(data.total > 0.0) || cutters_.empty()) {
        return false;                      // проходить нечего
    }
    data_ = &data;
    bounds_ = bounds;
    cellMm_ = cellMm;
    // Начало этапа по накопленной длине пути: тем же выражением, что у скана
    // коллизий (доля этапа умножается на полную длину).
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
    if (record_events_) {
        field_.recordEvents(true);
    }
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
    // Перемотка: поле снова целая заготовка, курсор в начало. Прежнее поле
    // живёт до этого вызова - публикует его вызывающий, а не обход.
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
            break;                         // ход кончается позже цели: не трогаем
        }
        if (cancelled && cancelled(d)) {
            advance_ms_ += msSince(t0);
            return false;
        }
        // Этап определяется длиной пути до начала хода: d не убывает, поэтому
        // указатель двигается только вперёд и переживает тики.
        const double d0 = data_->cum[i - 1];
        while (stage_ + 1 < starts_.size() && starts_[stage_ + 1] <= d0) {
            ++stage_;
        }
        // PathData хранит точки сцены (X, Z, -Y): обратно в машинные.
        const std::array<double, 3> a{data_->pts[(i - 1) * 3], -data_->pts[(i - 1) * 3 + 2],
                                      data_->pts[(i - 1) * 3 + 1]};
        const std::array<double, 3> b{data_->pts[i * 3], -data_->pts[i * 3 + 2],
                                      data_->pts[i * 3 + 1]};
        const StockCutter& cutter = cutters_[std::min(stage_, cutters_.size() - 1)];
        if (record_events_) {
            field_.setEventMm(d);          // углубление узла помечается концом хода
        }
        if (cutter.maxRadiusMm > 0.0) {
            field_.sweepExact(cutter, a, b);
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
        return out;                        // поля нет: причина в refusedStage
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
