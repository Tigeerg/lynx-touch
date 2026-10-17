// core/tooling.cpp - инструмент и оправка (зеркало tooling.py).
//
// Порядок операций повторяет питоновский: профиль инструмента собирается
// участками (острие -> фаска или нос -> канавки -> шейка -> тело), затем по нему
// считаются длина, радиус, обводка и кольца. Числа сверяются с эталоном
// побайтово (modules/gcode-viewer/test_controller_cpp.py), поэтому выражение
// пишется в том же порядке, что в Python: (pi / 2) * k / k_n, а не k * pi / 2 / k_n.
#include "core/tooling.h"

#include "core/text_util.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace gv {

namespace {

constexpr double PI = 3.14159265358979323846;      // math.pi

// Градусы -> радианы как питоновский math.radians: x * (pi / 180).
double radians(double deg)
{
    return deg * (PI / 180.0);
}

// Точное произведение (Деккеровская пара): z + zz равно x * y без округления.
struct DoubleLength {
    double hi = 0.0;
    double lo = 0.0;
};

DoubleLength dlMul(double x, double y)
{
    const double z = x * y;
    return DoubleLength{z, std::fma(x, y, -z)};
}

DoubleLength dlFastSum(double a, double b)
{
    const double x = a + b;
    return DoubleLength{x, (a - x) + b};
}

// Число из окружения с умолчанием: неразобранное значение не отличимо от
// заданного нуля и было бы молчаливой порчей оправки.
double envDouble(const char* name, double fallback)
{
    const char* raw = std::getenv(name);
    if (raw == nullptr || *raw == '\0') {
        return fallback;
    }
    char* end = nullptr;
    const double value = strtodC(raw, &end);
    if (end == raw || (end != nullptr && *end != '\0')) {
        return fallback;
    }
    return value;
}

// Гипотенуза по CPython vector_norm (два аргумента): масштаб на степень двойки,
// точный квадрат через fma, сумма с компенсацией и дифференциальная поправка.
// Входные NaN и бесконечности у наших профилей недостижимы: там конечные числа.
double vectorNorm2(double a, double b)
{
    const double x0 = std::fabs(a);
    const double x1 = std::fabs(b);
    double max = 0.0;
    if (x0 > max) {
        max = x0;
    }
    if (x1 > max) {
        max = x1;
    }
    if (std::isinf(max)) {
        return max;
    }
    if (max == 0.0) {
        return max;
    }
    int max_e = 0;
    std::frexp(max, &max_e);
    if (max_e < -1023) {
        // Субнормальные значения в профилях не встречаются: масштаб не составить.
        return std::sqrt(x0 * x0 + x1 * x1);
    }
    const double scale = std::ldexp(1.0, -max_e);
    const double vals[2] = {x0, x1};
    double csum = 1.0;
    double frac1 = 0.0;
    double frac2 = 0.0;
    for (int i = 0; i < 2; ++i) {
        const double x = vals[i] * scale;             // масштаб без потерь
        const DoubleLength pr = dlMul(x, x);          // квадрат без потерь
        const DoubleLength sm = dlFastSum(csum, pr.hi);
        csum = sm.hi;
        frac1 += pr.lo;
        frac2 += sm.lo;
    }
    double h = std::sqrt(csum - 1.0 + (frac1 + frac2));
    const DoubleLength pr = dlMul(-h, h);
    const DoubleLength sm = dlFastSum(csum, pr.hi);
    csum = sm.hi;
    frac1 += pr.lo;
    frac2 += sm.lo;
    const double rest = csum - 1.0 + (frac1 + frac2);
    h += rest / (2.0 * h);                            // дифференциальная поправка
    return h / scale;
}

} // namespace

HolderConstants holderConstants()
{
    // Читаются один раз: в Python это значения на момент импорта модуля.
    static const HolderConstants values = [] {
        HolderConstants out;
        out.nut_d = envDouble("CNC_HOLDER_NUT_D", out.nut_d);
        out.nut_h = envDouble("CNC_HOLDER_NUT_H", out.nut_h);
        out.body_d = envDouble("CNC_HOLDER_BODY_D", out.body_d);
        out.body_h = envDouble("CNC_HOLDER_BODY_H", out.body_h);
        return out;
    }();
    return values;
}

double pyHypot(double x, double y)
{
    return vectorNorm2(x, y);
}

double pyRound2(double x)
{
    if (!std::isfinite(x)) {
        return x;
    }
    const bool negative = x < 0.0;
    const double a = std::fabs(x);
    // Точное произведение a * 100 = z + zz: zz решает, с какой стороны от
    // середины лежит значение. std::round(z) / 100 не годится - он уводит
    // половину от нуля, а питоновский round - к чётному (12.345 -> 12.34).
    const double z = a * 100.0;
    const double zz = std::fma(a, 100.0, -z);
    const double fl = std::floor(z);
    const bool exact_half = (z - fl) == 0.5;
    double n = exact_half ? (std::fmod(fl, 2.0) == 0.0 ? fl : fl + 1.0) : std::nearbyint(z);
    if (exact_half) {
        if (zz > 0.0) {
            n = fl + 1.0;
        } else if (zz < 0.0) {
            n = fl;
        }
    }
    const double rounded = n / 100.0;
    return negative ? -rounded : rounded;
}

std::vector<ProfilePoint> toolProfile(const ToolInfo& info)
{
    const double r = info.diameter / 2.0;
    if (r <= 0.0) {
        return {};
    }
    std::string typ = info.type;
    const double cr = info.corner_radius;
    const double taper = info.taper_deg;
    const double tipa = info.tip_angle_deg;
    const double tipd = info.tip_diameter;
    double fl = info.flute_length;
    const double sl = info.shoulder_length;
    const double bl = info.body_length;
    const double sd = info.shank_diameter;
    const double ol = info.overall_length;
    if (typ == "FLAT" && taper > 0.0 && cr <= 0.0) {
        typ = "DRILL";               // коническое острие: пост не разобрал форму
    }
    if (fl <= 0.0) {
        fl = std::max(2.0 * r, 16.0);  // длины канавок в старых постах нет - держим видимой
    }

    std::vector<ProfilePoint> prof;
    prof.push_back({0.0, 0.0});      // ось на острие
    double y_cut = 0.0;
    if (typ == "BALL") {
        const int k_n = 12;          // полусферическая носовая часть
        for (int k = 1; k <= k_n; ++k) {
            const double phi = -PI / 2.0 + (PI / 2.0) * double(k) / double(k_n);
            prof.push_back({r * std::cos(phi), r + r * std::sin(phi)});
        }
        y_cut = r;
        if (fl > r) {
            prof.push_back({r, fl});
            y_cut = fl;
        }
    } else if (typ == "DRILL" || typ == "TAP") {
        const double ang = taper > 0.0 ? taper : (tipa > 0.0 ? tipa : 118.0);
        const double tr = std::tan(radians(std::max(1.0, std::min(89.0, ang)) / 2.0));
        double tip_h = 0.0;
        if (tipd > 0.0) {
            prof.push_back({tipd / 2.0, 0.0});
            tip_h = tr > 1e-9 ? (r - tipd / 2.0) / tr : r;
        } else {
            tip_h = tr > 1e-9 ? r / tr : r;
        }
        tip_h = std::max(0.0, tip_h);
        prof.push_back({r, tip_h});
        y_cut = tip_h;
        if (fl > tip_h) {
            prof.push_back({r, fl});
            y_cut = fl;
        }
    } else if (typ == "CHAMFER" || typ == "TAPER") {
        const double tr = std::tan(radians(std::max(1.0, std::min(89.0, taper > 0.0 ? taper : 45.0))));
        double tip_h = 0.0;
        if (tipd > 0.0) {
            prof.push_back({tipd / 2.0, 0.0});
            tip_h = tr > 1e-9 ? (r - tipd / 2.0) / tr : r;
        } else {
            tip_h = tr > 1e-9 ? r / tr : r;
        }
        tip_h = std::max(0.0, tip_h);
        prof.push_back({r, tip_h});
        y_cut = tip_h;
        if (fl > tip_h) {
            prof.push_back({r, fl});
            y_cut = fl;
        }
    } else {                             // FLAT / BULL / LOLLIPOP / DOVETAIL / PROBE
        if (cr > 0.0 && cr < r) {        // скруглённый нижний угол (радиусная концевая)
            prof.push_back({r - cr, 0.0});
            const int k_n = 8;
            for (int k = 1; k <= k_n; ++k) {
                const double phi = -PI / 2.0 + (PI / 2.0) * double(k) / double(k_n);
                prof.push_back({(r - cr) + cr * std::cos(phi), cr + cr * std::sin(phi)});
            }
            y_cut = cr;
            if (fl > cr) {
                prof.push_back({r, fl});
                y_cut = fl;
            }
        } else {
            prof.push_back({r, 0.0});
            prof.push_back({r, fl});
            y_cut = fl;
        }
    }

    // ---- канавки -> шейка -> тело (вылет = длина тела) ----
    // Оправка держит хвостовик на конце тела, поэтому видимая режущая часть идёт
    // от острия до BL (длина тела), как показывает HSMWorks; OL только
    // ограничивает её (тело не длиннее инструмента целиком). Радиусы: режущая
    // часть `r` по канавкам, хвостовик `rb` (SD) по телу.
    const double rb = sd > 0.0 ? sd / 2.0 : r;
    const double fl_v = std::max(fl, y_cut);
    const double sl_v = std::max(sl > 0.0 ? sl : fl_v, fl_v);
    double bl_v = bl > 0.0 ? bl : (ol > 0.0 ? ol : sl_v);
    bl_v = std::max(bl_v, sl_v);
    if (ol > 0.0) {
        bl_v = std::min(bl_v, ol);     // не длиннее инструмента целиком
    }
    if (fl_v > y_cut) {
        prof.push_back({r, fl_v});     // прямая канавка
    }
    if (sl_v > fl_v) {
        prof.push_back({r, sl_v});     // шейка держит радиус режущей части
    }
    if (std::fabs(rb - r) > 1e-9) {
        prof.push_back({rb, sl_v});    // плоская ступень к хвостовику
    }
    if (bl_v > sl_v) {
        prof.push_back({rb, bl_v});    // тело до вылета
    }
    prof.push_back({0.0, bl_v});
    return prof;
}

std::vector<ProfilePoint> toolOutlineProfile(const std::vector<ProfilePoint>& profile, double pad)
{
    const std::size_t n = profile.size();
    if (n < 2) {
        return profile;
    }
    // Наружная нормаль каждого отрезка в плоскости (r, y): наружу = (ty, -tr) / L.
    std::vector<std::array<double, 2>> seg(n - 1);
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const double tr = profile[i + 1].radius_mm - profile[i].radius_mm;
        const double ty = profile[i + 1].height_mm - profile[i].height_mm;
        double L = pyHypot(tr, ty);
        if (L == 0.0) {
            L = 1.0;                                   // Python: `or 1.0`
        }
        seg[i] = {ty / L, -tr / L};
    }
    std::vector<ProfilePoint> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        const double r = profile[i].radius_mm;
        const double y = profile[i].height_mm;
        double nr = 0.0;
        double ny = 0.0;
        if (i == 0) {
            nr = seg[0][0];
            ny = seg[0][1];
        } else if (i == n - 1) {
            nr = seg[n - 2][0];
            ny = seg[n - 2][1];
        } else {                                       // биссектриса двух смежных рёбер
            nr = seg[i - 1][0] + seg[i][0];
            ny = seg[i - 1][1] + seg[i][1];
            const double L = pyHypot(nr, ny);
            if (L > 1e-9) {
                nr = nr / L;
                ny = ny / L;
            } else {
                nr = seg[i][0];
                ny = seg[i][1];
            }
        }
        out.push_back({std::max(0.0, r + nr * pad), y + ny * pad});
    }
    return out;
}

double toolExposedLength(const ToolInfo& info)
{
    if (!info.present) {
        return 0.0;
    }
    const std::vector<ProfilePoint> prof = toolProfile(info);
    double top = 0.0;
    for (const ProfilePoint& p : prof) {
        top = std::max(top, p.height_mm);
    }
    return prof.empty() ? 0.0 : top;
}

std::vector<HolderSection> holderProfile(double tool_len)
{
    const HolderConstants c = holderConstants();
    const double rn = c.nut_d / 2.0;
    const double rb = c.body_d / 2.0;
    return {
        HolderSection{rn, tool_len, tool_len + c.nut_h},
        HolderSection{rb, tool_len + c.nut_h, tool_len + c.nut_h + c.body_h},
    };
}

std::vector<ProfilePoint> holderSolidProfile()
{
    const HolderConstants c = holderConstants();
    const double rn = c.nut_d / 2.0;
    const double rb = c.body_d / 2.0;
    const double hn = c.nut_h;
    const double hb = c.body_h;
    const double pair[][2] = {
        {0.0, 0.0},
        {rn - 3.0, 0.0},                     // нижний торец
        {rn, 3.0},                           // нижняя фаска
        {rn, std::max(3.0, hn - 6.0)},       // поясок накатанной гайки
        {rn - 1.0, std::max(3.0, hn - 2.0)}, // верхняя фаска
        {rn - 5.0, hn},                      // верхний торец гайки
        {rn - 5.0, hn + 2.0},
        {rb, hn + 6.0},                      // переход в корпус
        {rb, hn + hb},                       // корпус (со стороны шпинделя)
        {0.0, hn + hb},
    };
    std::vector<ProfilePoint> out;
    const std::size_t count = sizeof(pair) / sizeof(pair[0]);
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back({pair[i][0], pair[i][1]});
    }
    return out;
}

std::vector<ProfilePoint> holderOutlineProfile()
{
    // Плоские торцы сняты: увеличенная копия не должна показывать их крышками.
    const std::vector<ProfilePoint> solid = holderSolidProfile();
    std::vector<ProfilePoint> out;
    out.reserve(solid.size());
    for (const ProfilePoint& p : solid) {
        if (p.radius_mm > 1e-6) {
            out.push_back(p);
        }
    }
    return out;
}

std::vector<RingSegment> ringsSegments(const std::vector<ProfilePoint>& profile, int segments)
{
    if (profile.empty() || segments < 3) {
        return {};
    }
    const double two_pi = 2.0 * PI;
    std::vector<RingSegment> out;
    for (const ProfilePoint& p : profile) {
        const double r = p.radius_mm;
        const double y = p.height_mm;
        if (r <= 1e-6) {
            continue;
        }
        for (int s = 0; s < segments; ++s) {
            const double a0 = two_pi * double(s) / double(segments);
            const double a1 = two_pi * double(s + 1) / double(segments);
            RingSegment seg;
            seg.x0 = r * std::cos(a0);
            seg.y0 = y;
            seg.z0 = r * std::sin(a0);
            seg.x1 = r * std::cos(a1);
            seg.y1 = y;
            seg.z1 = r * std::sin(a1);
            out.push_back(seg);
        }
    }
    return out;
}

std::vector<ProfilePoint> profileRingPoints(const std::vector<ProfilePoint>& profile, double min_deg)
{
    const std::size_t n = profile.size();
    if (n < 2) {
        return {};
    }
    const double cos_t = std::cos(radians(min_deg));
    std::vector<ProfilePoint> out;
    for (std::size_t i = 0; i < n; ++i) {
        const double r = profile[i].radius_mm;
        const double y = profile[i].height_mm;
        if (r <= 1e-6) {
            continue;
        }
        if (i == 0 || i == n - 1) {
            out.push_back({r, y});
            continue;
        }
        const double d0r = profile[i].radius_mm - profile[i - 1].radius_mm;
        const double d0y = profile[i].height_mm - profile[i - 1].height_mm;
        const double d1r = profile[i + 1].radius_mm - profile[i].radius_mm;
        const double d1y = profile[i + 1].height_mm - profile[i].height_mm;
        double L0 = pyHypot(d0r, d0y);
        if (L0 == 0.0) {
            L0 = 1.0;
        }
        double L1 = pyHypot(d1r, d1y);
        if (L1 == 0.0) {
            L1 = 1.0;
        }
        const double c = (d0r * d1r + d0y * d1y) / (L0 * L1);
        if (c < cos_t) {
            out.push_back({r, y});
        }
    }
    return out;
}

} // namespace gv
