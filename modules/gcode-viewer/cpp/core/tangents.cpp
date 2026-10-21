// core/tangents.cpp - STEP tangent families and the per-frame pass over them.
//
// The order of operations is part of the contract with the reference: the family
// arrays and tangent segments are checked against numpy bit for bit, so the file
// is compiled with /fp:precise (MSVC) and -ffp-contract=off (GCC/Clang), and the
// sums, averages and products use the same summation order as numpy.
#include "core/tangents.h"

#include "core/np_helpers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace gv {
namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double TWO_PI = 2.0 * PI;          // as 2 * math.pi in the reference
constexpr double DEG_TO_RAD = PI / 180.0;    // as math.radians: x * (pi / 180)
constexpr double NAN_ = std::numeric_limits<double>::quiet_NaN();

// Dot product, cross product and remainder by divisor sign - the shared bodies
// are in core/np_helpers.h.
using gv::np::cross3;
using gv::np::dot3;
using gv::np::npMod;

// Norm of a 3-vector (np.linalg.norm): the square root of the sum of squares.
// The body is shared in core/np_helpers.h.
inline double norm3(const double* v) {
    return gv::np::norm3Sqrt(v);
}

// Round half to even: numpy's np.round(x, n) multiplies by 10^n, rounds with rint
// and divides back; the reference builtin round(x, n) gives the same result on
// non-trivial values (nearbyint in FE_TONEAREST mode).
inline double roundDecimals(double x, double multiplier) {
    return std::nearbyint(x * multiplier) / multiplier;
}

// Mirrors _pt_tri_dist2: the squared distance from point p to each of the k
// triangles tri (a flat row of 9 coordinates). Inside the triangle it is the
// projection by the normal equation, outside - the distance to the nearest
// vertex.
void pointTriangleDist2(const double p[3], const double* tri, std::size_t k, double* out) {
    for (std::size_t i = 0; i < k; ++i) {
        const double* t = tri + i * 9;
        const double a[3] = {t[0], t[1], t[2]};
        const double e1[3] = {t[3] - t[0], t[4] - t[1], t[5] - t[2]};
        const double e2[3] = {t[6] - t[0], t[7] - t[1], t[8] - t[2]};
        const double v[3] = {p[0] - a[0], p[1] - a[1], p[2] - a[2]};
        const double a11 = dot3(e1, e1);
        const double a12 = dot3(e1, e2);
        const double a22 = dot3(e2, e2);
        const double d1 = dot3(e1, v);
        const double d2 = dot3(e2, v);
        double det = a11 * a22 - a12 * a12;
        if (std::fabs(det) < 1e-12) {
            det = 1.0;
        }
        const double s = (a22 * d1 - a12 * d2) / det;
        const double tt = (a11 * d2 - a12 * d1) / det;
        const bool inside = (s >= 0.0) && (tt >= 0.0) && (s + tt <= 1.0);
        const double q[3] = {a[0] + e1[0] * s + e2[0] * tt,
                             a[1] + e1[1] * s + e2[1] * tt,
                             a[2] + e1[2] * s + e2[2] * tt};
        const double pq[3] = {p[0] - q[0], p[1] - q[1], p[2] - q[2]};
        const double d2i = pq[0] * pq[0] + pq[1] * pq[1] + pq[2] * pq[2];
        double dv = 0.0;
        for (int c = 0; c < 3; ++c) {
            const double dx = p[0] - t[c * 3 + 0];
            const double dy = p[1] - t[c * 3 + 1];
            const double dz = p[2] - t[c * 3 + 2];
            const double dc = dx * dx + dy * dy + dz * dz;
            if (c == 0 || dc < dv) {
                dv = dc;
            }
        }
        out[i] = inside ? d2i : dv;
    }
}

// Mirrors _cone_keeps_silhouette: a cone gets tangent generators if it (1) is not
// shorter than the preset's slant length threshold and (2) is concave (the skin
// normals at its surface point toward the axis). Without skin the concavity check
// is skipped, only the length threshold remains.
bool coneKeepsSilhouette(const ConeSurface& cone, const SkinTriangles* skin,
                         const QualityPreset& quality) {
    const double slant = std::fabs(cone.s1 - cone.s0);
    if (slant < quality.coneMinSlantMm) {
        return false;
    }
    if (skin == nullptr || !skin->valid) {
        return true;
    }
    double d[3] = {cone.d[0], cone.d[1], cone.d[2]};
    const double dlen = std::max(norm3(d), 1e-12);
    for (int j = 0; j < 3; ++j) {
        d[j] /= dlen;
    }
    const double apex[3] = {cone.a[0], cone.a[1], cone.a[2]};
    const double psi = cone.psi;
    // any two unit vectors orthogonal to the axis: the probe goes around the cone
    const double ref[3] = {std::fabs(d[2]) < 0.9 ? 0.0 : 1.0, 0.0,
                           std::fabs(d[2]) < 0.9 ? 1.0 : 0.0};
    const double dref = dot3(d, ref);
    double u1[3] = {ref[0] - d[0] * dref, ref[1] - d[1] * dref, ref[2] - d[2] * dref};
    const double u1len = std::max(norm3(u1), 1e-12);
    for (int j = 0; j < 3; ++j) {
        u1[j] /= u1len;
    }
    double u2[3];
    cross3(d, u1, u2);
    const double sMid = (cone.s0 + cone.s1) / 2.0;
    const double side = sMid >= 0.0 ? 1.0 : -1.0;   // the second cavity of the cone
    // numpy.argpartition(dd, 15) on an array shorter than 16 elements raises an
    // exception, and it is swallowed - the cone stays. The same outcome.
    if (skin->count < 16) {
        return true;
    }
    const double cosPsi = std::cos(psi);
    const double sinPsi = std::sin(psi);
    std::vector<double> dd(skin->count);
    std::vector<std::size_t> order(skin->count);
    std::vector<double> tri16(16 * 9);
    std::vector<double> dv16(16);
    for (int k = 0; k < quality.coneProbeAzimuths; ++k) {
        const double ph = TWO_PI * k / quality.coneProbeAzimuths;
        const double cph = std::cos(ph);
        const double sph = std::sin(ph);
        double rad[3];
        for (int j = 0; j < 3; ++j) {
            rad[j] = (u1[j] * cph + u2[j] * sph) * side;
        }
        double p[3];
        for (int j = 0; j < 3; ++j) {
            const double along = d[j] * cosPsi + rad[j] * side * sinPsi;
            p[j] = apex[j] + along * sMid;
        }
        for (std::size_t f = 0; f < skin->count; ++f) {
            const double w[3] = {skin->skcen[f * 3 + 0] - p[0],
                                 skin->skcen[f * 3 + 1] - p[1],
                                 skin->skcen[f * 3 + 2] - p[2]};
            dd[f] = dot3(w, w);
            order[f] = f;
        }
        std::partial_sort(order.begin(), order.begin() + 16, order.end(),
                          [&dd](std::size_t a, std::size_t b) {
                              if (dd[a] != dd[b]) {
                                  return dd[a] < dd[b];
                              }
                              return a < b;
                          });
        for (std::size_t c = 0; c < 16; ++c) {
            std::memcpy(&tri16[c * 9], &skin->sktri[order[c] * 9], 9 * sizeof(double));
        }
        pointTriangleDist2(p, tri16.data(), 16, dv16.data());
        int own = 0;
        int other = 0;
        bool any = false;
        for (std::size_t c = 0; c < 16; ++c) {
            if (!(dv16[c] < 1.5)) {
                continue;
            }
            any = true;
            const double* irm = &skin->skirm[order[c] * 3];
            const double dot = dot3(irm, rad);
            if (dot < -0.05) {
                ++own;
            }
            if (dot > 0.05) {
                ++other;
            }
        }
        if (!any) {
            continue;                       // the probe fell past the face
        }
        if (own > 0 && own >= other) {
            return true;
        }
    }
    return false;
}

// Weld key of coincident conical surfaces (_dedupe_cones): apex, canonical axis
// direction, half-angle and the sorted slice s.
struct ConeKey {
    double a[3];
    double d[3];
    double psi;
    double slo;
    double shi;

    bool operator==(const ConeKey& o) const {
        for (int j = 0; j < 3; ++j) {
            if (a[j] != o.a[j] || d[j] != o.d[j]) {
                return false;
            }
        }
        return psi == o.psi && slo == o.slo && shi == o.shi;
    }
};

// Mirrors _dedupe_cones: one cone geometry in STEP is split across several face
// TShapes, and without welding each generator is drawn twice at one point.
std::vector<ConeSurface> dedupeCones(const std::vector<ConeSurface>& cones) {
    std::vector<ConeSurface> out;
    std::vector<ConeKey> seen;
    for (const ConeSurface& c : cones) {
        double d[3] = {c.d[0], c.d[1], c.d[2]};
        const double dlen = std::max(norm3(d), 1e-12);
        for (int j = 0; j < 3; ++j) {
            d[j] /= dlen;
        }
        std::vector<int> sig;
        for (int j = 0; j < 3; ++j) {
            if (std::fabs(d[j]) > 1e-4) {
                sig.push_back(j);
            }
        }
        double s0 = c.s0;
        double s1 = c.s1;
        if (!sig.empty() && d[sig[0]] < 0.0) {
            for (int j = 0; j < 3; ++j) {
                d[j] = -d[j];
            }
            const double ns0 = -s1;             // when the axis flips the range is reflected
            const double ns1 = -s0;
            s0 = ns0;
            s1 = ns1;
        }
        ConeKey key;
        for (int j = 0; j < 3; ++j) {
            key.a[j] = roundDecimals(c.a[j], 1000.0);
            key.d[j] = roundDecimals(d[j], 10000.0);
        }
        key.psi = roundDecimals(c.psi, 10000.0);
        const double r0 = roundDecimals(s0, 1000.0);
        const double r1 = roundDecimals(s1, 1000.0);
        key.slo = std::min(r0, r1);
        key.shi = std::max(r0, r1);
        bool dup = false;
        for (const ConeKey& k : seen) {
            if (k == key) {
                dup = true;
                break;
            }
        }
        if (dup) {
            continue;
        }
        seen.push_back(key);
        out.push_back(c);
    }
    return out;
}

// One family record: axis, cross-section basis, limits along the axis, fullness
// and arc coverage. The reference keeps these record lists separately
// (_tangent_families).
struct Entry {
    double o[3] = {0.0, 0.0, 0.0};
    double d[3] = {0.0, 0.0, 1.0};
    double e1[3] = {1.0, 0.0, 0.0};
    double e2[3] = {0.0, 0.0, 1.0};
    bool full = false;
    double r = 0.0;
    double t0 = 0.0;
    double t1 = 0.0;
    std::array<bool, 64> arc{};
    double a0 = 0.0;
    double a1 = 0.0;
};

// _union_a: union of the arcs of two sections; NaN in either gives NaN.
void unionArc(double& a0, double& a1, double c0, double c1) {
    if (!(std::isfinite(a0) && std::isfinite(c0))) {
        a0 = NAN_;
        a1 = NAN_;
        return;
    }
    a0 = std::min(a0, c0);
    a1 = std::max(a1, c1);
}

} // namespace

SkinTriangles buildSkinTriangles(const std::vector<double>& soup,
                                 const std::vector<float>& normals) {
    SkinTriangles out;
    // asarray(..., float).reshape(-1, 3, 3) in the reference raises an exception
    // if the length is not divisible by 9; the exception is swallowed, and there
    // is no skin at all.
    if (soup.size() % 9 != 0 || normals.size() % 9 != 0) {
        return out;
    }
    out.valid = true;
    out.count = soup.size() / 9;
    out.sktri = soup;
    out.skcen.resize(out.count * 3);
    out.skirm.resize(out.count * 3);
    for (std::size_t f = 0; f < out.count; ++f) {
        for (int j = 0; j < 3; ++j) {
            out.skcen[f * 3 + std::size_t(j)] = (soup[f * 9 + std::size_t(j)]
                                                 + soup[f * 9 + 3 + std::size_t(j)]
                                                 + soup[f * 9 + 6 + std::size_t(j)]) / 3.0;
            out.skirm[f * 3 + std::size_t(j)] = static_cast<double>(normals[f * 9 + std::size_t(j)]);
        }
    }
    return out;
}

TangentFamilies buildTangentFamilies(const StepSurfaceList& surfs,
                                     const PackedEdges* packedEdges,
                                     const SkinTriangles* skin,
                                     const QualityPreset& quality) {
    TangentFamilies out;
    std::vector<ConeSurface> cones;

    // Cylinders are grouped by key (axis with rounding, canonical direction,
    // radius); the group order is the order of first appearance, like a Python
    // dict. The key is 180 values on Test_model, so a linear search is cheaper
    // than a hash table and does not depend on the hash of a double.
    struct GroupKey {
        double op[3];
        double dr[3];
        double r;
    };
    std::vector<GroupKey> keys;
    std::vector<std::vector<const CylinderSurface*>> groups;

    for (const StepSurface& s : surfs) {
        if (!s.is_cylinder) {
            cones.push_back(s.cone);
            continue;
        }
        const CylinderSurface& c = s.cyl;
        double d[3] = {c.d[0], c.d[1], c.d[2]};
        const double dn = norm3(d);
        if (dn <= 1e-12) {
            continue;
        }
        for (int j = 0; j < 3; ++j) {
            d[j] /= dn;
        }
        double oPerp[3];
        const double al = dot3(d, c.o);
        for (int j = 0; j < 3; ++j) {
            oPerp[j] = c.o[j] - d[j] * al;
        }
        double dref[3] = {d[0], d[1], d[2]};
        for (int j = 0; j < 3; ++j) {
            if (std::fabs(d[j]) > 1e-9) {
                if (d[j] < 0.0) {               // lexicographic direction
                    for (int k = 0; k < 3; ++k) {
                        dref[k] = -d[k];
                    }
                }
                break;
            }
        }
        GroupKey key;
        for (int j = 0; j < 3; ++j) {
            key.op[j] = roundDecimals(oPerp[j], 10.0);
            key.dr[j] = roundDecimals(dref[j], 100.0);
        }
        key.r = roundDecimals(c.r, 100.0);
        std::size_t gi = keys.size();
        for (std::size_t k = 0; k < keys.size(); ++k) {
            if (keys[k].r == key.r && keys[k].op[0] == key.op[0] && keys[k].op[1] == key.op[1]
                    && keys[k].op[2] == key.op[2] && keys[k].dr[0] == key.dr[0]
                    && keys[k].dr[1] == key.dr[1] && keys[k].dr[2] == key.dr[2]) {
                gi = k;
                break;
            }
        }
        if (gi == keys.size()) {
            keys.push_back(key);
            groups.push_back({});
        }
        groups[gi].push_back(&c);
    }

    std::vector<Entry> entries;
    for (std::size_t g = 0; g < groups.size(); ++g) {
        const std::vector<const CylinderSurface*>& faces = groups[g];
        // the canonical direction of the key, not faces[0]: faces merged by it may
        // look along the axis in different directions
        double d[3] = {keys[g].dr[0], keys[g].dr[1], keys[g].dr[2]};
        const double dlen = std::max(norm3(d), 1e-12);
        for (int j = 0; j < 3; ++j) {
            d[j] /= dlen;
        }
        // the exact axis trace, not the rounded key: a key with a 0.1 mm step
        // pulled the generator off the true axis
        double o[3] = {0.0, 0.0, 0.0};
        for (const CylinderSurface* s : faces) {
            const double al = dot3(d, s->o);
            for (int j = 0; j < 3; ++j) {
                o[j] += s->o[j] - d[j] * al;
            }
        }
        for (int j = 0; j < 3; ++j) {
            o[j] /= static_cast<double>(faces.size());
        }
        bool full = false;
        for (const CylinderSurface* s : faces) {
            if (!s->has_arc) {
                full = true;
                break;
            }
            if (npMod(s->arc1 - s->arc0, TWO_PI) >= TWO_PI - 1e-6) {
                full = true;
                break;
            }
        }
        // axial clusters: different holes on one axis with one radius must not
        // merge into one long generator through the void
        std::vector<std::array<double, 2>> tbs;
        for (const CylinderSurface* s : faces) {
            double lo = s->t0;
            double hi = s->t1;
            if (dot3(d, s->d) < 0.0) {          // the face axis is opposite to the canonical one
                const double nlo = -hi;
                const double nhi = -lo;
                lo = nlo;
                hi = nhi;
            }
            const double w[3] = {s->o[0] - o[0], s->o[1] - o[1], s->o[2] - o[2]};
            const double c = dot3(d, w);
            tbs.push_back({c + lo, c + hi});
        }
        std::sort(tbs.begin(), tbs.end());
        std::vector<std::array<double, 2>> spans;
        for (const std::array<double, 2>& tb : tbs) {
            if (!spans.empty() && tb[0] <= spans.back()[1] + 1e-6) {
                spans.back()[1] = std::max(spans.back()[1], tb[1]);
            } else {
                spans.push_back(tb);
            }
        }
        double rsum = 0.0;
        for (const CylinderSurface* s : faces) {
            rsum += s->r;
        }
        const double rrep = rsum / static_cast<double>(faces.size());
        // Silhouette policy: the preset's arc threshold and minimum span (85 deg
        // and 2.5 mm at med) keep a generator only on real surfaces of revolution.
        double wmax = 0.0;
        for (const CylinderSurface* s : faces) {
            if (!s->has_arc) {
                wmax = TWO_PI;
                break;
            }
            wmax = std::max(wmax, npMod(s->arc1 - s->arc0, TWO_PI));
        }
        bool tangented = full || wmax >= quality.silhouetteArcDeg * DEG_TO_RAD;
        if (tangented) {
            for (const std::array<double, 2>& sp : spans) {
                if ((sp[1] - sp[0]) < quality.silhouetteMinSpanMm) {
                    tangented = false;
                    break;
                }
            }
        }
        // azimuthal coverage - from the analytic face arc, not from the mesh
        std::array<bool, 64> abins{};
        double fa0 = NAN_;
        double fa1 = NAN_;
        for (const CylinderSurface* s : faces) {
            if (!s->has_arc) {
                abins.fill(true);
                break;
            }
            const double spn = npMod(s->arc1 - s->arc0, TWO_PI);
            if (spn >= TWO_PI - 1e-6) {
                abins.fill(true);
                break;
            }
            const int nb = std::max(1, static_cast<int>(std::nearbyint(spn / TWO_PI * 64.0)));
            for (int k = 0; k < nb; ++k) {
                const double ang = npMod(s->arc0 + (k + 0.5) / nb * spn, TWO_PI);
                abins[static_cast<std::size_t>(ang / TWO_PI * 64.0) % 64] = true;
            }
            fa0 = s->arc0;
            fa1 = s->arc0 + spn;
        }
        for (const std::array<double, 2>& sp : spans) {
            Entry e;
            for (int j = 0; j < 3; ++j) {
                e.o[j] = o[j];
                e.d[j] = d[j];
                e.e1[j] = faces[0]->e1[j];
                e.e2[j] = faces[0]->e2[j];
            }
            e.full = tangented;
            e.r = rrep;
            e.t0 = sp[0];
            e.t1 = sp[1];
            e.arc = abins;
            e.a0 = fa0;
            e.a1 = fa1;
            entries.push_back(e);
        }
    }

    // Merge families on one axis: modeling noise splits one hole into two keys.
    // Only equal radii merge - a real step keeps its silhouette at the true wall
    // radius.
    struct Merged {
        std::size_t idx;
        double t0;
        double t1;
        bool full;
        std::array<bool, 64> arc;
        double a0;
        double a1;
    };
    std::vector<Merged> merged;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        bool placed = false;
        for (Merged& m : merged) {
            const Entry& rep = entries[m.idx];
            if (std::fabs(entries[i].r - rep.r) > 0.05) {
                continue;
            }
            if (std::fabs(dot3(entries[i].d, rep.d)) < 0.999) {
                continue;
            }
            const double w[3] = {entries[i].o[0] - rep.o[0], entries[i].o[1] - rep.o[1],
                                 entries[i].o[2] - rep.o[2]};
            const double al = dot3(w, rep.d);
            const double p[3] = {w[0] - rep.d[0] * al, w[1] - rep.d[1] * al,
                                 w[2] - rep.d[2] * al};
            if (norm3(p) > 0.4) {
                continue;
            }
            // the spans must touch or overlap: the check uses the representative's
            // original span, as in the reference
            if (rep.t1 + 0.5 < entries[i].t0 || entries[i].t1 + 0.5 < rep.t0) {
                continue;
            }
            m.t0 = std::min(m.t0, entries[i].t0);
            m.t1 = std::max(m.t1, entries[i].t1);
            m.full = m.full || entries[i].full;
            for (int k = 0; k < 64; ++k) {
                m.arc[std::size_t(k)] = m.arc[std::size_t(k)] || entries[i].arc[std::size_t(k)];
            }
            unionArc(m.a0, m.a1, entries[i].a0, entries[i].a1);
            placed = true;
            break;
        }
        if (!placed) {
            merged.push_back({i, entries[i].t0, entries[i].t1, entries[i].full, entries[i].arc,
                              entries[i].a0, entries[i].a1});
        }
    }
    std::vector<Entry> fam;
    if (!merged.empty() && merged.size() < entries.size()) {
        for (const Merged& m : merged) {
            Entry e = entries[m.idx];
            e.t0 = m.t0;
            e.t1 = m.t1;
            e.full = m.full;
            e.arc = m.arc;
            e.a0 = m.a0;
            e.a1 = m.a1;
            fam.push_back(e);
        }
    } else {
        fam = entries;
    }

    const std::size_t n = fam.size();
    // DROP: generator azimuths that duplicate a static B-rep seam line (same
    // axis, same radius, axial overlap above the preset threshold, 2 mm at med).
    // The row width is (N,8), as in the reference, so there are at most 8
    // azimuths.
    std::vector<double> drop(n * 8, NAN_);
    if (packedEdges != nullptr && n > 0) {
        const std::size_t floats = packedEdges->raw.size() / sizeof(float);
        if (!packedEdges->raw.empty() && packedEdges->nv >= 2 && floats >= 10) {
            std::vector<float> vf(floats);
            std::memcpy(vf.data(), packedEdges->raw.data(), floats * sizeof(float));
            const std::size_t vcount = floats / 5;
            const std::size_t segCount = vcount / 2;
            std::vector<double> sa(segCount * 3);
            std::vector<double> sb(segCount * 3);
            std::vector<double> su(segCount * 3);
            std::vector<std::size_t> segIdx;
            for (std::size_t j = 0; j < segCount; ++j) {
                double sd[3];
                for (int c = 0; c < 3; ++c) {
                    sa[j * 3 + std::size_t(c)] = static_cast<double>(vf[(2 * j) * 5 + std::size_t(c)]);
                    sb[j * 3 + std::size_t(c)] = static_cast<double>(vf[(2 * j + 1) * 5 + std::size_t(c)]);
                    sd[c] = sb[j * 3 + std::size_t(c)] - sa[j * 3 + std::size_t(c)];
                }
                const double sl = norm3(sd);
                if (!(sl > 1e-6)) {
                    continue;
                }
                for (int c = 0; c < 3; ++c) {
                    su[j * 3 + std::size_t(c)] = sd[c] / sl;
                }
                segIdx.push_back(j);
            }
            std::vector<std::size_t> par;
            std::vector<double> perp;
            std::vector<double> rr;
            std::vector<std::size_t> sel;
            std::vector<double> lo2;
            std::vector<double> hi2;
            std::vector<double> ang;
            for (std::size_t i = 0; i < n; ++i) {
                par.clear();
                for (std::size_t j : segIdx) {
                    if (std::fabs(dot3(&su[j * 3], fam[i].d)) > 0.9993) {
                        par.push_back(j);
                    }
                }
                if (par.empty()) {
                    continue;
                }
                perp.assign(par.size() * 3, 0.0);
                rr.assign(par.size(), 0.0);
                for (std::size_t k = 0; k < par.size(); ++k) {
                    const std::size_t j = par[k];
                    const double w[3] = {sa[j * 3 + 0] - fam[i].o[0], sa[j * 3 + 1] - fam[i].o[1],
                                         sa[j * 3 + 2] - fam[i].o[2]};
                    const double al = dot3(w, fam[i].d);
                    for (int c = 0; c < 3; ++c) {
                        perp[k * 3 + std::size_t(c)] = w[c] - fam[i].d[c] * al;
                    }
                    rr[k] = norm3(&perp[k * 3]);
                }
                sel.clear();
                for (std::size_t k = 0; k < par.size(); ++k) {
                    // 0.35 mm: only edges lying on this cylinder; wider is not
                    // allowed - in a coaxial group it captured the seam of the
                    // neighbouring step
                    if (std::fabs(rr[k] - fam[i].r) < 0.35) {
                        sel.push_back(k);
                    }
                }
                if (sel.empty()) {
                    continue;
                }
                lo2.assign(sel.size(), 0.0);
                hi2.assign(sel.size(), 0.0);
                ang.clear();
                for (std::size_t k = 0; k < sel.size(); ++k) {
                    const std::size_t j = par[sel[k]];
                    const double wa[3] = {sa[j * 3 + 0] - fam[i].o[0], sa[j * 3 + 1] - fam[i].o[1],
                                          sa[j * 3 + 2] - fam[i].o[2]};
                    const double wb[3] = {sb[j * 3 + 0] - fam[i].o[0], sb[j * 3 + 1] - fam[i].o[1],
                                          sb[j * 3 + 2] - fam[i].o[2]};
                    const double ta = dot3(wa, fam[i].d);
                    const double tb = dot3(wb, fam[i].d);
                    lo2[k] = std::min(ta, tb);
                    hi2[k] = std::max(ta, tb);
                    const double ov = std::min(hi2[k], fam[i].t1) - std::max(lo2[k], fam[i].t0);
                    if (!(ov > quality.dropOverlapMm) || !(rr[sel[k]] > 1e-6)) {
                        continue;
                    }
                    ang.push_back(npMod(std::atan2(dot3(&perp[sel[k] * 3], fam[i].e2),
                                                   dot3(&perp[sel[k] * 3], fam[i].e1)), TWO_PI));
                }
                if (ang.empty()) {
                    continue;
                }
                std::sort(ang.begin(), ang.end());
                std::vector<double> uniq;
                for (double a : ang) {
                    if (uniq.empty() || a - uniq.back() > 0.03) {
                        uniq.push_back(a);
                    }
                }
                // a copy of the same seam across zero (0.001 and 6.283): the
                // runtime compares around the circle, so only the first stays
                if (uniq.size() > 1 && uniq[0] + TWO_PI - uniq.back() < 0.03) {
                    uniq.pop_back();
                }
                for (std::size_t k = 0; k < uniq.size() && k < 8; ++k) {
                    drop[i * 8 + k] = uniq[k];
                }
            }
        }
    }

    CylinderFamilies famOut;
    famOut.count = n;
    famOut.O.resize(n * 3);
    famOut.D.resize(n * 3);
    famOut.E1.resize(n * 3);
    famOut.E2.resize(n * 3);
    famOut.R.resize(n);
    famOut.T0.resize(n);
    famOut.T1.resize(n);
    famOut.RREF.resize(n);
    famOut.A0.resize(n);
    famOut.A1.resize(n);
    famOut.FULL.assign(n, 0);
    famOut.ARC.assign(n * 64, 0);
    famOut.DROP = drop;
    for (std::size_t i = 0; i < n; ++i) {
        for (int j = 0; j < 3; ++j) {
            famOut.O[i * 3 + std::size_t(j)] = fam[i].o[j];
            famOut.D[i * 3 + std::size_t(j)] = fam[i].d[j];
            famOut.E1[i * 3 + std::size_t(j)] = fam[i].e1[j];
            famOut.E2[i * 3 + std::size_t(j)] = fam[i].e2[j];
        }
        famOut.R[i] = fam[i].r;
        famOut.T0[i] = fam[i].t0;
        famOut.T1[i] = fam[i].t1;
        famOut.A0[i] = fam[i].a0;
        famOut.A1[i] = fam[i].a1;
        famOut.FULL[i] = fam[i].full ? 1 : 0;
    }

    // Coaxial sections of a stepped hole get a common azimuth: their own tangents
    // diverge by ~0.2 deg, and the contour was drawn as two offset stubs. The
    // reference radius is the largest on this axis.
    for (std::size_t i = 0; i < n; ++i) {
        double best = -1.0;
        for (std::size_t j = 0; j < n; ++j) {
            bool coax = (i == j);
            if (!coax) {
                const double pd = std::fabs(dot3(&famOut.D[i * 3], &famOut.D[j * 3]));
                const double w[3] = {famOut.O[j * 3 + 0] - famOut.O[i * 3 + 0],
                                     famOut.O[j * 3 + 1] - famOut.O[i * 3 + 1],
                                     famOut.O[j * 3 + 2] - famOut.O[i * 3 + 2]};
                const double al = dot3(w, &famOut.D[j * 3]);
                const double p[3] = {w[0] - famOut.D[i * 3 + 0] * al,
                                     w[1] - famOut.D[i * 3 + 1] * al,
                                     w[2] - famOut.D[i * 3 + 2] * al};
                coax = (pd > 0.999) && (norm3(p) < 0.4);
            }
            if (coax) {
                best = std::max(best, famOut.R[j]);
            }
        }
        famOut.RREF[i] = best;
    }

    // Azimuthal coverage: the mesh mask is limited by the analytic face arc, but
    // only for partial arcs - a full hole is not cut.
    if (skin != nullptr && skin->valid && n > 0) {
        std::vector<double> pp(skin->count * 9);    // (F,3,3): vertex minus the axial part
        std::vector<double> alv(skin->count * 3);   // (F,3): the same offsets along the axis
        for (std::size_t i = 0; i < n; ++i) {
            if (!fam[i].full) {
                continue;
            }
            const double* u = fam[i].d;
            for (std::size_t f = 0; f < skin->count; ++f) {
                for (int c = 0; c < 3; ++c) {
                    const double* vx = &skin->sktri[f * 9 + std::size_t(c) * 3];
                    double wv[3];
                    for (int k = 0; k < 3; ++k) {
                        wv[k] = vx[k] - fam[i].o[k];
                    }
                    const double along = dot3(wv, u);
                    alv[f * 3 + std::size_t(c)] = along;
                    for (int k = 0; k < 3; ++k) {
                        pp[f * 9 + std::size_t(c) * 3 + std::size_t(k)] = wv[k] - u[k] * along;
                    }
                }
            }
            for (std::size_t f = 0; f < skin->count; ++f) {
                bool onTri = true;
                double amin = alv[f * 3];
                double amax = alv[f * 3];
                for (int c = 0; c < 3; ++c) {
                    // all three vertices are on this face's radius: the triangle
                    // lies on the cylinder itself, not on a neighbour
                    if (!(std::fabs(norm3(&pp[f * 9 + std::size_t(c) * 3]) - fam[i].r) < 0.25)) {
                        onTri = false;
                    }
                    amin = std::min(amin, alv[f * 3 + std::size_t(c)]);
                    amax = std::max(amax, alv[f * 3 + std::size_t(c)]);
                }
                if (!onTri || !(amin > fam[i].t0 - 0.5) || !(amax < fam[i].t1 + 0.5)) {
                    continue;
                }
                double cnt[3];
                for (int k = 0; k < 3; ++k) {
                    cnt[k] = (pp[f * 9 + std::size_t(k)] + pp[f * 9 + 3 + std::size_t(k)]
                              + pp[f * 9 + 6 + std::size_t(k)]) / 3.0;
                }
                const double a = npMod(std::atan2(dot3(cnt, fam[i].e2), dot3(cnt, fam[i].e1)),
                                       TWO_PI);
                famOut.ARC[i * 64 + static_cast<std::size_t>(a / TWO_PI * 64.0) % 64] = 1;
            }
        }
        for (std::size_t i = 0; i < n; ++i) {
            if (!fam[i].full) {
                continue;
            }
            int msum = 0;
            int asum = 0;
            for (int k = 0; k < 64; ++k) {
                msum += famOut.ARC[i * 64 + std::size_t(k)];
                asum += fam[i].arc[std::size_t(k)] ? 1 : 0;
            }
            if (msum >= 56) {
                continue;                       // a full hole - do not cut
            }
            if (asum >= 4) {
                int cut = 0;
                for (int k = 0; k < 64; ++k) {
                    if (famOut.ARC[i * 64 + std::size_t(k)] && fam[i].arc[std::size_t(k)]) {
                        ++cut;
                    }
                }
                for (int k = 0; k < 64; ++k) {
                    const bool keep = cut > 0
                        ? (famOut.ARC[i * 64 + std::size_t(k)] && fam[i].arc[std::size_t(k)])
                        : fam[i].arc[std::size_t(k)];
                    famOut.ARC[i * 64 + std::size_t(k)] = keep ? 1 : 0;
                }
            }
        }
        // a continuous arc (a0, a0 + span) for exact trimming at sliding angles;
        // full holes (a ~360 deg mask) are marked NaN - they are not cut
        for (std::size_t i = 0; i < n; ++i) {
            int msum = 0;
            for (int k = 0; k < 64; ++k) {
                msum += famOut.ARC[i * 64 + std::size_t(k)];
            }
            if (msum >= 56) {
                famOut.A0[i] = NAN_;
                famOut.A1[i] = NAN_;
            }
        }
    }

    out.cylinders = std::move(famOut);
    // The cone policy, then welding of coincident ones: the order is as in the
    // reference.
    std::vector<ConeSurface> kept;
    kept.reserve(cones.size());
    for (const ConeSurface& c : cones) {
        if (coneKeepsSilhouette(c, skin, quality)) {
            kept.push_back(c);
        }
    }
    out.cones = dedupeCones(kept);
    return out;
}

TangentFamilies profileTangentFamilies(const std::vector<ProfilePoint>& profile)
{
    TangentFamilies out;
    if (profile.size() < 2) {
        return out;
    }
    // A profile section: either a cylinder (constant radius) or a cone (radius
    // changes linearly). A flat face (dy = 0) and a stretch on the axis give no
    // generators - the ring draws them.
    struct Section {
        bool cylinder = true;
        double r = 0.0;
        double t0 = 0.0;
        double t1 = 0.0;
        ConeSurface cone;
    };
    const double eps = 1e-9;
    std::vector<Section> sections;
    for (std::size_t i = 0; i + 1 < profile.size(); ++i) {
        const double r0 = profile[i].radius_mm;
        const double y0 = profile[i].height_mm;
        const double r1 = profile[i + 1].radius_mm;
        const double y1 = profile[i + 1].height_mm;
        const double dr = r1 - r0;
        const double dy = y1 - y0;
        if (std::fabs(dy) < eps || (r0 <= eps && r1 <= eps)) {
            continue;
        }
        Section s;
        if (std::fabs(dr) < eps) {
            s.r = r0;
            s.t0 = std::min(y0, y1);
            s.t1 = std::max(y0, y1);
            // Adjacent cylinders of one radius (the body and the neck on one
            // diameter) give one shared generator, not two overlaid lines.
            if (!sections.empty() && sections.back().cylinder
                    && std::fabs(sections.back().r - s.r) < eps
                    && std::fabs(sections.back().t1 - s.t0) < 1e-6) {
                sections.back().t1 = s.t1;
            } else {
                sections.push_back(s);
            }
            continue;
        }
        // The cone axis looks from the apex to the section: the radius grows along
        // it, so the parameter s of both ends is positive, as in ConeSurface from
        // STEP.
        const double sgn = dr > 0.0 ? 1.0 : -1.0;
        const double yApex = y0 - r0 * dy / dr;
        const double psi = std::atan(std::fabs(dr / dy));
        const double cosPsi = std::cos(psi);
        if (!(cosPsi > eps) || !std::isfinite(yApex)) {
            continue;
        }
        s.cylinder = false;
        s.cone.a[1] = yApex;
        // The axis is set in full: ConeSurface by default carries the axis
        // (0, 0, 1), and from a single d[1] record the axis drifts to (0, sgn, 1) -
        // the generator would land at a radius of ~38 mm instead of the real
        // 11.5..23 (measured on the holder).
        s.cone.d[0] = 0.0;
        s.cone.d[1] = sgn;
        s.cone.d[2] = 0.0;
        s.cone.psi = psi;
        s.cone.s0 = (y0 - yApex) * sgn / cosPsi;
        s.cone.s1 = (y1 - yApex) * sgn / cosPsi;
        sections.push_back(s);
    }

    std::size_t ncyl = 0;
    for (const Section& s : sections) {
        if (s.cylinder) {
            ++ncyl;
        }
    }
    CylinderFamilies& cyls = out.cylinders;
    cyls.count = ncyl;
    cyls.O.assign(ncyl * 3, 0.0);
    cyls.D.assign(ncyl * 3, 0.0);
    cyls.E1.assign(ncyl * 3, 0.0);
    cyls.E2.assign(ncyl * 3, 0.0);
    cyls.R.assign(ncyl, 0.0);
    cyls.T0.assign(ncyl, 0.0);
    cyls.T1.assign(ncyl, 0.0);
    cyls.RREF.assign(ncyl, 0.0);
    cyls.A0.assign(ncyl, NAN_);
    cyls.A1.assign(ncyl, NAN_);
    cyls.FULL.assign(ncyl, 1);
    cyls.DROP.assign(ncyl * 8, NAN_);
    cyls.ARC.assign(ncyl * 64, 0);
    std::size_t ic = 0;
    for (const Section& s : sections) {
        if (!s.cylinder) {
            out.cones.push_back(s.cone);
            continue;
        }
        cyls.D[ic * 3 + 1] = 1.0;
        cyls.E1[ic * 3 + 0] = 1.0;
        cyls.E2[ic * 3 + 2] = 1.0;
        cyls.R[ic] = s.r;
        cyls.T0[ic] = s.t0;
        cyls.T1[ic] = s.t1;
        cyls.RREF[ic] = s.r;
        ++ic;
    }
    return out;
}

const std::vector<double>& tangentFromFamilies(const TangentFamilies& fam, const double eyeMm[3],
                                               TangentScratch& scratch) {
    scratch.segments.clear();
    // Aliases to the buffers of the frame-surviving object: the body below does
    // not change, only the places where these vectors are created.
    std::vector<double>& along = scratch.along;
    std::vector<double>& perp = scratch.perp;
    std::vector<double>& rp = scratch.rp;
    std::vector<double>& mh = scratch.mh;
    std::vector<double>& rot = scratch.rot;
    std::vector<double>& nrm = scratch.nrm;
    std::vector<double>& cg = scratch.cg;
    std::vector<double>& sg = scratch.sg;
    std::vector<double>& phi = scratch.phi;
    std::vector<double>& baseA = scratch.baseA;
    std::vector<double>& baseB = scratch.baseB;
    std::vector<std::uint8_t>& ok = scratch.ok;
    std::vector<double>& segments = scratch.segments;
    const CylinderFamilies& cyls = fam.cylinders;
    const std::size_t n = cyls.count;
    if (n > 0) {
        along.assign(n, 0.0);
        perp.assign(n * 3, 0.0);
        rp.assign(n, 0.0);
        mh.assign(n * 3, 0.0);
        rot.assign(n * 3, 0.0);
        nrm.assign(n * 3, 0.0);
        cg.assign(n, 0.0);
        sg.assign(n, 0.0);
        bool anyValid = false;
        for (std::size_t i = 0; i < n; ++i) {
            const double w[3] = {eyeMm[0] - cyls.O[i * 3 + 0], eyeMm[1] - cyls.O[i * 3 + 1],
                                 eyeMm[2] - cyls.O[i * 3 + 2]};
            along[i] = dot3(&cyls.D[i * 3], w);
            for (int c = 0; c < 3; ++c) {
                perp[i * 3 + std::size_t(c)] = w[c] - cyls.D[i * 3 + std::size_t(c)] * along[i];
            }
            rp[i] = norm3(&perp[i * 3]);
            anyValid = anyValid || (rp[i] > cyls.R[i] + 1e-6);
        }
        if (anyValid) {
            for (std::size_t i = 0; i < n; ++i) {
                // the tangency azimuth follows the largest radius of a coaxial
                // group: the sections of a stepped hole land on one contour
                const double rref = cyls.RREF[i];
                const double rpSafe = std::max(rp[i], 1e-9);
                const double rang = rp[i] > rref ? rref : cyls.R[i];
                double c = rang / rpSafe;
                c = std::min(1.0, std::max(-1.0, c));
                cg[i] = c;
                sg[i] = std::sqrt(std::max(1.0 - c * c, 0.0));
                const double div = std::max(rp[i], 1e-9);
                for (int k = 0; k < 3; ++k) {
                    mh[i * 3 + std::size_t(k)] = perp[i * 3 + std::size_t(k)] / div;
                }
                cross3(&cyls.D[i * 3], &mh[i * 3], &rot[i * 3]);
            }
            phi.assign(n, 0.0);
            baseA.assign(n * 3, 0.0);
            baseB.assign(n * 3, 0.0);
            for (std::size_t i = 0; i < n; ++i) {
                for (int k = 0; k < 3; ++k) {
                    baseA[i * 3 + std::size_t(k)] = cyls.O[i * 3 + std::size_t(k)]
                        + cyls.D[i * 3 + std::size_t(k)] * cyls.T0[i];
                    baseB[i * 3 + std::size_t(k)] = cyls.O[i * 3 + std::size_t(k)]
                        + cyls.D[i * 3 + std::size_t(k)] * cyls.T1[i];
                }
            }
            const double tol = 2.0 * DEG_TO_RAD;
            for (int side = 0; side < 2; ++side) {
                const double sgn = side == 0 ? 1.0 : -1.0;
                for (std::size_t i = 0; i < n; ++i) {
                    for (int k = 0; k < 3; ++k) {
                        nrm[i * 3 + std::size_t(k)] = mh[i * 3 + std::size_t(k)] * cg[i]
                            + rot[i * 3 + std::size_t(k)] * (sgn * sg[i]);
                    }
                    phi[i] = npMod(std::atan2(dot3(&nrm[i * 3], &cyls.E2[i * 3]),
                                              dot3(&nrm[i * 3], &cyls.E1[i * 3])), TWO_PI);
                }
                ok.assign(n, 0);
                for (std::size_t i = 0; i < n; ++i) {
                    ok[i] = cyls.FULL[i];
                    // trimming by the analytic arc (a0..a1): without it the line
                    // runs past the blend edge onto a flat face
                    const bool part = std::isfinite(cyls.A0[i]);
                    if (part) {
                        const double rel = npMod(phi[i] - cyls.A0[i], TWO_PI);
                        const double span = npMod(cyls.A1[i] - cyls.A0[i], TWO_PI);
                        const bool arcok = (rel <= span + tol) || (rel >= TWO_PI - tol);
                        if (!arcok) {
                            ok[i] = 0;
                        }
                    }
                }
                // a generator duplicating a B-rep seam edge: a band about 1.5 mm
                // wide by radius
                for (std::size_t i = 0; i < n; ++i) {
                    if (!ok[i]) {
                        continue;
                    }
                    bool anyDrop = false;
                    for (int k = 0; k < 8; ++k) {
                        if (std::isfinite(cyls.DROP[i * 8 + std::size_t(k)])) {
                            anyDrop = true;
                            break;
                        }
                    }
                    if (!anyDrop) {
                        continue;
                    }
                    const double band = std::max(std::atan(1.5 / std::max(cyls.R[i], 0.05)), 0.025);
                    for (int k = 0; k < 8; ++k) {
                        const double dphi = std::fabs(npMod(phi[i] - cyls.DROP[i * 8 + std::size_t(k)]
                                                            + PI, TWO_PI) - PI);
                        if (std::isfinite(dphi) && dphi < band) {
                            ok[i] = 0;
                            break;
                        }
                    }
                }
                for (std::size_t i = 0; i < n; ++i) {
                    if (!(rp[i] > cyls.R[i] + 1e-6) || !ok[i]) {
                        continue;
                    }
                    double pa[3];
                    double pb[3];
                    for (int k = 0; k < 3; ++k) {
                        pa[k] = baseA[i * 3 + std::size_t(k)]
                            + nrm[i * 3 + std::size_t(k)] * cyls.R[i];
                        pb[k] = baseB[i * 3 + std::size_t(k)]
                            + nrm[i * 3 + std::size_t(k)] * cyls.R[i];
                    }
                    for (int k = 0; k < 3; ++k) {
                        segments.push_back(pa[k]);
                    }
                    for (int k = 0; k < 3; ++k) {
                        segments.push_back(pb[k]);
                    }
                }
            }
        }
    }
    for (const ConeSurface& s : fam.cones) {
        double sd[3] = {s.d[0], s.d[1], s.d[2]};
        const double dn = norm3(sd);
        if (dn <= 1e-12) {
            continue;
        }
        for (int k = 0; k < 3; ++k) {
            sd[k] /= dn;
        }
        const double psi = s.psi;
        const double w[3] = {eyeMm[0] - s.a[0], eyeMm[1] - s.a[1], eyeMm[2] - s.a[2]};
        const double wlen = norm3(w);
        if (wlen < 1e-6 || std::fabs(std::cos(psi)) <= 1e-6) {
            continue;
        }
        const double wh[3] = {w[0] / wlen, w[1] / wlen, w[2] / wlen};
        const double ct = dot3(sd, wh);
        if (std::fabs(ct) >= 1.0) {
            continue;
        }
        const double st = std::sqrt(std::max(1.0 - ct * ct, 1e-12));
        double u1[3];
        for (int k = 0; k < 3; ++k) {
            u1[k] = (wh[k] - sd[k] * ct) / st;
        }
        double u2[3];
        cross3(sd, u1, u2);
        const double cb = ct / std::cos(psi);
        if (std::fabs(cb) > 1.0) {
            continue;
        }
        const double den = std::max(st * std::sin(psi), 1e-9);
        const double cf = (cb - ct * std::cos(psi)) / den;
        if (std::fabs(cf) > 1.0) {
            continue;
        }
        const double sf = std::sqrt(std::max(1.0 - cf * cf, 0.0));
        for (int side = 0; side < 2; ++side) {
            const double sgn = side == 0 ? 1.0 : -1.0;
            double gdir[3];
            for (int k = 0; k < 3; ++k) {
                gdir[k] = sd[k] * std::cos(psi)
                    + (u1[k] * cf + u2[k] * (sgn * sf)) * std::sin(psi);
            }
            double pa[3];
            double pb[3];
            for (int k = 0; k < 3; ++k) {
                pa[k] = s.a[k] + gdir[k] * s.s0;
                pb[k] = s.a[k] + gdir[k] * s.s1;
            }
            for (int k = 0; k < 3; ++k) {
                segments.push_back(pa[k]);
            }
            for (int k = 0; k < 3; ++k) {
                segments.push_back(pb[k]);
            }
        }
    }
    return scratch.segments;
}

std::vector<double> tangentFromFamilies(const TangentFamilies& fam, const double eyeMm[3]) {
    TangentScratch scratch;
    tangentFromFamilies(fam, eyeMm, scratch);
    return std::move(scratch.segments);   // move, not copy: as the former return of segments
}

} // namespace gv
