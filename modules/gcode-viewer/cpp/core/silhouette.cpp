#include "core/silhouette.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <utility>

// The order of operations here is part of the contract: the result is checked
// against the reference digests bit for bit, not "approximately". It is compiled
// with /fp:precise (MSVC) and -ffp-contract=off (GCC/Clang): reassociation and
// FMA shift the low bits of the float32 result.

namespace gv {
namespace {

constexpr double DEG_TO_RAD = 3.14159265358979323846 / 180.0;

// Weld key of a vertex: the position rounded to the weld grid (np.round(V / weld)).
struct WeldKey {
    double v[3];
};

// Lexicographic key order, like np.unique(axis=0): fields left to right.
// Equality is element-wise (==), so -0.0 and 0.0 are one node.
inline bool keyLess(const WeldKey& a, const WeldKey& b) {
    for (int i = 0; i < 3; ++i) {
        if (a.v[i] < b.v[i]) { return true; }
        if (a.v[i] > b.v[i]) { return false; }
    }
    return false;
}

inline bool keyEqual(const WeldKey& a, const WeldKey& b) {
    return a.v[0] == b.v[0] && a.v[1] == b.v[1] && a.v[2] == b.v[2];
}

// Triangle edge: canonically ordered ends (a < b) and the face number.
struct EdgeRec {
    std::uint32_t a;
    std::uint32_t b;
    std::uint32_t face;
};

// Candidate silhouette edge: ends and two adjacent faces (f0 < f1).
struct Candidate {
    std::uint32_t a;
    std::uint32_t b;
    std::int32_t f0;
    std::int32_t f1;
};

// np.clip(x, -1.0, 1.0) without a NaN branch: NaN passes through, like numpy
// (std::min/std::max would turn NaN into one).
inline double clipUnit(double x) {
    if (x < -1.0) { return -1.0; }
    if (x > 1.0) { return 1.0; }
    return x;
}

} // namespace

MeshTopology buildSilhouetteTopology(const std::vector<double>& soup,
                                     const QualityPreset& quality) {
    MeshTopology topo;
    const std::size_t faces = soup.size() / 9;
    topo.faces = faces;
    if (faces == 0) {
        return topo;
    }

    // Vertex welding: one position on the weld grid is one index. Without it,
    // adjacent faces of one B-rep edge get different indices and the edge
    // adjacency map cannot be built. The key is the position in weld units; key
    // equality is the welding.
    const std::size_t slots = faces * 3;
    std::vector<WeldKey> key(slots);
    for (std::size_t i = 0; i < slots; ++i) {
        for (int d = 0; d < 3; ++d) {
            key[i].v[d] = std::nearbyint(soup[i * 3 + std::size_t(d)] / quality.silhouetteWeldMm);
        }
    }
    std::vector<std::uint32_t> order(slots);
    std::iota(order.begin(), order.end(), std::uint32_t(0));
    std::sort(order.begin(), order.end(), [&key](std::uint32_t x, std::uint32_t y) {
        return keyLess(key[x], key[y]);
    });

    // The cluster representative is the first key of the group in sorted order
    // (in numpy also the first: mask by aux[1:] != aux[:-1]). Equal keys on a
    // STEP soup are bit-identical (the zero keys all came from -0.0), so numpy's
    // sort instability does not affect ea/eb.
    std::vector<std::uint32_t> inv(slots);
    std::vector<WeldKey> uniq;
    uniq.reserve(slots / 6 + 1);
    for (std::size_t i = 0; i < slots; ++i) {
        const WeldKey& k = key[order[i]];
        if (uniq.empty() || !keyEqual(uniq.back(), k)) {
            uniq.push_back(k);
        }
        inv[order[i]] = static_cast<std::uint32_t>(uniq.size() - 1);
    }

    // The node position is the cluster representative: the deviation from the
    // original point is <= weld/2 (5e-5 mm), but deterministic.
    std::vector<double> pos(uniq.size() * 3);
    for (std::size_t g = 0; g < uniq.size(); ++g) {
        for (int d = 0; d < 3; ++d) {
            pos[g * 3 + std::size_t(d)] = uniq[g].v[d] * quality.silhouetteWeldMm;
        }
    }

    // Three edges of each face (a < b) in the reference order: face by face,
    // inside a face - (0,1), (1,2), (2,0).
    std::vector<EdgeRec> edge(slots);
    for (std::size_t f = 0; f < faces; ++f) {
        for (std::uint32_t j = 0; j < 3; ++j) {
            const std::uint32_t v0 = inv[f * 3 + j];
            const std::uint32_t v1 = inv[f * 3 + (j + 1) % 3];
            EdgeRec& rec = edge[f * 3 + j];
            rec.a = std::min(v0, v1);
            rec.b = std::max(v0, v1);
            rec.face = static_cast<std::uint32_t>(f);
        }
    }
    // The edge key is (a,b), like numpy; the third key is the face: in the
    // reference the group is built by a stable sort, so inside a group the faces
    // go in ascending order and f0 is the smaller one.
    std::sort(edge.begin(), edge.end(), [](const EdgeRec& x, const EdgeRec& y) {
        if (x.a != y.a) { return x.a < y.a; }
        if (x.b != y.b) { return x.b < y.b; }
        return x.face < y.face;
    });

    // Candidates are edges with exactly two adjacent faces: a silhouette lives
    // at the junction of two faces. Edges of another multiplicity (the STEP soup
    // has some: 1, 4, 6) are filtered out, as in the reference.
    std::vector<Candidate> cand;
    for (std::size_t i = 0; i < edge.size();) {
        std::size_t j = i + 1;
        while (j < edge.size() && edge[j].a == edge[i].a && edge[j].b == edge[i].b) {
            ++j;
        }
        if (j - i == 2) {
            cand.push_back(Candidate{edge[i].a, edge[i].b,
                                     static_cast<std::int32_t>(edge[i].face),
                                     static_cast<std::int32_t>(edge[i + 1].face)});
        }
        i = j;
    }

    // Normals are geometric, from the triangles themselves: the silhouette
    // horizon is set by a face normal flip, and smoothing would blur it. The
    // summation order is the same as numpy (cross, then the square root of the
    // sum of squares left to right).
    std::vector<double> nrm(faces * 3, 0.0);
    std::vector<double> area(faces, 0.0);
    for (std::size_t f = 0; f < faces; ++f) {
        const double* t = &soup[f * 9];
        const double d0[3] = {t[3] - t[0], t[4] - t[1], t[5] - t[2]};
        const double d1[3] = {t[6] - t[0], t[7] - t[1], t[8] - t[2]};
        const double cr[3] = {d0[1] * d1[2] - d0[2] * d1[1],
                              d0[2] * d1[0] - d0[0] * d1[2],
                              d0[0] * d1[1] - d0[1] * d1[0]};
        const double len = std::sqrt(((cr[0] * cr[0] + cr[1] * cr[1]) + cr[2] * cr[2]));
        area[f] = 0.5 * len;
        const double div = (len == 0.0) ? 1.0 : len;   // degenerate face: normal as is
        for (int d = 0; d < 3; ++d) {
            nrm[f * 3 + std::size_t(d)] = cr[d] / div;
        }
    }

    // Candidate filter: only smooth horizons (no sharp edges - the static edge
    // layer draws those) and no degenerate faces (their normal is numerical
    // noise, the sign flips and breeds short false whiskers).
    const double angle_lim = quality.silhouetteMaxEdgeDeg * DEG_TO_RAD;
    topo.ea.reserve(cand.size() * 3);
    topo.eb.reserve(cand.size() * 3);
    topo.f0.reserve(cand.size());
    topo.f1.reserve(cand.size());
    for (const Candidate& c : cand) {
        const std::size_t f0 = std::size_t(c.f0);
        const std::size_t f1 = std::size_t(c.f1);
        if (!(area[f0] > quality.silhouetteMinFaceAreaMm2)
                || !(area[f1] > quality.silhouetteMinFaceAreaMm2)) {
            continue;
        }
        const double* n0 = &nrm[f0 * 3];
        const double* n1 = &nrm[f1 * 3];
        // Dot product of the normals left to right: np.einsum over three terms
        // gives a different low bit in places, but the 20-degree decision does
        // not depend on it - the angle nearest the threshold is 0.054 rad away.
        const double dot = ((n0[0] * n1[0] + n0[1] * n1[1]) + n0[2] * n1[2]);
        if (!(std::acos(clipUnit(dot)) < angle_lim)) {
            continue;
        }
        topo.f0.push_back(c.f0);
        topo.f1.push_back(c.f1);
        for (int d = 0; d < 3; ++d) {
            topo.ea.push_back(static_cast<float>(pos[std::size_t(c.a) * 3 + std::size_t(d)]));
            topo.eb.push_back(static_cast<float>(pos[std::size_t(c.b) * 3 + std::size_t(d)]));
        }
    }

    // n and d: d = n·cen in float32, cen is the face centroid (mean of three
    // vertices, double, then float32). A single summation order with numpy:
    // (n0*c0 + n1*c1) + n2*c2, addition left to right.
    topo.n.resize(faces * 3);
    topo.d.resize(faces);
    for (std::size_t f = 0; f < faces; ++f) {
        const double* t = &soup[f * 9];
        const float cen[3] = {static_cast<float>(((t[0] + t[3]) + t[6]) / 3.0),
                              static_cast<float>(((t[1] + t[4]) + t[7]) / 3.0),
                              static_cast<float>(((t[2] + t[5]) + t[8]) / 3.0)};
        const float nv[3] = {static_cast<float>(nrm[f * 3 + 0]),
                             static_cast<float>(nrm[f * 3 + 1]),
                             static_cast<float>(nrm[f * 3 + 2])};
        for (int d = 0; d < 3; ++d) {
            topo.n[f * 3 + std::size_t(d)] = nv[d];
        }
        topo.d[f] = ((nv[0] * cen[0] + nv[1] * cen[1]) + nv[2] * cen[2]);
    }
    return topo;
}

std::vector<float> meshSilhouetteSegments(const MeshTopology& topo, const double eyeMm[3]) {
    MeshSilhouetteScratch scratch;
    meshSilhouetteSegments(topo, eyeMm, scratch);
    return std::move(scratch.out);      // move, not copy: as the former return of out
}

const std::vector<float>& meshSilhouetteSegments(const MeshTopology& topo, const double eyeMm[3],
                                                 MeshSilhouetteScratch& scratch) {
    scratch.out.clear();
    const std::size_t faces = topo.n.size() / 3;
    if (faces == 0 || topo.edgeCount() == 0) {
        return scratch.out;
    }

    // The eye is float32, as in the reference (np.asarray(E, dtype=float32)).
    const float ex = static_cast<float>(eyeMm[0]);
    const float ey = static_cast<float>(eyeMm[1]);
    const float ez = static_cast<float>(eyeMm[2]);

    // Sign s = n·E - d per face. A face with s == 0 (the eye in its plane,
    // including -0.0) gets 0 and is not counted as a silhouette edge - like the
    // reference's product < 0.
    scratch.sign.assign(faces, 0);
    for (std::size_t f = 0; f < faces; ++f) {
        const float* nf = &topo.n[f * 3];
        const float s = ((nf[0] * ex + nf[1] * ey) + nf[2] * ez) - topo.d[f];
        scratch.sign[f] = static_cast<std::int8_t>(s < 0.0f ? -1 : (s > 0.0f ? 1 : 0));
    }

    const std::size_t edges = topo.edgeCount();
    for (std::size_t k = 0; k < edges; ++k) {
        const std::int32_t f0 = topo.f0[k];
        const std::int32_t f1 = topo.f1[k];
        if (scratch.sign[std::size_t(f0)] * scratch.sign[std::size_t(f1)] >= 0) {
            continue;
        }
        for (int d = 0; d < 3; ++d) {
            scratch.out.push_back(topo.ea[k * 3 + std::size_t(d)]);
        }
        for (int d = 0; d < 3; ++d) {
            scratch.out.push_back(topo.eb[k * 3 + std::size_t(d)]);
        }
    }
    return scratch.out;
}

} // namespace gv
