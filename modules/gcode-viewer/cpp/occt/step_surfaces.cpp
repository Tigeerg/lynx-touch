// occt/step_surfaces.cpp - mirrors load_step_surfaces from occt/step_surfaces.cpp.
//
// The face traversal order and the order of operations are part of the result:
// the descriptors are checked against the reference bit for bit, so the file is
// compiled with /fp:precise (MSVC) and -ffp-contract=off (GCC/Clang).
#include "occt/step_surfaces.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <BRepAdaptor_Surface.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax1.hxx>
#include <gp_Cone.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>

#include "core/np_helpers.h"
#include "occt/step_import.h"

namespace gv {
namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double TWO_PI = 2.0 * PI;

// Dot product, cross product and remainder by divisor sign - the shared bodies
// are in core/np_helpers.h.
using gv::np::cross3;
using gv::np::dot3;
using gv::np::npMod;

// Norm of a 3-vector: in the reference this is (sum of squares) ** 0.5, i.e. pow,
// not sqrt: on values around one they differ in the low bit, and this shows in
// the normalized axis direction. The body is shared in core/np_helpers.h.
inline double norm3(const double* v) {
    return gv::np::norm3Pow(v);
}

// round(a, 6) in the reference: round half to even to the sixth decimal. The
// values are azimuths in [0, 2pi), the result goes straight into arc0/arc1, so
// the rounding must match the reference bit for bit.
inline double round6(double x) {
    return std::nearbyint(x * 1e6) / 1e6;
}

// Normal of a 3-vector: dv = direction - origin, where origin is (0, 0, 0), i.e.
// direction itself; degeneracy is cut by the 1e-12 threshold, as in the
// reference.
void unitDir(const gp_Dir& dir, double out[3]) {
    const double dv[3] = {dir.X(), dir.Y(), dir.Z()};
    const double len = std::max(norm3(dv), 1e-12);
    for (int k = 0; k < 3; ++k) {
        out[k] = dv[k] / len;
    }
}

} // namespace

StepSurfaceList loadStepSurfaces(const TopoDS_Shape& shape, double deflection,
                                 bool shapeAlreadyMeshed) {
    StepSurfaceList out;
    try {
        // Tessellation is needed by the reference for the face-node branch. The
        // parameters are those of load_step_model, so on an already meshed shape
        // the repeated pass does nothing; it is called to keep the path the same
        // as the reference. shapeAlreadyMeshed=true (the loadStepBundleAll path)
        // skips the step: the shape was meshed above with the same call and the
        // same parameters.
        if (!shapeAlreadyMeshed) {
            BRepMesh_IncrementalMesh mesher(shape, deflection, false, deflection * 2.0, true);
            mesher.Perform();
        }
    } catch (const Standard_Failure&) {
        return out;
    }

    for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
        try {
            const TopoDS_Face face = TopoDS::Face(exp.Current());
            BRepAdaptor_Surface surf(face);
            const GeomAbs_SurfaceType st = surf.GetType();
            if (st != GeomAbs_Cylinder && st != GeomAbs_Cone) {
                continue;
            }

            // Face points are its vertices (BRep_Tool::Pnt already returns them in
            // assembly coordinates, accounting for the face location). The
            // triangulation branch in the reference is unreachable: the OCP
            // wrapper of Poly_Triangulation has no IsNull method, so
            // getattr(ftri, "IsNull", lambda: True) gives True, and the condition
            // `ftri is not None and not IsNull and NbNodes > 0` is false. The
            // check confirms it: the reference builds a half-cylinder arc from two
            // vertices (azimuths 0 and pi), not from mesh nodes - nodes would give
            // a different answer for an arc longer than 180 deg.
            std::vector<double> pts;
            for (TopExp_Explorer vx(face, TopAbs_VERTEX); vx.More(); vx.Next()) {
                try {
                    const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(vx.Current()));
                    pts.push_back(p.X());
                    pts.push_back(p.Y());
                    pts.push_back(p.Z());
                } catch (const Standard_Failure&) {
                    // a vertex that failed to parse is skipped
                }
            }
            if (pts.empty()) {
                continue;
            }
            const std::size_t np = pts.size() / 3;

            if (st == GeomAbs_Cylinder) {
                const gp_Cylinder cyl = surf.Cylinder();
                const gp_Ax1 axis = cyl.Axis();
                const gp_Pnt lo = axis.Location();
                double dn[3];
                unitDir(axis.Direction(), dn);
                const double o[3] = {lo.X(), lo.Y(), lo.Z()};

                double t0 = 0.0;
                double t1 = 0.0;
                for (std::size_t i = 0; i < np; ++i) {
                    const double q[3] = {pts[i * 3] - o[0], pts[i * 3 + 1] - o[1],
                                         pts[i * 3 + 2] - o[2]};
                    const double tl = dot3(q, dn);
                    if (i == 0 || tl < t0) {
                        t0 = tl;
                    }
                    if (i == 0 || tl > t1) {
                        t1 = tl;
                    }
                }

                // Cross-section basis: the first node whose transverse part is not
                // degenerate sets e1; e2 is the cross product with the axis.
                double e1[3] = {1.0, 0.0, 0.0};
                bool haveE1 = false;
                for (std::size_t i = 0; i < np && !haveE1; ++i) {
                    const double vq[3] = {pts[i * 3] - o[0], pts[i * 3 + 1] - o[1],
                                          pts[i * 3 + 2] - o[2]};
                    const double al = dot3(vq, dn);
                    const double pv[3] = {vq[0] - dn[0] * al, vq[1] - dn[1] * al,
                                          vq[2] - dn[2] * al};
                    const double rp = norm3(pv);
                    if (rp > 1e-6) {
                        for (int k = 0; k < 3; ++k) {
                            e1[k] = pv[k] / rp;
                        }
                        haveE1 = true;
                    }
                }
                double e2[3] = {0.0, 0.0, 1.0};
                if (haveE1) {
                    cross3(dn, e1, e2);
                }

                // Angular arc of the face: most "cylinders" in STEP are arcs
                // (fillets, opened holes), and a generator outside the arc would
                // hang over the void.
                std::vector<double> rawAng;
                if (haveE1) {
                    for (std::size_t i = 0; i < np; ++i) {
                        const double vq[3] = {pts[i * 3] - o[0], pts[i * 3 + 1] - o[1],
                                              pts[i * 3 + 2] - o[2]};
                        const double al = dot3(vq, dn);
                        const double pv[3] = {vq[0] - dn[0] * al, vq[1] - dn[1] * al,
                                              vq[2] - dn[2] * al};
                        if (norm3(pv) > 1e-6) {
                            rawAng.push_back(npMod(std::atan2(dot3(pv, e2), dot3(pv, e1)),
                                                   TWO_PI));
                        }
                    }
                }
                CylinderSurface cs;
                cs.has_arc = rawAng.size() >= 2;
                if (cs.has_arc) {
                    std::vector<double> sa;
                    sa.reserve(rawAng.size());
                    for (double a : rawAng) {
                        sa.push_back(round6(a));
                    }
                    std::sort(sa.begin(), sa.end());
                    sa.erase(std::unique(sa.begin(), sa.end()), sa.end());
                    std::vector<double> gaps(sa.size());
                    for (std::size_t i = 0; i + 1 < sa.size(); ++i) {
                        gaps[i] = sa[i + 1] - sa[i];
                    }
                    gaps[sa.size() - 1] = sa[0] + TWO_PI - sa[sa.size() - 1];
                    std::size_t gi = 0;
                    for (std::size_t i = 1; i < gaps.size(); ++i) {
                        if (gaps[i] > gaps[gi]) {
                            gi = i;                     // the first maximum, like list.index
                        }
                    }
                    const double start = sa[(gi + 1) % sa.size()];
                    cs.arc0 = start;
                    cs.arc1 = npMod(start + TWO_PI - gaps[gi], TWO_PI);
                }
                for (int k = 0; k < 3; ++k) {
                    cs.o[k] = o[k];
                    cs.d[k] = dn[k];
                    cs.e1[k] = e1[k];
                    cs.e2[k] = e2[k];
                }
                cs.r = cyl.Radius();
                cs.t0 = t0;
                cs.t1 = t1;
                StepSurface s;
                s.is_cylinder = true;
                s.cyl = cs;
                out.push_back(s);
            } else {
                const gp_Cone cn = surf.Cone();
                const gp_Pnt ap = cn.Apex();
                double dn[3];
                unitDir(cn.Axis().Direction(), dn);
                // gp_Cone.SemiAngle is already in radians; |psi| removes the sign
                // of the second cavity, and cos(psi) is clamped from below - as in
                // the reference.
                const double psi = std::fabs(cn.SemiAngle());
                const double ca = std::max(std::cos(psi), 1e-6);
                const double a2[3] = {ap.X(), ap.Y(), ap.Z()};
                double s0 = 0.0;
                double s1 = 0.0;
                for (std::size_t i = 0; i < np; ++i) {
                    const double q[3] = {pts[i * 3] - a2[0], pts[i * 3 + 1] - a2[1],
                                         pts[i * 3 + 2] - a2[2]};
                    const double sl = dot3(q, dn) / ca;
                    if (i == 0 || sl < s0) {
                        s0 = sl;
                    }
                    if (i == 0 || sl > s1) {
                        s1 = sl;
                    }
                }
                StepSurface s;
                s.is_cylinder = false;
                for (int k = 0; k < 3; ++k) {
                    s.cone.a[k] = a2[k];
                    s.cone.d[k] = dn[k];
                }
                s.cone.psi = psi;
                s.cone.s0 = s0;
                s.cone.s1 = s1;
                out.push_back(s);
            }
        } catch (const Standard_Failure&) {
            // a face that failed to parse is skipped - as in the reference
        }
    }
    return out;
}

StepSurfaceList loadStepSurfaces(const std::string& utf8Path, double deflection) {
    return loadStepSurfaces(readStepShape(utf8Path), deflection);
}

} // namespace gv
