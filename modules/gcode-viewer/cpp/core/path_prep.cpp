// core/path_prep.cpp - подготовка траектории (зеркало path_prep.py).
//
// Буферы сверяются с питоновскими побайтово, поэтому повторены порядок
// суммирования (numpy.sum по трём элементам складывает слева направо, как и
// цикл здесь) и приведение к float32 (округление к ближайшему чётному у обоих).
#include "path_prep.h"

#include <cmath>
#include <cstring>

namespace gv {
namespace {

// double -> float32 -> 4 байта little-endian: формат буфера геометрии.
void appendFloat(std::vector<unsigned char>& out, double v) {
    const float f = static_cast<float>(v);
    unsigned char buf[4];
    std::memcpy(buf, &f, 4);
    out.insert(out.end(), buf, buf + 4);
}

} // namespace

Aabb sceneBoundsLoHi(const std::array<double, 3>& lo, const std::array<double, 3>& hi) {
    Aabb out;
    out.lo = {lo[0], lo[2], -hi[1]};
    out.hi = {hi[0], hi[2], -lo[1]};
    return out;
}

PackedStream packStream(const std::vector<double>& pts, const std::vector<unsigned char>& flags,
                        const std::vector<double>& cum, double total, bool wantRapid,
                        bool machine) {
    PackedStream out;
    const size_t n = pts.size() / 3;
    const double inv = (total > 0.0) ? (1.0 / total) : 0.0;
    if (n < 2) { return out; }

    // Отрезок i использует флаг точки i: так же режется питоновский mask.
    std::vector<size_t> idx;
    idx.reserve(n - 1);
    for (size_t i = 0; i + 1 < n; ++i) {
        if ((flags[i] != 0) == wantRapid) { idx.push_back(i); }
    }
    if (idx.empty()) { return out; }

    out.bytes.reserve(idx.size() * 10 * 4);
    for (size_t i : idx) {
        double s0[3] = {pts[i * 3 + 0], pts[i * 3 + 1], pts[i * 3 + 2]};
        double s1[3] = {pts[(i + 1) * 3 + 0], pts[(i + 1) * 3 + 1], pts[(i + 1) * 3 + 2]};
        if (machine) {
            const double t = s0[1];
            s0[1] = s0[2];
            s0[2] = -t;
            const double t1 = s1[1];
            s1[1] = s1[2];
            s1[2] = -t1;
        }
        const double t0 = cum[i] * inv;
        const double t1 = cum[i + 1] * inv;
        appendFloat(out.bytes, s0[0]);
        appendFloat(out.bytes, s0[1]);
        appendFloat(out.bytes, s0[2]);
        appendFloat(out.bytes, t0);
        appendFloat(out.bytes, 0.0);
        appendFloat(out.bytes, s1[0]);
        appendFloat(out.bytes, s1[1]);
        appendFloat(out.bytes, s1[2]);
        appendFloat(out.bytes, t1);
        appendFloat(out.bytes, 0.0);
    }
    out.nv = static_cast<int>(idx.size()) * 2;
    return out;
}

std::vector<StageRange> stagesNorm(const std::vector<Stage>& stages,
                                   const std::vector<double>& cum, double total) {
    std::vector<StageRange> out;
    const size_t n = stages.size();
    if (n == 0) { return out; }
    const double inv = (total > 0.0) ? (1.0 / total) : 0.0;
    auto at = [&cum, inv](int index) {
        if (index < 0 || static_cast<size_t>(index) >= cum.size()) { return 0.0; }
        return cum[static_cast<size_t>(index)] * inv;
    };
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        StageRange r;
        r.label = stages[i].label;
        r.info = stages[i].info;
        double s = at(stages[i].index);
        const double e = (i + 1 < n) ? at(stages[i + 1].index) : 1.0;
        s = std::max(0.0, std::min(1.0, s));
        r.start = s;
        r.end = std::max(s, std::min(1.0, e));
        out.push_back(r);
    }
    return out;
}

PathData prepareOrdered(const std::vector<PathPoint>& ordered, const std::vector<Stage>& stages) {
    PathData out;
    const size_t n = ordered.size();
    if (n == 0) { return out; }               // center (0,0,0), radius 50, min_y 0

    out.last = ordered.back().point;
    std::vector<double> pts(n * 3);
    out.rapid.resize(n);
    out.speeds.resize(n);
    for (size_t i = 0; i < n; ++i) {
        pts[i * 3 + 0] = ordered[i].point[0];
        pts[i * 3 + 1] = ordered[i].point[1];
        pts[i * 3 + 2] = ordered[i].point[2];
        out.rapid[i] = ordered[i].is_rapid ? 1 : 0;
        out.speeds[i] = ordered[i].speed_mm_s;
    }

    out.cum.assign(n, 0.0);
    if (n > 1) {
        double acc = 0.0;
        for (size_t i = 1; i < n; ++i) {
            const double dx = pts[i * 3 + 0] - pts[(i - 1) * 3 + 0];
            const double dy = pts[i * 3 + 1] - pts[(i - 1) * 3 + 1];
            const double dz = pts[i * 3 + 2] - pts[(i - 1) * 3 + 2];
            // numpy: (d*d).sum(axis=1) для трёх элементов - слева направо.
            const double d2 = dx * dx + dy * dy + dz * dz;
            acc += std::sqrt(d2);
            out.cum[i] = acc;
        }
        out.total = out.cum[n - 1];
    } else {
        out.total = 0.0;
    }

    double lo[3] = {pts[0], pts[1], pts[2]};
    double hi[3] = {pts[0], pts[1], pts[2]};
    for (size_t i = 1; i < n; ++i) {
        for (int k = 0; k < 3; ++k) {
            const double v = pts[i * 3 + static_cast<size_t>(k)];
            if (v < lo[k]) { lo[k] = v; }
            if (v > hi[k]) { hi[k] = v; }
        }
    }

    // Полилиния анимации в координатах сцены: машинные (X, Y, Z) -> (X, Z, -Y).
    out.pts.resize(n * 3);
    for (size_t i = 0; i < n; ++i) {
        out.pts[i * 3 + 0] = pts[i * 3 + 0];
        out.pts[i * 3 + 1] = pts[i * 3 + 2];
        out.pts[i * 3 + 2] = -pts[i * 3 + 1];
    }

    out.rapid_stream = packStream(pts, out.rapid, out.cum, out.total, true);
    out.feed_stream = packStream(pts, out.rapid, out.cum, out.total, false);

    const std::array<double, 3> loA{lo[0], lo[1], lo[2]};
    const std::array<double, 3> hiA{hi[0], hi[1], hi[2]};
    out.bounds = sceneBoundsLoHi(loA, hiA);
    out.has_bounds = true;

    const double cm[3] = {(lo[0] + hi[0]) / 2.0, (lo[1] + hi[1]) / 2.0, (lo[2] + hi[2]) / 2.0};
    out.center = {cm[0], cm[2], -cm[1]};

    // Питоновский sum((hi-lo)**2 для трёх осей): x**2 - это pow(x, 2.0).
    const double len2 = std::pow(hi[0] - lo[0], 2.0) + std::pow(hi[1] - lo[1], 2.0)
                        + std::pow(hi[2] - lo[2], 2.0);
    out.radius = 0.5 * std::sqrt(len2);

    out.min_y = pts[2];                       // минимум машинного Z
    for (size_t i = 1; i < n; ++i) {
        if (pts[i * 3 + 2] < out.min_y) { out.min_y = pts[i * 3 + 2]; }
    }

    out.seg_count = static_cast<int>(n) - 1;
    out.stages = stagesNorm(stages, out.cum, out.total);
    return out;
}

} // namespace gv
