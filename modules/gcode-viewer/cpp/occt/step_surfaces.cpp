// occt/step_surfaces.cpp - зеркало load_step_surfaces из step_surfaces.py.
//
// Порядок обхода граней и порядок операций - часть результата: дескрипторы
// сверяются с эталоном побитово, поэтому файл компилируется с /fp:precise
// (MSVC) и -ffp-contract=off (GCC/Clang).
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

#include "occt/step_import.h"

namespace gv {
namespace {

constexpr double PI = 3.14159265358979323846;
constexpr double TWO_PI = 2.0 * PI;

inline double dot3(const double* a, const double* b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

// Норма 3-вектора: у эталона это (сумма квадратов) ** 0.5, то есть pow, а не
// sqrt: на значениях около единицы они расходятся в младшем бите, и это видно
// в нормализованном направлении оси.
inline double norm3(const double* v) {
    return std::pow(v[0] * v[0] + v[1] * v[1] + v[2] * v[2], 0.5);
}

inline void cross3(const double* a, const double* b, double* out) {
    out[0] = a[1] * b[2] - a[2] * b[1];
    out[1] = a[2] * b[0] - a[0] * b[2];
    out[2] = a[0] * b[1] - a[1] * b[0];
}

inline double npMod(double x, double y) {
    const double r = std::fmod(x, y);
    if (r != 0.0 && ((r < 0.0) != (y < 0.0))) {
        return r + y;
    }
    return r;
}

// round(a, 6) в Python: округление пол-к-чётному до шестого знака. Значения -
// азимуты в [0, 2pi), результат идёт прямо в arc0/arc1, поэтому округление
// обязано совпасть с эталоном бит-в-бит.
inline double round6(double x) {
    return std::nearbyint(x * 1e6) / 1e6;
}

// Нормаль 3-вектора: dv = direction - origin, где origin это (0, 0, 0), то есть
// сам direction; вырождение обрезается порогом 1e-12, как в эталоне.
void unitDir(const gp_Dir& dir, double out[3]) {
    const double dv[3] = {dir.X(), dir.Y(), dir.Z()};
    const double len = std::max(norm3(dv), 1e-12);
    for (int k = 0; k < 3; ++k) {
        out[k] = dv[k] / len;
    }
}

} // namespace

StepSurfaceList loadStepSurfaces(const TopoDS_Shape& shape, double deflection) {
    StepSurfaceList out;
    try {
        // Тесселяция нужна эталону ради ветки узлов грани. Параметры те же, что
        // у load_step_model, поэтому на уже размёщенной форме повторный проход
        // ничего не делает; вызывается ради совпадения пути с эталоном.
        BRepMesh_IncrementalMesh mesher(shape, deflection, false, deflection * 2.0, true);
        mesher.Perform();
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

            // Точки грани - её вершины (BRep_Tool::Pnt уже отдаёт их в
            // координатах сборки, с учётом локации грани). Ветка триангуляции в
            // эталоне недостижима: у OCP-обёртки Poly_Triangulation нет метода
            // IsNull, поэтому getattr(ftri, "IsNull", lambda: True)() даёт True,
            // и условие `ftri is not None and not IsNull и NbNodes() > 0` ложно.
            // Сверка это подтверждает: дугу полуцилиндра эталон строит по двум
            // вершинам (азимуты 0 и pi), а не по узлам сетки - узлы дали бы
            // другой ответ при дуге длиннее 180 гр.
            std::vector<double> pts;
            for (TopExp_Explorer vx(face, TopAbs_VERTEX); vx.More(); vx.Next()) {
                try {
                    const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(vx.Current()));
                    pts.push_back(p.X());
                    pts.push_back(p.Y());
                    pts.push_back(p.Z());
                } catch (const Standard_Failure&) {
                    // вершина, которую не удалось разобрать, пропускается
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

                // Базис сечения: первый узел, у которого поперечная часть не
                // вырождена, задаёт e1; e2 - векторное произведение с осью.
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

                // Угловая дуга грани: большинство "цилиндров" в STEP - дуги
                // (галтели, развёрнутые отверстия), и образующая вне дуги
                // висела бы над пустотой.
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
                            gi = i;                     // первый максимум, как list.index
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
                // gp_Cone.SemiAngle уже в радианах; |psi| снимает знак второй
                // полости, а cos(psi) обрезан снизу - как в эталоне.
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
            // грань, которую не удалось разобрать, пропускается - как в эталоне
        }
    }
    return out;
}

StepSurfaceList loadStepSurfaces(const std::string& utf8Path, double deflection) {
    return loadStepSurfaces(readStepShape(utf8Path), deflection);
}

} // namespace gv
