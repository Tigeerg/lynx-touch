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

// Невязка уровня реза, мм, на которой подточка цепочки границы считается
// лежащей на уровне (stockCutBoundary, subdivPerCell): на два порядка строже
// требования 1e-6 мм (случай boundary_refined пробника stock_sim_probe).
constexpr double SUBDIV_DEPTH_EPS_MM = 1e-10;

// Допуск ПОЛОЖЕНИЯ подточки на уровне вдоль нормали клетки, мм: у гладкого среза
// невязка доводится до SUBDIV_DEPTH_EPS_MM, а у кромки плоской фрезы глубина
// обрывается ступенькой, и там уровень - не линия, а сама кромка: её положение
// доводится до этого допуска. Число на порядок строже требования 1e-6 мм случая
// boundary_refined: у гладкого шара отклонение вершины от аналитической границы
// выходит 1e-7 мм, у кромки плоской фрезы - доли 1e-6 (там "уровень" - разрыв).
constexpr double SUBDIV_POS_EPS_MM = 1e-7;

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

namespace {

// Минимум H(P) = min_t (z(t) + h(r(t))) одного хода A -> B в точке (px, py):
// общая часть StockField::sweepExact (узел сетки) и точечного съёма границы
// реза (stockCutBoundary: произвольная точка ребра клетки). px/py - смещение
// точки от начала хода, мм; az, dz, dx, dy, l2 и оба корня - из хода.
// Возвращает высоту в машинном Z, мм; +бесконечность - ни один участок не
// достаёт точки. Вынесено, чтобы узел поля и точка границы считались ОДНОЙ
// формулой (правка минимума не разъезжается между полем и границей).
double sweepNodeHeight(const StockProfile& prof, double az, double dz,
                       double dx, double dy, double l2, double rootL2, double rootL2dz,
                       double px, double py)
{
    // Проекция точки на прямую хода: tStar; d2 - квадрат расстояния до
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
            return best;                   // точка дальше инструмента: не режет
        }
        const double w = r1sq > d2 ? std::sqrt((r1sq - d2) / l2) : 0.0;
        const double lo = std::max(0.0, tStar - w);
        const double hi = std::min(1.0, tStar + w);
        if (!(hi >= lo)) {
            return best;
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
        return best;
    }

    for (const StockProfilePiece& piece : prof.pieces) {
        const double r0sq = piece.r0Mm * piece.r0Mm;
        const double r1sq = piece.r1Mm * piece.r1Mm;
        if (r1sq + 1e-9 < d2) {
            continue;                      // точка дальше участка: он не режет
        }
        const double w0 = r0sq > d2 ? std::sqrt((r0sq - d2) / l2) : 0.0;
        const double w1 = r1sq > d2 ? std::sqrt((r1sq - d2) / l2) : 0.0;
        // Критическая точка участка (u = t - tStar) - один раз на точку.
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
                uCritical = -dz * std::sqrt(inside) / (rootL2 * rootL2dz);
            }
        } else {
            shiftedArc = true;             // скругление угла: Ньютон на интервале
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
    return best;
}

// Глубина точечного съёма хода A -> B в точке (px, py), мм: та же величина,
// что хранит узел поля (глубина от верха, обрезана толщиной). Ноль - точка не
// тронута. Вертикальный ход (l2 == 0) разбирается как в StockField::sweepExact.
double sweepPointDepth(const StockProfile& prof, const std::array<double, 3>& a,
                       const std::array<double, 3>& b, double px, double py,
                       double topMm, double thicknessMm)
{
    if (prof.empty()) {
        return 0.0;
    }
    const double dx = b[0] - a[0];
    const double dy = b[1] - a[1];
    const double dz = b[2] - a[2];
    const double l2 = dx * dx + dy * dy;
    double best = std::numeric_limits<double>::infinity();
    if (!(l2 > 0.0)) {
        const double r = std::hypot(px - a[0], py - a[1]);
        if (r > prof.maxRadiusMm) {
            return 0.0;
        }
        best = std::min(a[2], b[2]) + prof.heightAt(r);
    } else {
        best = sweepNodeHeight(prof, a[2], dz, dx, dy, l2, std::sqrt(l2),
                               std::sqrt(l2 + dz * dz), px - a[0], py - a[1]);
    }
    if (!std::isfinite(best)) {
        return 0.0;
    }
    const double depth = topMm - best;
    return depth > 0.0 ? std::min(depth, thicknessMm) : 0.0;
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
            const double best = sweepNodeHeight(prof, az, dz, dx, dy, l2, rootL2,
                                                rootL2dz, px, py);
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

namespace {

// Расстояние от точки до отрезка в XY, мм.
double pointSegmentDistanceMm(double px, double py, double ax, double ay,
                              double bx, double by)
{
    const double dx = bx - ax;
    const double dy = by - ay;
    const double l2 = dx * dx + dy * dy;
    double t = 0.0;
    if (l2 > 0.0) {
        t = std::min(1.0, std::max(0.0, ((px - ax) * dx + (py - ay) * dy) / l2));
    }
    return std::hypot(px - (ax + dx * t), py - (ay + dy * t));
}

} // namespace

StockBoundaryResult stockCutBoundary(const PathData& data, const StockBounds& bounds,
                                     double cellMm, const std::vector<StockCutter>& cutters,
                                     const std::vector<float>& depthMm, int countX, int countY,
                                     double cutMm, StockBoundaryCache* cache, int refineSteps,
                                     int subdivPerCell)
{
    StockBoundaryResult out;
    const std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    if (countX < 2 || countY < 2 || !(cellMm > 0.0) || cutters.empty()
            || depthMm.size() < std::size_t(countX) * std::size_t(countY)) {
        return out;
    }
    const double thicknessMm = bounds.zMaxMm - bounds.zMinMm;
    const std::size_t n = data.pts.size() / 3;
    if (n < 2 || !(thicknessMm > 0.0)) {
        return out;
    }
    const int cellsX = countX - 1;
    const int cellsY = countY - 1;
    const std::size_t cellCount = std::size_t(cellsX) * std::size_t(cellsY);

    // Состояние: кэш вызывающего (живой тик) или местный (сборка без состояния).
    StockBoundaryCache local;
    StockBoundaryCache& state = (cache != nullptr) ? *cache : local;
    const bool reuse = cache != nullptr
            && state.countX == countX && state.countY == countY && state.cellMm == cellMm
            && state.bounds.xMinMm == bounds.xMinMm && state.bounds.xMaxMm == bounds.xMaxMm
            && state.bounds.yMinMm == bounds.yMinMm && state.bounds.yMaxMm == bounds.yMaxMm
            && state.bounds.zMinMm == bounds.zMinMm && state.bounds.zMaxMm == bounds.zMaxMm
            && state.cellSegments.size() == cellCount
            && state.depth.size() == depthMm.size();

    if (!reuse) {
        // Ходы в машинных координатах и номер инструмента этапа - тем же
        // правилом, что StockWalk::advanceTo: этап определяется длиной пути до
        // начала хода.
        state.bounds = bounds;
        state.countX = countX;
        state.countY = countY;
        state.cellMm = cellMm;
        state.moveA.clear();
        state.moveB.clear();
        state.moveCutter.clear();
        state.moveA.reserve(n - 1);
        state.moveB.reserve(n - 1);
        state.moveCutter.reserve(n - 1);
        std::vector<double> starts;
        starts.reserve(data.stages.size());
        for (const StageRange& stage : data.stages) {
            starts.push_back(stage.start * data.total);
        }
        std::size_t stage = 0;
        for (std::size_t i = 1; i < n; ++i) {
            const double d0 = data.cum[i - 1];
            while (stage + 1 < starts.size() && starts[stage + 1] <= d0) {
                ++stage;
            }
            const int ci = int(std::min(stage, cutters.size() - 1));
            if (!(cutters[std::size_t(ci)].maxRadiusMm > 0.0)) {
                continue;
            }
            const std::array<double, 3> a{data.pts[(i - 1) * 3], -data.pts[(i - 1) * 3 + 2],
                                          data.pts[(i - 1) * 3 + 1]};
            const std::array<double, 3> b{data.pts[i * 3], -data.pts[i * 3 + 2],
                                          data.pts[i * 3 + 1]};
            // Ход целиком выше заготовки не режет: как в StockField::sweepExact.
            if (std::min(a[2], b[2]) >= bounds.zMaxMm) {
                continue;
            }
            state.moveA.push_back(a);
            state.moveB.push_back(b);
            state.moveCutter.push_back(ci);
        }
        if (state.moveA.empty()) {
            return out;
        }

        // Раскладка ходов по плиткам: точка перебирает ходы только своей плитки,
        // а плитка получает каждый ход, чья капсула её пересекает, поэтому
        // выборка полна (капсула хода - bbox отрезка плюс радиус инструмента).
        // Плитка в две клетки: замер на Test_model (узел 0.5) - 8 клеток дали
        // 45.2 млн обращений к глубине и 391 мс, 2 клетки - 15.6 млн и 160 мс.
        constexpr int TILE_CELLS = 2;
        state.tileMm = double(TILE_CELLS) * cellMm;
        state.tileCountX = std::max(1, int(std::ceil(
                (bounds.xMaxMm - bounds.xMinMm) / state.tileMm)));
        state.tileCountY = std::max(1, int(std::ceil(
                (bounds.yMaxMm - bounds.yMinMm) / state.tileMm)));
        const int tx = state.tileCountX;
        const int ty = state.tileCountY;
        const double tileMm = state.tileMm;
        state.buckets.assign(std::size_t(tx) * std::size_t(ty), {});
        for (std::size_t mi = 0; mi < state.moveA.size(); ++mi) {
            const double radius = cutters[std::size_t(state.moveCutter[mi])].maxRadiusMm;
            const double x0 = std::min(state.moveA[mi][0], state.moveB[mi][0]) - radius;
            const double x1 = std::max(state.moveA[mi][0], state.moveB[mi][0]) + radius;
            const double y0 = std::min(state.moveA[mi][1], state.moveB[mi][1]) - radius;
            const double y1 = std::max(state.moveA[mi][1], state.moveB[mi][1]) + radius;
            const int i0 = std::max(0, int(std::floor((x0 - bounds.xMinMm) / tileMm)));
            const int i1 = std::min(tx - 1, int(std::floor((x1 - bounds.xMinMm) / tileMm)));
            const int j0 = std::max(0, int(std::floor((y0 - bounds.yMinMm) / tileMm)));
            const int j1 = std::min(ty - 1, int(std::floor((y1 - bounds.yMinMm) / tileMm)));
            for (int jy = j0; jy <= j1; ++jy) {
                for (int ix = i0; ix <= i1; ++ix) {
                    state.buckets[std::size_t(jy) * std::size_t(tx) + std::size_t(ix)]
                            .push_back(int(mi));
                }
            }
        }
        state.cellSegments.assign(cellCount, {});
    }

    const int tx = state.tileCountX;
    const int ty = state.tileCountY;
    const double tileMm = state.tileMm;
    const std::vector<std::array<double, 3>>& moveA = state.moveA;
    const std::vector<std::array<double, 3>>& moveB = state.moveB;
    const std::vector<int>& moveCutter = state.moveCutter;
    const std::vector<std::vector<int>>& buckets = state.buckets;

    // Точная глубина в произвольной точке: перебор ходов своей плитки, каждый -
    // той же формулой, что узел поля (sweepPointDepth).
    const auto depthAt = [&](double xMm, double yMm) {
        const int ix = std::min(tx - 1, std::max(0, int(std::floor((xMm - bounds.xMinMm) / tileMm))));
        const int iy = std::min(ty - 1, std::max(0, int(std::floor((yMm - bounds.yMinMm) / tileMm))));
        double depth = 0.0;
        for (int mi : buckets[std::size_t(iy) * std::size_t(tx) + std::size_t(ix)]) {
            const double d = sweepPointDepth(cutters[std::size_t(moveCutter[std::size_t(mi)])].profile,
                                             moveA[std::size_t(mi)], moveB[std::size_t(mi)],
                                             xMm, yMm, bounds.zMaxMm, thicknessMm);
            if (d > depth) {
                depth = d;
            }
            ++out.evaluations;
        }
        return depth;
    };

    const int steps = std::max(1, refineSteps);
    const int subdiv = std::max(1, subdivPerCell);
    const auto at = [&](int ix, int iy) {
        return double(depthMm[std::size_t(iy) * std::size_t(countX) + std::size_t(ix)]);
    };
    // Точка цепочки границы: хорду между переходами цепочка проходит по УРОВНЮ
    // реза, а не по прямой. Уровень - линия, которую поле само подразумевает
    // между узлами: подточка берётся долей вдоль хорды и спускается на уровень по
    // ТОЧНОЙ глубине хода. Направление спуска - градиент билинейной реконструкции
    // поля в самой подточке: он перпендикулярен уровню, поэтому спуск трансверсален
    // и не длиннее клетки (у клетки, где хорда перескакивает через нетронутый
    // участок, нормаль хорды увела бы спуск вдоль уровня). Где реконструкция
    // плоская (градиент нулевой), берётся нормаль хорды. Скобка уровня ищется
    // шагами в долю клетки, а положение в ней уточняется хордой (метод Illinois):
    // у гладкого среза хорда сходится за считанные шаги, а у кромки плоской фрезы
    // глубина обрывается ступенькой, и там половинение удержанного конца
    // оставляет деление пополам. Ложь возврата - уровня рядом не нашлось.
    const auto projectToCut = [&](double bx, double by, double nx, double ny,
                                  double& qx, double& qy) {
        qx = bx;
        qy = by;
        const double f = depthAt(bx, by) - cutMm;
        if (std::fabs(f) <= SUBDIV_DEPTH_EPS_MM) {
            return true;
        }
        // Скобка: шаги долей клетки в сторону, где невязка меняет знак.
        const double dir = f > 0.0 ? -1.0 : 1.0;
        const double stepMm = cellMm * 0.25;
        double sBack = 0.0;
        double fBack = f;
        double sFore = 0.0;
        double fFore = f;
        bool bracketed = false;
        for (int k = 1; k <= 4; ++k) {
            const double s = dir * stepMm * double(k);
            const double ft = depthAt(bx + nx * s, by + ny * s) - cutMm;
            if (std::fabs(ft) <= SUBDIV_DEPTH_EPS_MM) {
                qx = bx + nx * s;
                qy = by + ny * s;
                return true;
            }
            if ((ft < 0.0) != (f < 0.0)) {
                sFore = s;
                fFore = ft;
                bracketed = true;
                break;
            }
            sBack = s;
            fBack = ft;
        }
        if (!bracketed) {
            return false;                  // уровня рядом нет: остаётся хорда
        }
        // Скобка: sBack и sFore, невязки на них - разных знаков. Уточнение -
        // хордой (Illinois), а половинение удержанного конца оставляет деление
        // пополам у ступеньки.
        double sLo = std::min(sBack, sFore);
        double sHi = std::max(sBack, sFore);
        double fLo = sBack <= sFore ? fBack : fFore;
        double fHi = sBack <= sFore ? fFore : fBack;
        double sLast = sBack;
        int sameSide = 0;
        for (int it = 0; it < 30; ++it) {
            if (sHi - sLo <= SUBDIV_POS_EPS_MM) {
                break;                         // положение уровня уже точнее допуска
            }
            double sNext = (fLo * sHi - fHi * sLo) / (fLo - fHi);
            if (!(sNext > sLo && sNext < sHi)) {
                sNext = 0.5 * (sLo + sHi);
            }
            const double fNext = depthAt(bx + nx * sNext, by + ny * sNext) - cutMm;
            sLast = sNext;
            if (std::fabs(fNext) <= SUBDIV_DEPTH_EPS_MM) {
                break;
            }
            if ((fNext < 0.0) == (fLo < 0.0)) {
                sLo = sNext;
                fLo = fNext;
                if (sameSide < 0) {
                    fHi *= 0.5;                 // Illinois: удержанная сторона тает
                }
                sameSide = -1;
            } else {
                sHi = sNext;
                if (sameSide > 0) {
                    fLo *= 0.5;
                }
                sameSide = 1;
            }
        }
        qx = bx + nx * sLast;
        qy = by + ny * sLast;
        return true;
    };
    // Цепочка одной клетки: переходы A и B на её рёбрах и subdiv - 1 подточек
    // между ними на уровне реза. При subdiv = 1 цепочка - прежняя хорда.
    // ix/iy - клетка (по её узлам считается градиент поля), unprojected -
    // счётчик подточек, которым уровня рядом не нашлось.
    const auto emitChain = [&](double ax, double ay, double bx, double by, int ix, int iy,
                               std::vector<StockContourSegment>& segments, int& unprojected) {
        if (subdiv <= 1) {
            segments.push_back(StockContourSegment{ax, ay, bx, by});
            return;
        }
        const double dx = bx - ax;
        const double dy = by - ay;
        const double len = std::hypot(dx, dy);
        if (!(len > 0.0)) {
            segments.push_back(StockContourSegment{ax, ay, bx, by});
            return;
        }
        // Нормаль хорды (сторона, где билинейная реконструкция глубже) - запасное
        // направление спуска: им обходятся клетки с плоской реконструкцией.
        double fallbackX = -dy / len;
        double fallbackY = dx / len;
        const double probe = cellMm * 0.25;
        const double plus = bilinearDepth(depthMm, countX, countY,
                                          ax + fallbackX * probe, ay + fallbackY * probe,
                                          bounds, cellMm);
        const double minus = bilinearDepth(depthMm, countX, countY,
                                           ax - fallbackX * probe, ay - fallbackY * probe,
                                           bounds, cellMm);
        if (minus > plus) {
            fallbackX = -fallbackX;
            fallbackY = -fallbackY;
        }
        // Градиент билинейной реконструкции поля в клетке, мм глубины на мм хода.
        const double d0 = at(ix, iy);
        const double d1 = at(ix + 1, iy);
        const double d2 = at(ix + 1, iy + 1);
        const double d3 = at(ix, iy + 1);
        const double cellX = bounds.xMinMm + double(ix) * cellMm;
        const double cellY = bounds.yMinMm + double(iy) * cellMm;
        double prevX = ax;
        double prevY = ay;
        for (int k = 1; k < subdiv; ++k) {
            const double t = double(k) / double(subdiv);
            const double baseX = ax + dx * t;
            const double baseY = ay + dy * t;
            const double fx = (baseX - cellX) / cellMm;
            const double fy = (baseY - cellY) / cellMm;
            double gx = ((d1 - d0) * (1.0 - fy) + (d2 - d3) * fy) / cellMm;
            double gy = ((d3 - d0) * (1.0 - fx) + (d2 - d1) * fx) / cellMm;
            const double glen = std::hypot(gx, gy);
            double nx = fallbackX;
            double ny = fallbackY;
            if (glen > 1e-9) {
                nx = gx / glen;
                ny = gy / glen;
            }
            double qx = 0.0;
            double qy = 0.0;
            if (!projectToCut(baseX, baseY, nx, ny, qx, qy)) {
                ++unprojected;
            }
            segments.push_back(StockContourSegment{prevX, prevY, qx, qy});
            prevX = qx;
            prevY = qy;
        }
        segments.push_back(StockContourSegment{prevX, prevY, bx, by});
    };
    // Отрезки одной клетки: marching squares по знакам узлов поля, положение
    // перехода на ребре уточняется делением отрезка по ТОЧНОЙ аналитической
    // глубине. Знак на концах ребра взят из поля (в узле глубина точна), поэтому
    // скобка верна, а узел задаёт лишь выбор ребра - ошибка от узла не зависит.
    // Цепочка между переходами дробится на уровне реза (emitChain).
    const auto computeCell = [&](int ix, int iy, std::vector<StockContourSegment>& segments,
                                 int& crossingEdges) {
        segments.clear();
        crossingEdges = 0;
        const double x0 = bounds.xMinMm + double(ix) * cellMm;
        const double x1 = x0 + cellMm;
        const double y0 = bounds.yMinMm + double(iy) * cellMm;
        const double y1 = y0 + cellMm;
        // Углы 0..3: 0 - (x0, y0), 1 - (x1, y0), 2 - (x1, y1), 3 - (x0, y1).
        const double f[4] = {at(ix, iy) - cutMm, at(ix + 1, iy) - cutMm,
                             at(ix + 1, iy + 1) - cutMm, at(ix, iy + 1) - cutMm};
        const double cx[4] = {x0, x1, x1, x0};
        const double cy[4] = {y0, y0, y1, y1};
        double px[4];
        double py[4];
        int count = 0;
        for (int e = 0; e < 4; ++e) {
            const int a = e;
            const int b = (e + 1) & 3;
            if ((f[a] < 0.0) == (f[b] < 0.0)) {
                continue;
            }
            const bool cutA = !(f[a] < 0.0);
            double lo = 0.0;
            double hi = 1.0;
            for (int s = 0; s < steps; ++s) {
                const double mid = 0.5 * (lo + hi);
                const double dm = depthAt(cx[a] + (cx[b] - cx[a]) * mid,
                                          cy[a] + (cy[b] - cy[a]) * mid);
                if ((!(dm < cutMm)) == cutA) {
                    lo = mid;
                } else {
                    hi = mid;
                }
            }
            const double t = 0.5 * (lo + hi);
            px[count] = cx[a] + (cx[b] - cx[a]) * t;
            py[count] = cy[a] + (cy[b] - cy[a]) * t;
            ++count;
        }
        if (count == 2) {
            emitChain(px[0], py[0], px[1], py[1], ix, iy, segments, out.unprojected);
            crossingEdges = 2;
        } else if (count == 4) {
            // Седло: знак среднего по клетке решает, какая пара отрезков - то же
            // правило, что у stockCutContour (детерминизм).
            const double center = 0.25 * (f[0] + f[1] + f[2] + f[3]);
            if (center < 0.0) {
                emitChain(px[0], py[0], px[1], py[1], ix, iy, segments, out.unprojected);
                emitChain(px[2], py[2], px[3], py[3], ix, iy, segments, out.unprojected);
            } else {
                emitChain(px[0], py[0], px[3], py[3], ix, iy, segments, out.unprojected);
                emitChain(px[1], py[1], px[2], py[2], ix, iy, segments, out.unprojected);
            }
            crossingEdges = 4;
        }
    };

    // Какие клетки считать: при первой сборке все, дальше - только те, чьи узлы
    // изменились (съём монотонен: глубина узла только растёт). Цена тика
    // пропорциональна новому участку границы, а не всей границе.
    std::vector<int> dirty;
    if (!reuse) {
        dirty.reserve(cellCount);
        for (std::size_t cell = 0; cell < cellCount; ++cell) {
            dirty.push_back(int(cell));
        }
    } else {
        for (int iy = 0; iy < countY; ++iy) {
            for (int ix = 0; ix < countX; ++ix) {
                const std::size_t idx = std::size_t(iy) * std::size_t(countX) + std::size_t(ix);
                if (depthMm[idx] == state.depth[idx]) {
                    continue;
                }
                // Узел (ix, iy) входит в клетки (ix-1..ix, iy-1..iy).
                for (int cy = iy - 1; cy <= iy; ++cy) {
                    for (int cx = ix - 1; cx <= ix; ++cx) {
                        if (cx < 0 || cy < 0 || cx >= cellsX || cy >= cellsY) {
                            continue;
                        }
                        dirty.push_back(cy * cellsX + cx);
                    }
                }
            }
        }
        std::sort(dirty.begin(), dirty.end());
        dirty.erase(std::unique(dirty.begin(), dirty.end()), dirty.end());
    }

    std::vector<StockContourSegment> segments;
    for (int cell : dirty) {
        int crossingEdges = 0;
        computeCell(cell % cellsX, cell / cellsX, segments, crossingEdges);
        state.cellSegments[std::size_t(cell)] = segments;
        out.crossingEdges += crossingEdges;
    }
    // Плоский список отрезков: порядок клеток фиксирован, значит детерминирован.
    for (const std::vector<StockContourSegment>& cell : state.cellSegments) {
        out.segments.insert(out.segments.end(), cell.begin(), cell.end());
    }
    state.depth = depthMm;                     // поле на момент сборки
    out.buildMs = msSince(t0);
    return out;
}

std::vector<float> stockCutSignedDistance(const StockBounds& bounds, double cellMm,
                                          const std::vector<float>& depthMm,
                                          int countX, int countY,
                                          const std::vector<StockContourSegment>& segments,
                                          double cutMm, double radiusMm)
{
    std::vector<float> out;
    if (countX < 2 || countY < 2 || !(cellMm > 0.0) || !(radiusMm > 0.0)
            || depthMm.size() < std::size_t(countX) * std::size_t(countY)) {
        return out;
    }
    out.assign(std::size_t(countX) * std::size_t(countY), float(radiusMm));
    for (int iy = 0; iy < countY; ++iy) {
        for (int ix = 0; ix < countX; ++ix) {
            const std::size_t idx = std::size_t(iy) * std::size_t(countX) + std::size_t(ix);
            // Знак - по глубине узла (в узле она точна): снято - отрицательно.
            if (double(depthMm[idx]) > cutMm) {
                out[idx] = float(-radiusMm);
            }
        }
    }
    for (const StockContourSegment& segment : segments) {
        const double x0 = std::min(segment.x0Mm, segment.x1Mm) - radiusMm;
        const double x1 = std::max(segment.x0Mm, segment.x1Mm) + radiusMm;
        const double y0 = std::min(segment.y0Mm, segment.y1Mm) - radiusMm;
        const double y1 = std::max(segment.y0Mm, segment.y1Mm) + radiusMm;
        const int ix0 = std::max(0, int(std::ceil((x0 - bounds.xMinMm) / cellMm)));
        const int ix1 = std::min(countX - 1, int(std::floor((x1 - bounds.xMinMm) / cellMm)));
        const int iy0 = std::max(0, int(std::ceil((y0 - bounds.yMinMm) / cellMm)));
        const int iy1 = std::min(countY - 1, int(std::floor((y1 - bounds.yMinMm) / cellMm)));
        for (int iy = iy0; iy <= iy1; ++iy) {
            const double py = bounds.yMinMm + double(iy) * cellMm;
            for (int ix = ix0; ix <= ix1; ++ix) {
                const double px = bounds.xMinMm + double(ix) * cellMm;
                const double dist = pointSegmentDistanceMm(px, py, segment.x0Mm, segment.y0Mm,
                                                           segment.x1Mm, segment.y1Mm);
                const std::size_t idx = std::size_t(iy) * std::size_t(countX) + std::size_t(ix);
                if (dist < std::fabs(double(out[idx]))) {
                    out[idx] = float(out[idx] < 0.0f ? -dist : dist);
                }
            }
        }
    }
    return out;
}

std::vector<float> stockCutChainDistance(const StockBounds& bounds, double cellMm,
                                         const std::vector<float>& signedMm,
                                         int countX, int countY,
                                         const std::vector<StockContourSegment>& segments,
                                         double plateauMm, int refine,
                                         int* outCountX, int* outCountY)
{
    if (outCountX != nullptr) {
        *outCountX = 0;
    }
    if (outCountY != nullptr) {
        *outCountY = 0;
    }
    if (countX < 2 || countY < 2 || !(cellMm > 0.0) || !(plateauMm > 0.0)
            || refine < 1 || signedMm.size() < std::size_t(countX) * std::size_t(countY)) {
        return {};
    }
    const double h = cellMm / double(refine);
    const int nfx = (countX - 1) * refine + 1;
    const int nfy = (countY - 1) * refine + 1;
    if (outCountX != nullptr) {
        *outCountX = nfx;
    }
    if (outCountY != nullptr) {
        *outCountY = nfy;
    }
    std::vector<float> out(std::size_t(nfx) * std::size_t(nfy), 0.0f);
    // Сторона точки - билинейно из поля знакового расстояния: у поля нулевой
    // уровень уже назван стороной цепочки, поэтому знак узла мелкой сетки
    // согласован с цепочкой. Магнитуда пока - плато.
    for (int jy = 0; jy < nfy; ++jy) {
        const double py = bounds.yMinMm + double(jy) * h;
        const double qy = (py - bounds.yMinMm) / cellMm;
        const int iy0 = std::max(0, std::min(countY - 2, int(std::floor(qy))));
        const double fy = std::max(0.0, std::min(1.0, qy - double(iy0)));
        for (int jx = 0; jx < nfx; ++jx) {
            const double px = bounds.xMinMm + double(jx) * h;
            const double qx = (px - bounds.xMinMm) / cellMm;
            const int ix0 = std::max(0, std::min(countX - 2, int(std::floor(qx))));
            const double fx = std::max(0.0, std::min(1.0, qx - double(ix0)));
            const auto at = [&](int ix, int iy) {
                return double(signedMm[std::size_t(iy) * std::size_t(countX)
                                       + std::size_t(ix)]);
            };
            const double a = at(ix0, iy0) * (1.0 - fx) + at(ix0 + 1, iy0) * fx;
            const double b = at(ix0, iy0 + 1) * (1.0 - fx) + at(ix0 + 1, iy0 + 1) * fx;
            const double s = a * (1.0 - fy) + b * fy;
            out[std::size_t(jy) * std::size_t(nfx) + std::size_t(jx)] =
                    float(s < 0.0 ? -plateauMm : plateauMm);
        }
    }
    // Точное расстояние до цепочки в пределах плато: каждый узел окрестности
    // звена берёт меньшую по модулю магнитуду со своим знаком.
    const double rad = plateauMm;
    for (const StockContourSegment& segment : segments) {
        const double x0 = std::min(segment.x0Mm, segment.x1Mm) - rad;
        const double x1 = std::max(segment.x0Mm, segment.x1Mm) + rad;
        const double y0 = std::min(segment.y0Mm, segment.y1Mm) - rad;
        const double y1 = std::max(segment.y0Mm, segment.y1Mm) + rad;
        const int jx0 = std::max(0, int(std::ceil((x0 - bounds.xMinMm) / h)));
        const int jx1 = std::min(nfx - 1, int(std::floor((x1 - bounds.xMinMm) / h)));
        const int jy0 = std::max(0, int(std::ceil((y0 - bounds.yMinMm) / h)));
        const int jy1 = std::min(nfy - 1, int(std::floor((y1 - bounds.yMinMm) / h)));
        for (int jy = jy0; jy <= jy1; ++jy) {
            const double py = bounds.yMinMm + double(jy) * h;
            for (int jx = jx0; jx <= jx1; ++jx) {
                const double px = bounds.xMinMm + double(jx) * h;
                const double dist = pointSegmentDistanceMm(px, py, segment.x0Mm, segment.y0Mm,
                                                           segment.x1Mm, segment.y1Mm);
                const std::size_t idx = std::size_t(jy) * std::size_t(nfx) + std::size_t(jx);
                if (dist < std::fabs(double(out[idx]))) {
                    out[idx] = float(out[idx] < 0.0f ? -dist : dist);
                }
            }
        }
    }
    return out;
}

StockWallMesh stockCutWallFromSegments(const StockBounds& bounds, double cellMm,
                                       const std::vector<float>& depthMm, int countX, int countY,
                                       const std::vector<StockContourSegment>& segments)
{
    StockWallMesh mesh;
    if (segments.empty()) {
        return mesh;
    }
    const float zTop = float(bounds.zMaxMm);
    mesh.vertices.reserve(segments.size() * 4 * 4);
    mesh.indices.reserve(segments.size() * 6);
    for (const StockContourSegment& segment : segments) {
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

namespace {

// Порог уступа в долях клетки: то же число, что STEP_CELLS в stocksurface.vert
// (разбор выбора - там). Им правило шейдера отличает настоящий уступ от склона и
// от канавки шириной в клетку.
constexpr double kStepCells = 0.5;

// Глубина по правилу шейдера (stocksurface.vert, steppedDepth) со стороной,
// названной половиной: это высота, которой половина опускает свою вершину. У
// настоящего уступа (клетка-кинк) половина стоит на своём уровне; у склона и
// канавки правило билинейно. Копия правила в ядре нужна стенке: её низ обязан
// лечь на кромку дна, а не на билинейную глубину поля в точке контура.
double layerRuleDepth(const std::vector<float>& depthMm, int countX, int countY,
                      const StockBounds& bounds, double cellMm, double cutMm,
                      double xMm, double yMm, StockLayer layer)
{
    const auto at = [&](int ix, int iy) {
        ix = std::max(0, std::min(countX - 1, ix));
        iy = std::max(0, std::min(countY - 1, iy));
        return double(depthMm[std::size_t(iy) * std::size_t(countX) + std::size_t(ix)]);
    };
    // Клетка точки: прижата к крайним узлам, как прижимает выборку текселя шейдер.
    const double px = (xMm - bounds.xMinMm) / cellMm;
    const double py = (yMm - bounds.yMinMm) / cellMm;
    const int ix0 = std::max(0, std::min(countX - 2, int(std::floor(px))));
    const int iy0 = std::max(0, std::min(countY - 2, int(std::floor(py))));
    const double fx = std::max(0.0, std::min(1.0, px - double(ix0)));
    const double fy = std::max(0.0, std::min(1.0, py - double(iy0)));
    const double d00 = at(ix0, iy0);
    const double d10 = at(ix0 + 1, iy0);
    const double d01 = at(ix0, iy0 + 1);
    const double d11 = at(ix0 + 1, iy0 + 1);
    const double depth = (d00 * (1.0 - fx) + d10 * fx) * (1.0 - fy)
                       + (d01 * (1.0 - fx) + d11 * fx) * fy;
    const double shallow = std::min(std::min(d00, d10), std::min(d01, d11));
    const double deep = std::max(std::max(d00, d10), std::max(d01, d11));
    const double rise = deep - shallow;
    if (rise <= cellMm * kStepCells) {
        return depth;                        // размаха на уступ нет
    }
    // Главная ось уступа и площадки за клеткой по обе её стороны.
    const bool alongX = std::fabs(d10 - d00) + std::fabs(d11 - d01)
            >= std::fabs(d01 - d00) + std::fabs(d11 - d10);
    const int ax = alongX ? 1 : 0;
    const int ay = alongX ? 0 : 1;
    const int sx = alongX ? 0 : 1;
    const int sy = alongX ? 1 : 0;
    const double aheadA = at(ix0 + 2 * ax, iy0 + 2 * ay);
    const double aheadB = at(ix0 + 2 * ax + sx, iy0 + 2 * ay + sy);
    const double behindA = at(ix0 - ax, iy0 - ay);
    const double behindB = at(ix0 - ax + sx, iy0 - ay + sy);
    const double tol = 0.25 * rise;
    const auto atLevel = [&](double value) {
        return std::min(std::fabs(value - shallow), std::fabs(value - deep)) <= tol;
    };
    bool kink = atLevel(aheadA) && atLevel(aheadB) && atLevel(behindA) && atLevel(behindB);
    kink = kink && std::fabs(0.5 * (aheadA + aheadB) - 0.5 * (behindA + behindB))
                        >= 0.75 * rise;
    if (!kink) {
        return depth;                        // склон или канавка в клетку
    }
    const double mid = 0.5 * (shallow + deep);
    const bool split = ((d00 > cutMm) == (d00 > mid)) && ((d10 > cutMm) == (d10 > mid))
            && ((d01 > cutMm) == (d01 > mid)) && ((d11 > cutMm) == (d11 > mid));
    if (!split) {
        return depth > mid ? deep : shallow; // уступ без границы: ближний уровень
    }
    return layer == StockLayer::Floor ? deep : shallow;
}

// Низ стенки на кромке дна в точке контура: у уступа - по СДВИНУТОЙ внутрь дна
// точке (там правило ступает, и уровень выбирает сторона), у склона - по самой
// точке контура (правило билинейно, и кромка дна там не сдвигается: сдвиг
// увёл бы низ стенки от кромки на сдвиг, умноженный на наклон поля).
double layerFloorBottomMm(const std::vector<float>& depthMm, int countX, int countY,
                          const StockBounds& bounds, double cellMm, double cutMm,
                          double xMm, double yMm, double nx, double ny, double nudgeMm)
{
    const double qx = xMm + nx * nudgeMm;
    const double qy = yMm + ny * nudgeMm;
    const double rule = layerRuleDepth(depthMm, countX, countY, bounds, cellMm, cutMm,
                                       qx, qy, StockLayer::Floor);
    const double bilinear = bilinearDepth(depthMm, countX, countY, qx, qy, bounds, cellMm);
    if (std::fabs(rule - bilinear) > 1e-9) {
        return rule;                          // правило ступает: кромка сдвинута
    }
    return layerRuleDepth(depthMm, countX, countY, bounds, cellMm, cutMm,
                          xMm, yMm, StockLayer::Floor);
}

} // namespace

double stockLayerDepthMm(const StockBounds& bounds, double cellMm,
                         const std::vector<float>& depthMm, int countX, int countY,
                         double cutMm, double xMm, double yMm, StockLayer layer)
{
    if (countX < 2 || countY < 2 || !(cellMm > 0.0)
            || depthMm.size() < std::size_t(countX) * std::size_t(countY)) {
        return 0.0;
    }
    return layerRuleDepth(depthMm, countX, countY, bounds, cellMm, cutMm, xMm, yMm, layer);
}

StockWallMesh stockCutWallFromLayer(const StockBounds& bounds, double cellMm,
                                    const std::vector<float>& depthMm, int countX, int countY,
                                    double cutMm,
                                    const std::vector<StockContourSegment>& segments)
{
    StockWallMesh mesh;
    if (segments.empty() || countX < 2 || countY < 2 || !(cellMm > 0.0)) {
        return mesh;
    }
    // Тот же сдвиг внутрь дна, что у половины дна (stockLayerSurface): низ
    // стенки обязан совпасть с её кромкой точка в точку, иначе щель.
    const double nudgeMm = cellMm * 0.04;
    const float zTop = float(bounds.zMaxMm);
    mesh.vertices.reserve(segments.size() * 4 * 4);
    mesh.indices.reserve(segments.size() * 6);
    for (const StockContourSegment& segment : segments) {
        const double dx = segment.x1Mm - segment.x0Mm;
        const double dy = segment.y1Mm - segment.y0Mm;
        const double len = std::hypot(dx, dy);
        double nx = 0.0;
        double ny = 0.0;
        if (len > 0.0) {
            nx = -dy / len;
            ny = dx / len;
            // Нормаль в сторону дна: билинейная глубина по обе стороны отрезка
            // (снято глубже нетронутого).
            const double mx = 0.5 * (segment.x0Mm + segment.x1Mm);
            const double my = 0.5 * (segment.y0Mm + segment.y1Mm);
            const double probe = cellMm * 0.25;
            const double plus = bilinearDepth(depthMm, countX, countY,
                                              mx + nx * probe, my + ny * probe, bounds, cellMm);
            const double minus = bilinearDepth(depthMm, countX, countY,
                                               mx - nx * probe, my - ny * probe, bounds, cellMm);
            if (minus > plus) {
                nx = -nx;
                ny = -ny;
            }
        }
        const double d0 = layerFloorBottomMm(depthMm, countX, countY, bounds, cellMm, cutMm,
                                             segment.x0Mm, segment.y0Mm, nx, ny, nudgeMm);
        const double d1 = layerFloorBottomMm(depthMm, countX, countY, bounds, cellMm, cutMm,
                                             segment.x1Mm, segment.y1Mm, nx, ny, nudgeMm);
        // Низ на кромке отрезанного дна, верх на плоскости нетронутого верха:
        // стык верх/стенка/дно идёт по одной границе, без щели.
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

StockLayerSurface stockLayerSurface(const StockBounds& bounds, double cellMm,
                                    const std::vector<float>& depthMm, int countX, int countY,
                                    double cutMm,
                                    const std::vector<StockContourSegment>& segments)
{
    StockLayerSurface out;
    if (countX < 2 || countY < 2 || !(cellMm > 0.0)
            || depthMm.size() < std::size_t(countX) * std::size_t(countY)) {
        return out;
    }
    const int cellsX = countX - 1;
    const int cellsY = countY - 1;
    const float zTop = float(bounds.zMaxMm);
    const double nudgeMm = cellMm * 0.04;
    const auto at = [&](int ix, int iy) {
        return double(depthMm[std::size_t(iy) * std::size_t(countX) + std::size_t(ix)]);
    };
    // Узел сетки: последний прижат к габариту, как у stockSurfaceMesh (k = 1).
    const auto nodeX = [&](int ix) {
        const double x = bounds.xMinMm + double(ix) * cellMm;
        return ix == countX - 1 ? std::min(x, bounds.xMaxMm) : x;
    };
    const auto nodeY = [&](int iy) {
        const double y = bounds.yMinMm + double(iy) * cellMm;
        return iy == countY - 1 ? std::min(y, bounds.yMaxMm) : y;
    };
    // Цепочки границы по клеткам: отрезок целиком лежит в своей клетке (marching
    // squares соединяет переходы на её рёбрах), поэтому клетка отрезка - клетка
    // его середины. Подряд идущие отрезки, у которых конец одного совпадает с
    // началом другого, склеиваются в цепочку: у уточнённой границы переход между
    // рёбрами клетки раздроблен, и внутри клетки у цепочки есть узлы
    // (stockCutBoundary, subdivPerCell).
    struct Chain {
        std::vector<std::array<double, 2>> points;   // A, узлы..., B
    };
    std::vector<std::vector<Chain>> chains(std::size_t(cellsX) * std::size_t(cellsY));
    constexpr double CHAIN_JOIN_MM = 1e-9;           // звенья цепочки общие точкой
    for (const StockContourSegment& segment : segments) {
        const double mx = 0.5 * (segment.x0Mm + segment.x1Mm);
        const double my = 0.5 * (segment.y0Mm + segment.y1Mm);
        const int ix = std::max(0, std::min(cellsX - 1,
                                            int(std::floor((mx - bounds.xMinMm) / cellMm))));
        const int iy = std::max(0, std::min(cellsY - 1,
                                            int(std::floor((my - bounds.yMinMm) / cellMm))));
        std::vector<Chain>& cell = chains[std::size_t(iy) * std::size_t(cellsX) + std::size_t(ix)];
        if (!cell.empty()) {
            const std::array<double, 2>& last = cell.back().points.back();
            if (std::hypot(last[0] - segment.x0Mm, last[1] - segment.y0Mm) <= CHAIN_JOIN_MM) {
                cell.back().points.push_back({segment.x1Mm, segment.y1Mm});
                continue;
            }
        }
        Chain chain;
        chain.points.push_back({segment.x0Mm, segment.y0Mm});
        chain.points.push_back({segment.x1Mm, segment.y1Mm});
        cell.push_back(std::move(chain));
    }

    // Точка половины: позиция и нормаль границы внутрь половины (у точки схода).
    struct Point {
        double x = 0.0;
        double y = 0.0;
        double nx = 0.0;
        double ny = 0.0;
        bool onBoundary = false;
    };
    std::vector<float> topVertices;
    std::vector<float> floorVertices;
    std::vector<std::uint32_t> topIndices;
    std::vector<std::uint32_t> floorIndices;
    int boundaryTop = 0;
    int boundaryFloor = 0;
    int shiftedTop = 0;
    int shiftedFloor = 0;

    // Нужен ли сдвиг texcoord у граничной точки: он нужен там, где правило
    // шейдера СТУПАЕТ (у уступа знак выбирает уровень). У склона правило
    // билинейно, и половины обязаны сойтись одним texcoord - иначе высоты кромок
    // разошлись бы на сдвиг.
    const auto needsShift = [&](const Point& p, StockLayer layer) {
        const double qx = p.x + p.nx * nudgeMm;
        const double qy = p.y + p.ny * nudgeMm;
        const double rule = layerRuleDepth(depthMm, countX, countY, bounds, cellMm, cutMm,
                                           qx, qy, layer);
        const double bilinear = bilinearDepth(depthMm, countX, countY, qx, qy, bounds, cellMm);
        return std::fabs(rule - bilinear) > 1e-9;
    };
    const auto emit = [&](const std::vector<Point>& poly, StockLayer layer) {
        if (poly.size() < 3) {
            return;
        }
        const bool floor = layer == StockLayer::Floor;
        std::vector<float>& vertices = floor ? floorVertices : topVertices;
        std::vector<std::uint32_t>& indices = floor ? floorIndices : topIndices;
        const std::uint32_t base = std::uint32_t(vertices.size() / 6);
        for (const Point& p : poly) {
            double u = ((p.x - bounds.xMinMm) / cellMm + 0.5) / double(countX);
            double v = ((p.y - bounds.yMinMm) / cellMm + 0.5) / double(countY);
            if (p.onBoundary) {
                (floor ? boundaryFloor : boundaryTop) += 1;
                if (needsShift(p, layer)) {
                    u += p.nx * nudgeMm / (cellMm * double(countX));
                    v += p.ny * nudgeMm / (cellMm * double(countY));
                    (floor ? shiftedFloor : shiftedTop) += 1;
                }
            }
            vertices.push_back(float(p.x));
            vertices.push_back(float(p.y));
            vertices.push_back(zTop);
            vertices.push_back(float(u));
            vertices.push_back(float(v));
            // Сторона половины едет признаком вершины (stocksurface.vert,
            // steppedDepth): у уступа высота - уровень СВОЕЙ стороны, а знать её
            // может только тот, кто режет клетку, - то есть здесь. Прежде шейдер
            // называл сторону знаком знакового расстояния по углам клетки, и
            // угловая хорда отставала от цепочки: вершины верха падали на дно
            // отвесным треугольником (замер - случай wall_rim_drawn). Признак
            // занимает один float - шестой; шейдер читает его как UV1.x.
            vertices.push_back(floor ? 0.0f : 1.0f);
        }
        for (std::size_t i = 1; i + 1 < poly.size(); ++i) {
            indices.push_back(base);
            indices.push_back(base + std::uint32_t(i));
            indices.push_back(base + std::uint32_t(i + 1));
        }
    };
    // Место точки на обходе многоугольника: ближайшее ребро. Точка цепочки стоит
    // на ребре (marching squares соединяет переходы на рёбрах), поэтому ближайшее
    // ребро и есть её ребро.
    const auto edgeOfPoint = [](const std::vector<Point>& poly, double x, double y) {
        double best = std::numeric_limits<double>::max();
        int edge = 0;
        const std::size_t n = poly.size();
        for (std::size_t i = 0; i < n; ++i) {
            const Point& a = poly[i];
            const Point& b = poly[(i + 1) % n];
            const double dist = pointSegmentDistanceMm(x, y, a.x, a.y, b.x, b.y);
            if (dist < best) {
                best = dist;
                edge = int(i);
            }
        }
        return edge;
    };
    // Точка внутри обхода (луч вправо): серединой цепочки она находит своего
    // хозяина - часть клетки, которую цепочка режет.
    const auto insidePoly = [](const std::vector<Point>& poly, double x, double y) {
        bool in = false;
        const std::size_t n = poly.size();
        for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
            const double xi = poly[i].x;
            const double yi = poly[i].y;
            const double xj = poly[j].x;
            const double yj = poly[j].y;
            if ((yi > y) != (yj > y) && x < (xj - xi) * (y - yi) / (yj - yi) + xi) {
                in = !in;
            }
        }
        return in;
    };
    // Добавить точку в конец обхода, если она не повторяет последнюю: переход
    // ровно в угол клетки даёт ту же точку дважды.
    const auto pushPoint = [](std::vector<Point>& poly, const Point& p) {
        if (!poly.empty() && std::hypot(poly.back().x - p.x, poly.back().y - p.y) <= 1e-12) {
            return;
        }
        poly.push_back(p);
    };
    // Снять последнюю точку, если она повторяет первую (обход замкнут).
    const auto dropClosingRepeat = [](std::vector<Point>& poly) {
        while (poly.size() > 2 && std::hypot(poly.front().x - poly.back().x,
                                             poly.front().y - poly.back().y) <= 1e-12) {
            poly.pop_back();
        }
    };

    // Часть клетки: обход против часовой (как углы клетки 0-1-2-3) и её половина.
    struct Part {
        std::vector<Point> poly;
        int floorSide = -1;              // 1 - дно, 0 - верх, -1 - не названа
    };
    // Одна половина одной клетки. Клетка без цепочек границы уходит целиком,
    // двумя треугольниками. Клетку с цепочками режет каждая цепочка: половина
    // берёт СВОЮ сторону цепочки - обход по цепочке плюс обход по рёбрам клетки.
    // Цепочка идёт по уровню реза (у уточнённой границы внутри клетки есть узлы),
    // поэтому резать её хордой нельзя: узлы половины обязаны лечь на полилинию
    // границы, а куски половин - не налезать друг на друга (сумма площадей
    // половин равна площади клетки, случай layer_surface). Сторону называет угол
    // между рёбрами переходов, первый по обходу клетки от начала цепочки: правило
    // прежнее - его глубину читает узел поля.
    const auto buildCell = [&](int ix, int iy, StockLayer layer) {
        const double x0 = nodeX(ix);
        const double x1 = nodeX(ix + 1);
        const double y0 = nodeY(iy);
        const double y1 = nodeY(iy + 1);
        const double d[4] = { at(ix, iy), at(ix + 1, iy), at(ix + 1, iy + 1), at(ix, iy + 1) };
        const double cx[4] = { x0, x1, x1, x0 };
        const double cy[4] = { y0, y0, y1, y1 };
        const std::vector<Chain>& cellChains =
                chains[std::size_t(iy) * std::size_t(cellsX) + std::size_t(ix)];
        if (cellChains.empty()) {
            const bool floorCell = d[0] > cutMm || d[1] > cutMm || d[2] > cutMm || d[3] > cutMm;
            if (floorCell != (layer == StockLayer::Floor)) {
                return;                      // клетка другой половины
            }
            std::vector<Point> quad(4);
            for (int k = 0; k < 4; ++k) {
                quad[k].x = cx[k];
                quad[k].y = cy[k];
            }
            emit(quad, layer);
            return;
        }
        // Части клетки: сначала вся клетка, дальше каждая цепочка режет свою.
        std::vector<Part> parts(1);
        for (int k = 0; k < 4; ++k) {
            Point p;
            p.x = cx[k];
            p.y = cy[k];
            parts[0].poly.push_back(p);
        }
        for (const Chain& chain : cellChains) {
            const std::size_t pointCount = chain.points.size();
            if (pointCount < 2) {
                continue;
            }
            // Хозяин цепочки: часть, внутри которой лежит её середина.
            const std::array<double, 2>& mid = chain.points[pointCount / 2];
            std::size_t host = 0;
            for (std::size_t i = 0; i < parts.size(); ++i) {
                if (insidePoly(parts[i].poly, mid[0], mid[1])) {
                    host = i;
                    break;
                }
            }
            const std::array<double, 2>& a = chain.points.front();
            const std::array<double, 2>& b = chain.points.back();
            // Угол дуги: первый по обходу КЛЕТКИ от ребра начала цепочки. Его
            // глубину читает узел поля - она и называет сторону дуги.
            int edgeA = 0;
            double bestEdge = std::numeric_limits<double>::max();
            for (int e = 0; e < 4; ++e) {
                const int e2 = (e + 1) & 3;
                const double dist = pointSegmentDistanceMm(a[0], a[1], cx[e], cy[e],
                                                           cx[e2], cy[e2]);
                if (dist < bestEdge) {
                    bestEdge = dist;
                    edgeA = e;
                }
            }
            const int arcCorner = (edgeA + 1) & 3;
            const bool arcIsFloor = d[arcCorner] > cutMm;
            const std::vector<Point>& hostPoly = parts[host].poly;
            const std::size_t n = hostPoly.size();
            const int iA = edgeOfPoint(hostPoly, a[0], a[1]);
            const int iB = edgeOfPoint(hostPoly, b[0], b[1]);
            if (iA == iB) {
                continue;      // оба перехода на одном ребре: у клетки их по одному
            }
            // Нормали узлов цепочки: половина лежит влево от СВОЕГО обхода
            // цепочки (обход клетки против часовой). Нормаль нужна только сдвигу
            // texcoord (доля клетки), и у почти прямой цепочки она общая.
            const double dx = b[0] - a[0];
            const double dy = b[1] - a[1];
            const double len = std::hypot(dx, dy);
            const double nx = len > 0.0 ? -dy / len : 0.0;   // влево от A -> B
            const double ny = len > 0.0 ? dx / len : 0.0;
            const auto chainPoint = [&](double px, double py, bool onArc) {
                Point point;
                point.x = px;
                point.y = py;
                point.nx = onArc ? -nx : nx;    // сторона дуги - вправо от A -> B
                point.ny = onArc ? -ny : ny;
                point.onBoundary = true;
                return point;
            };
            // Часть дуги (где угол дуги): граница идёт A -> угол -> B по обходу
            // клетки, а по цепочке - обратно от B к A.
            Part arc;
            arc.poly.reserve(pointCount + n + 1);
            arc.poly.push_back(chainPoint(a[0], a[1], true));
            for (std::size_t k = (std::size_t(iA) + 1) % n; ; k = (k + 1) % n) {
                pushPoint(arc.poly, hostPoly[k]);
                if (k == std::size_t(iB)) {
                    break;
                }
            }
            arc.poly.push_back(chainPoint(b[0], b[1], true));
            for (std::size_t k = pointCount - 1; k-- > 1;) {
                pushPoint(arc.poly, chainPoint(chain.points[k][0], chain.points[k][1], true));
            }
            dropClosingRepeat(arc.poly);
            // Другая часть: цепочка идёт от A к B, обход клетки - от B к A.
            Part far;
            far.poly.reserve(pointCount + n);
            for (std::size_t k = 0; k + 1 < pointCount; ++k) {
                pushPoint(far.poly, chainPoint(chain.points[k][0], chain.points[k][1], false));
            }
            far.poly.push_back(chainPoint(b[0], b[1], false));
            for (std::size_t k = (std::size_t(iB) + 1) % n; ; k = (k + 1) % n) {
                pushPoint(far.poly, hostPoly[k]);
                if (k == std::size_t(iA)) {
                    break;
                }
            }
            dropClosingRepeat(far.poly);
            arc.floorSide = arcIsFloor ? 1 : 0;
            far.floorSide = arcIsFloor ? 0 : 1;
            parts[host] = std::move(arc);
            parts.push_back(std::move(far));
        }
        for (const Part& part : parts) {
            if ((part.floorSide == 1) != (layer == StockLayer::Floor)) {
                continue;
            }
            emit(part.poly, layer);
        }
    };

    for (int pass = 0; pass < 2; ++pass) {
        const StockLayer layer = pass == 0 ? StockLayer::Top : StockLayer::Floor;
        for (int iy = 0; iy < cellsY; ++iy) {
            for (int ix = 0; ix < cellsX; ++ix) {
                buildCell(ix, iy, layer);
            }
        }
        if (pass == 0) {
            out.topVertexCount = int(topVertices.size() / 6);
            out.topIndexCount = int(topIndices.size());
        }
    }
    out.vertices = std::move(topVertices);
    out.vertices.insert(out.vertices.end(), floorVertices.begin(), floorVertices.end());
    out.indices = std::move(topIndices);
    for (std::uint32_t index : floorIndices) {
        out.indices.push_back(index + std::uint32_t(out.topVertexCount));
    }
    out.boundaryVertices = boundaryTop + boundaryFloor;
    out.shiftedUvVertices = shiftedTop + shiftedFloor;
    return out;
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
