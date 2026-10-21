#include "occt/step_edges.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

#include <TopExp_Explorer.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <GeomAbs_CurveType.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <GCPnts_UniformDeflection.hxx>
#include <gp_Pnt.hxx>

namespace gv {
namespace {

using Pnt3 = std::array<double, 3>;
using Segment = EdgeSegment;

inline double dot(const Pnt3 &x, const Pnt3 &y)
{
    return x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
}

inline Pnt3 cross(const Pnt3 &x, const Pnt3 &y)
{
    return { x[1] * y[2] - x[2] * y[1],
             x[2] * y[0] - x[0] * y[2],
             x[0] * y[1] - x[1] * y[0] };
}

inline double norm(const Pnt3 &x)
{
    return std::sqrt(x[0] * x[0] + x[1] * x[1] + x[2] * x[2]);
}

// Round to 0.01 mm half to even: numpy.round in the reference does the same.
double round2(double v)
{
    const double r = std::nearbyint(v * 100.0) / 100.0;
    return r == 0.0 ? 0.0 : r;            // -0.0 and 0.0 are one key
}

// Straight-edge key: a pair of ends rounded to 0.01 mm, in ascending order.
// Mirrors _line_key from occt/step_import.cpp (sorting triples as tuples).
struct LineKey {
    Pnt3 lo{};
    Pnt3 hi{};

    bool operator==(const LineKey &o) const
    {
        return lo == o.lo && hi == o.hi;
    }
};

struct LineKeyHash {
    std::size_t operator()(const LineKey &k) const
    {
        std::size_t h = 1469598103934665603ull;
        for (int e = 0; e < 2; ++e) {
            const Pnt3 &p = e ? k.hi : k.lo;
            for (int i = 0; i < 3; ++i) {
                std::uint64_t bits = 0;
                std::memcpy(&bits, &p[i], sizeof(bits));
                h = (h ^ bits) * 1099511628211ull;
            }
        }
        return h;
    }
};

// std::hash is not defined for std::array: grid-cell keys are hashed themselves.
template <std::size_t N>
struct IntTupleHash {
    std::size_t operator()(const std::array<long long, N> &k) const
    {
        std::size_t h = 1469598103934665603ull;
        for (std::size_t i = 0; i < N; ++i)
            h = (h ^ std::size_t(k[i])) * 1099511628211ull;
        return h;
    }
};

bool lineKey(const BRepAdaptor_Curve &crv, LineKey &key)
{
    if (crv.GetType() != GeomAbs_Line)
        return false;
    gp_Pnt p0, p1;
    const double f = crv.FirstParameter();
    const double l = crv.LastParameter();
    crv.D0(f, p0);
    crv.D0(l, p1);
    const Pnt3 a{ round2(p0.X()), round2(p0.Y()), round2(p0.Z()) };
    const Pnt3 b{ round2(p1.X()), round2(p1.Y()), round2(p1.Z()) };
    if (std::lexicographical_compare(b.begin(), b.end(), a.begin(), a.end())) {
        key.lo = b;
        key.hi = a;
    } else {
        key.lo = a;
        key.hi = b;
    }
    return true;
}

// Shape edges as segments: TopAbs_EDGE traversal, each edge's polyline via
// GCPnts_UniformDeflection (linear tolerance, mm). Seam lines are dropped: a
// straight edge whose adjacent faces are all curved (seams and generators of
// cylinders and cones, including seams between counterbore arcs) - this is not a
// model edge, SolidWorks does not draw them either. A straight edge that touches
// a plane with at least one face (a flat wall transitioning into a fillet, 90
// degrees) stays an edge.
std::vector<Segment> buildSegments(const TopoDS_Shape &shape, double deflection,
                                   bool dropSeams)
{
    std::unordered_map<LineKey, bool, LineKeyHash> lineAllCurved;
    if (dropSeams) {
        for (TopExp_Explorer expf(shape, TopAbs_FACE); expf.More(); expf.Next()) {
            const TopoDS_Face face = TopoDS::Face(expf.Current());
            const bool curved =
                BRepAdaptor_Surface(face).GetType() != GeomAbs_Plane;
            for (TopExp_Explorer expe(face, TopAbs_EDGE); expe.More(); expe.Next()) {
                LineKey key;
                if (!lineKey(BRepAdaptor_Curve(TopoDS::Edge(expe.Current())), key))
                    continue;
                const auto it = lineAllCurved.find(key);
                if (it == lineAllCurved.end())
                    lineAllCurved.emplace(key, curved);
                else
                    it->second = it->second && curved;
            }
        }
    }

    std::vector<Segment> out;
    out.reserve(8192);
    for (TopExp_Explorer exp(shape, TopAbs_EDGE); exp.More(); exp.Next()) {
        const TopoDS_Edge edge = TopoDS::Edge(exp.Current());
        BRepAdaptor_Curve crv(edge);
        if (dropSeams) {
            LineKey key;
            if (lineKey(crv, key)) {
                const auto it = lineAllCurved.find(key);
                if (it != lineAllCurved.end() && it->second)
                    continue;                // seam line: not a model edge
            }
        }
        GCPnts_UniformDeflection sampler(crv, deflection);
        if (!sampler.IsDone() || sampler.NbPoints() < 2)
            continue;
        gp_Pnt prev = sampler.Value(1);
        for (int i = 2; i <= sampler.NbPoints(); ++i) {
            const gp_Pnt p = sampler.Value(i);
            const Pnt3 a{ prev.X(), prev.Y(), prev.Z() };
            const Pnt3 b{ p.X(), p.Y(), p.Z() };
            // Coincident polyline points do not become a segment.
            if (std::fabs(a[0] - b[0]) + std::fabs(a[1] - b[1])
                    + std::fabs(a[2] - b[2]) > 1e-9) {
                out.push_back({ a, b });
            }
            prev = p;
        }
    }
    return out;
}

// Welding of nearly coincident straight segments: mirrors _coalesce_segments
// (core/packing.cpp). STEP files (especially assemblies) carry the same geometry
// as several different TShape edges, and the outline draws it twice; copies
// shifted by 0.02-0.1 mm read as "double" lines. Segments are put into direction
// buckets, inside a bucket into clusters by distance from the perpendicular (a
// 0.12 mm grid), and the axial intervals of a cluster are merged.
std::vector<Segment> coalesceSegments(const std::vector<Segment> &segments,
                                      double distTol, double gapTol)
{
    std::vector<Segment> items;
    items.reserve(segments.size());
    for (const Segment &s : segments) {
        const Pnt3 d{ s.b[0] - s.a[0], s.b[1] - s.a[1], s.b[2] - s.a[2] };
        if (norm(d) > 1e-6)
            items.push_back(s);
    }
    const int m = int(items.size());
    if (m == 0)
        return {};

    // Direction with a canonical sign: the first non-zero component > 0.
    std::vector<Pnt3> dir(m);
    for (int i = 0; i < m; ++i) {
        const Pnt3 d{ items[i].b[0] - items[i].a[0],
                      items[i].b[1] - items[i].a[1],
                      items[i].b[2] - items[i].a[2] };
        const double len = norm(d);
        Pnt3 u{ d[0] / len, d[1] / len, d[2] / len };
        int first = 0;
        while (first < 2 && std::fabs(u[first]) <= 1e-6)
            ++first;
        if (u[first] < 0.0)
            u = { -u[0], -u[1], -u[2] };
        dir[i] = u;
    }

    // Buckets by direction rounded to 0.001; the order is like a Python dict: by
    // first appearance in the segment list.
    std::vector<std::vector<int>> buckets;
    std::unordered_map<std::array<long long, 3>, int, IntTupleHash<3>> bucketOf;
    for (int i = 0; i < m; ++i) {
        std::array<long long, 3> key{};
        for (int k = 0; k < 3; ++k) {
            const double r = std::nearbyint(dir[i][k] * 1000.0);
            key[k] = static_cast<long long>(r == 0.0 ? 0.0 : r);
        }
        const auto it = bucketOf.find(key);
        if (it == bucketOf.end()) {
            bucketOf.emplace(key, int(buckets.size()));
            buckets.emplace_back();
            buckets.back().push_back(i);
        } else {
            buckets[it->second].push_back(i);
        }
    }

    struct Cluster {
        double fx, fy, lo, hi;
        int count;
    };

    std::vector<Segment> out;
    out.reserve(items.size());
    for (const std::vector<int> &idxs : buckets) {
        if (idxs.size() == 1) {
            out.push_back(items[idxs[0]]);       // a bucket of one segment: as is
            continue;
        }
        Pnt3 u{ 0.0, 0.0, 0.0 };
        for (int i : idxs)
            for (int k = 0; k < 3; ++k)
                u[k] += dir[i][k];
        const double ul = norm(u);
        u = { u[0] / ul, u[1] / ul, u[2] / ul };

        Pnt3 e1 = std::fabs(u[2]) < 0.9 ? Pnt3{ 0.0, 0.0, 1.0 } : Pnt3{ 1.0, 0.0, 0.0 };
        const double e1d = dot(e1, u);
        e1 = { e1[0] - u[0] * e1d, e1[1] - u[1] * e1d, e1[2] - u[2] * e1d };
        const double e1l = norm(e1);
        e1 = { e1[0] / e1l, e1[1] / e1l, e1[2] / e1l };
        const Pnt3 e2 = cross(u, e1);

        const int c = int(idxs.size());
        std::vector<double> lo0(c), hi0(c), fx(c), fy(c);
        for (int j = 0; j < c; ++j) {
            const Pnt3 &a = items[idxs[j]].a;
            const Pnt3 &b = items[idxs[j]].b;
            const double ta = dot(a, u);
            const double tb = dot(b, u);
            lo0[j] = std::min(ta, tb);
            hi0[j] = std::max(ta, tb);
            const Pnt3 footA{ a[0] - u[0] * ta, a[1] - u[1] * ta, a[2] - u[2] * ta };
            fx[j] = dot(footA, e1);
            fy[j] = dot(footA, e2);
        }

        // Clustering is greedy, in ascending axial start order: candidates lie in
        // the 3x3 neighbourhood of grid cells (a cluster is no farther than
        // distTol from its base).
        std::vector<int> order(c);
        for (int j = 0; j < c; ++j)
            order[j] = j;
        std::stable_sort(order.begin(), order.end(),
                         [&lo0](int x, int y) { return lo0[x] < lo0[y]; });

        std::vector<Cluster> members;
        std::unordered_map<std::array<long long, 2>, std::vector<int>,
                           IntTupleHash<2>> cells;
        for (int j : order) {
            const long long cx = static_cast<long long>(std::floor(fx[j] / distTol));
            const long long cy = static_cast<long long>(std::floor(fy[j] / distTol));
            int best = -1;
            double bestd = distTol;
            for (long long gx = cx - 1; gx <= cx + 1; ++gx) {
                for (long long gy = cy - 1; gy <= cy + 1; ++gy) {
                    const auto cit = cells.find({ gx, gy });
                    if (cit == cells.end())
                        continue;
                    for (int ci : cit->second) {
                        const double dx = fx[j] - members[ci].fx;
                        const double dy = fy[j] - members[ci].fy;
                        const double dd = std::hypot(dx, dy);
                        if (dd >= bestd)
                            continue;
                        if (lo0[j] - members[ci].hi > gapTol
                                || hi0[j] + gapTol < members[ci].lo) {
                            continue;
                        }
                        best = ci;
                        bestd = dd;
                    }
                }
            }
            if (best < 0) {
                members.push_back({ fx[j], fy[j], lo0[j], hi0[j], 1 });
                cells[{ cx, cy }].push_back(int(members.size()) - 1);
            } else {
                Cluster &cl = members[best];
                const int n = cl.count + 1;
                cl.fx = (cl.fx * cl.count + fx[j]) / n;
                cl.fy = (cl.fy * cl.count + fy[j]) / n;
                cl.lo = std::min(cl.lo, lo0[j]);
                cl.hi = std::max(cl.hi, hi0[j]);
                cl.count = n;
                // The average base drifts as members join: the cluster must also
                // be found from the cell of its current average.
                const long long mx =
                    static_cast<long long>(std::floor(cl.fx / distTol));
                const long long my =
                    static_cast<long long>(std::floor(cl.fy / distTol));
                if (mx != cx || my != cy) {
                    std::vector<int> &lst = cells[{ mx, my }];
                    if (std::find(lst.begin(), lst.end(), best) == lst.end())
                        lst.push_back(best);
                }
            }
        }
        for (const Cluster &cl : members) {
            const Pnt3 foot{ e1[0] * cl.fx + e2[0] * cl.fy,
                             e1[1] * cl.fx + e2[1] * cl.fy,
                             e1[2] * cl.fx + e2[2] * cl.fy };
            out.push_back({ { foot[0] + u[0] * cl.lo, foot[1] + u[1] * cl.lo,
                              foot[2] + u[2] * cl.lo },
                            { foot[0] + u[0] * cl.hi, foot[1] + u[1] * cl.hi,
                              foot[2] + u[2] * cl.hi } });
        }
    }
    return out;
}

} // namespace

std::vector<EdgeSegment> stepRawSegments(const TopoDS_Shape &shape, double deflection,
                                         bool dropSeams)
{
    return buildSegments(shape, deflection, dropSeams);
}

std::vector<EdgeSegment> coalesceStepSegments(const std::vector<EdgeSegment> &segments,
                                              double distTol, double gapTol)
{
    return coalesceSegments(segments, distTol, gapTol);
}

} // namespace gv
