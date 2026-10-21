// core/tooling.cpp - tool and holder.
//
// The order of operations repeats the reference: the tool profile is assembled
// from sections (tip -> chamfer or nose -> flutes -> neck -> body), then the
// length, radius, outline and rings are computed from it. The numbers are checked
// against the reference byte for byte, so the expression is written in the same
// order as the reference: (pi / 2) * k / k_n, not k * pi / 2 / k_n.
#include "core/tooling.h"

#include "core/text_util.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace gv {

namespace {

constexpr double PI = 3.14159265358979323846;      // math.pi

// Degrees -> radians like the reference math.radians: x * (pi / 180).
double radians(double deg)
{
    return deg * (PI / 180.0);
}

// Exact product (Dekker pair): z + zz equals x * y without rounding.
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

// A number from the environment with a fallback: an unparsed value would be
// indistinguishable from a given zero and would silently corrupt the holder.
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

// Hypotenuse by CPython vector_norm (two arguments): scaling by a power of two,
// an exact square via fma, compensated summation and a differential correction.
// NaN and infinities are unreachable for our profiles: the numbers are finite.
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
        // Subnormal values do not occur in profiles: the scale cannot be formed.
        return std::sqrt(x0 * x0 + x1 * x1);
    }
    const double scale = std::ldexp(1.0, -max_e);
    const double vals[2] = {x0, x1};
    double csum = 1.0;
    double frac1 = 0.0;
    double frac2 = 0.0;
    for (int i = 0; i < 2; ++i) {
        const double x = vals[i] * scale;             // scaling without loss
        const DoubleLength pr = dlMul(x, x);          // square without loss
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
    h += rest / (2.0 * h);                            // differential correction
    return h / scale;
}

} // namespace

HolderConstants holderConstants()
{
    // Read once: in the reference these are the values at module import.
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
    // The exact product a * 100 = z + zz: zz decides on which side of the middle
    // the value lies. std::round(z) / 100 does not do - it rounds half away from
    // zero, while the reference round goes half to even (12.345 -> 12.34).
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
        typ = "DRILL";               // conical tip: the postprocessor did not parse the shape
    }
    if (fl <= 0.0) {
        fl = std::max(2.0 * r, 16.0);  // old posts have no flute length - keep it visible
    }

    std::vector<ProfilePoint> prof;
    prof.push_back({0.0, 0.0});      // axis at the tip
    double y_cut = 0.0;
    if (typ == "BALL") {
        const int k_n = 12;          // hemispherical nose
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
        if (cr > 0.0 && cr < r) {        // rounded bottom corner (corner-radius end mill)
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

    // ---- flutes -> neck -> body (gauge length = body length) ----
    // The holder grips the shank at the end of the body, so the visible cutting
    // part runs from the tip to BL (body length), as HSMWorks shows; OL only
    // limits it (the body is no longer than the whole tool). Radii: the cutting
    // part `r` along the flutes, the shank `rb` (SD) along the body.
    const double rb = sd > 0.0 ? sd / 2.0 : r;
    const double fl_v = std::max(fl, y_cut);
    const double sl_v = std::max(sl > 0.0 ? sl : fl_v, fl_v);
    double bl_v = bl > 0.0 ? bl : (ol > 0.0 ? ol : sl_v);
    bl_v = std::max(bl_v, sl_v);
    if (ol > 0.0) {
        bl_v = std::min(bl_v, ol);     // no longer than the whole tool
    }
    if (fl_v > y_cut) {
        prof.push_back({r, fl_v});     // straight flute
    }
    if (sl_v > fl_v) {
        prof.push_back({r, sl_v});     // the neck keeps the cutting-part radius
    }
    if (std::fabs(rb - r) > 1e-9) {
        prof.push_back({rb, sl_v});    // flat step to the shank
    }
    if (bl_v > sl_v) {
        prof.push_back({rb, bl_v});    // body up to the gauge length
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
    // Outer normal of each segment in the (r, y) plane: outward = (ty, -tr) / L.
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
        } else {                                       // bisector of two adjacent edges
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
        {rn - 3.0, 0.0},                     // bottom face
        {rn, 3.0},                           // bottom chamfer
        {rn, std::max(3.0, hn - 6.0)},       // knurled nut land
        {rn - 1.0, std::max(3.0, hn - 2.0)}, // top chamfer
        {rn - 5.0, hn},                      // nut top face
        {rn - 5.0, hn + 2.0},
        {rb, hn + 6.0},                      // transition into the body
        {rb, hn + hb},                       // body (spindle side)
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
    // Flat faces are removed: the enlarged copy must not show them as caps.
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
