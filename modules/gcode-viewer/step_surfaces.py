"""Аналитические поверхности вращения из STEP: цилиндры и конусы.

Вынесено из mock_backend.py (этап 0 плана переноса на C++).
"""
import math

from gv_common import _np
from gv_types import ConeSurface, CylinderSurface
from step_import import _step_shape

np = _np          # код слоя читает numpy как np

def load_step_surfaces(path, shape=None):
    """Analytic surfaces of revolution from a STEP file.

    Returns CylinderSurface/ConeSurface descriptors for every face whose
    underlying surface is a cylinder or a cone - the shapes that make up
    machined bores, rounds and chamfers.
    Feeds GcodeToolpath.refreshTangents: view-dependent tangent generatrices
    computed from the exact CAD geometry (analytic silhouette of bores and
    rounds) - no post-processing shaders, plain Lines geometry like the
    B-rep edges. Requires cadquery-ocp; [] when absent/unparsable."""
    try:
        from OCP.TopExp import TopExp_Explorer
        from OCP.TopAbs import TopAbs_FACE, TopAbs_VERTEX
        from OCP.TopoDS import TopoDS
        from OCP.BRepAdaptor import BRepAdaptor_Surface
        from OCP.BRep import BRep_Tool
        from OCP.BRepMesh import BRepMesh_IncrementalMesh
        from OCP.TopLoc import TopLoc_Location
        from OCP.GeomAbs import GeomAbs_Cylinder, GeomAbs_Cone
        from OCP.gp import gp_Pnt
    except Exception:
        return []
    try:
        if shape is None:
            shape = _step_shape(path)
        BRepMesh_IncrementalMesh(shape, 0.05, False, 0.10, True).Perform()
    except Exception:
        return []
    out = []
    try:
        exp = TopExp_Explorer(shape, TopAbs_FACE)
        while exp.More():
            try:
                face = TopoDS.Face(exp.Current())
                surf = BRepAdaptor_Surface(face)
                st = surf.GetType()
                if st not in (GeomAbs_Cylinder, GeomAbs_Cone):
                    exp.Next()
                    continue
                # BRepAdaptor_Surface и BRep_Tool.Pnt_s уже отдают геометрию в
                # координатах сборки (с учётом TopLoc_Location грани): второе
                # применение трансформации удваивало смещение (ось цилиндра
                # на x=200 мм вместо 100 мм у грани, сдвинутой на 100 мм).
                def xp(t):
                    return (float(t[0]), float(t[1]), float(t[2]))

                pts = []
                ftri = None
                try:
                    floc = TopLoc_Location()
                    ftri = BRep_Tool.Triangulation_s(face, floc)
                except Exception:
                    ftri = None
                if ftri is not None and not getattr(ftri, "IsNull", lambda: True)() \
                        and ftri.NbNodes() > 0:
                    ftrsf = floc.Transformation()
                    for i in range(1, ftri.NbNodes() + 1):
                        q = ftri.Node(i).Transformed(ftrsf)
                        pts.append((q.X(), q.Y(), q.Z()))
                if not pts:
                    vx = TopExp_Explorer(face, TopAbs_VERTEX)
                    while vx.More():
                        try:
                            p = BRep_Tool.Pnt_s(TopoDS.Vertex(vx.Current()))
                            pts.append(xp((p.X(), p.Y(), p.Z())))
                        except Exception:
                            pass
                        vx.Next()
                if not pts:
                    exp.Next()
                    continue
                if st == GeomAbs_Cylinder:
                    cyl = surf.Cylinder()
                    lo_, la = cyl.Axis().Location(), cyl.Axis().Direction()
                    origin = xp((0.0, 0.0, 0.0))
                    o = xp((lo_.X(), lo_.Y(), lo_.Z()))
                    dv = xp((la.X(), la.Y(), la.Z()))
                    dv = (dv[0] - origin[0], dv[1] - origin[1], dv[2] - origin[2])
                    L = max((dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]) ** 0.5, 1e-12)
                    dn = (dv[0] / L, dv[1] / L, dv[2] / L)
                    tl = [(q[0] - o[0]) * dn[0] + (q[1] - o[1]) * dn[1]
                          + (q[2] - o[2]) * dn[2] for q in pts]
                    # Angular footprint of the face on the cylinder: most
                    # "cylinders" in machined STEPs are arcs (fillets, cut
                    # bores); a tangent whose azimuth falls outside the arc
                    # would float over empty space (static ghost lines).
                    e1 = None
                    for q in pts:
                        vq = (q[0] - o[0], q[1] - o[1], q[2] - o[2])
                        al = vq[0] * dn[0] + vq[1] * dn[1] + vq[2] * dn[2]
                        pv = (vq[0] - dn[0] * al, vq[1] - dn[1] * al,
                              vq[2] - dn[2] * al)
                        Rp = (pv[0] ** 2 + pv[1] ** 2 + pv[2] ** 2) ** 0.5
                        if Rp > 1e-6:
                            e1 = (pv[0] / Rp, pv[1] / Rp, pv[2] / Rp)
                            break
                    e2 = (dn[1] * e1[2] - dn[2] * e1[1],
                          dn[2] * e1[0] - dn[0] * e1[2],
                          dn[0] * e1[1] - dn[1] * e1[0]) if e1 else None
                    raw_ang = []
                    if e1:
                        for q in pts:
                            vq = (q[0] - o[0], q[1] - o[1], q[2] - o[2])
                            al = vq[0] * dn[0] + vq[1] * dn[1] + vq[2] * dn[2]
                            pv = (vq[0] - dn[0] * al, vq[1] - dn[1] * al,
                                  vq[2] - dn[2] * al)
                            if (pv[0] ** 2 + pv[1] ** 2 + pv[2] ** 2) ** 0.5 > 1e-6:
                                raw_ang.append(math.atan2(
                                    pv[0] * e2[0] + pv[1] * e2[1] + pv[2] * e2[2],
                                    pv[0] * e1[0] + pv[1] * e1[1] + pv[2] * e1[2]) % (2 * math.pi))
                    arc0 = arc1 = None
                    if len(raw_ang) >= 2:
                        sa = sorted(set(round(a, 6) for a in raw_ang))
                        gaps = [sa[i + 1] - sa[i] for i in range(len(sa) - 1)]
                        gaps.append(sa[0] + 2 * math.pi - sa[-1])
                        gi = gaps.index(max(gaps))
                        start = sa[(gi + 1) % len(sa)]
                        arc0 = start
                        arc1 = (start + 2 * math.pi - max(gaps)) % (2 * math.pi)
                    out.append(CylinderSurface(
                        o=o, d=dn,
                        e1=e1 or (1.0, 0.0, 0.0),
                        e2=e2 or (0.0, 0.0, 1.0),
                        arc=(arc0, arc1),
                        r=float(cyl.Radius()),
                        t0=min(tl), t1=max(tl)))
                else:
                    cn = surf.Cone()
                    ap = cn.Apex(); ad = cn.Axis().Direction()
                    origin = xp((0.0, 0.0, 0.0))
                    a2 = xp((ap.X(), ap.Y(), ap.Z()))
                    dv = xp((ad.X(), ad.Y(), ad.Z()))
                    dv = (dv[0] - origin[0], dv[1] - origin[1], dv[2] - origin[2])
                    L = max((dv[0] * dv[0] + dv[1] * dv[1] + dv[2] * dv[2]) ** 0.5, 1e-12)
                    dn = (dv[0] / L, dv[1] / L, dv[2] / L)
                    # gp_Cone.SemiAngle: метода HalfAngle в OCP нет, а
                    # исключение ниже глотается - все конические грани
                    # (фаски, зенковки) молча выпадали из контура.
                    psi = abs(float(cn.SemiAngle()))
                    ca = max(math.cos(psi), 1e-6)
                    sl = [((q[0] - a2[0]) * dn[0] + (q[1] - a2[1]) * dn[1]
                           + (q[2] - a2[2]) * dn[2]) / ca for q in pts]
                    out.append(ConeSurface(
                        a=a2, d=dn,
                        psi=psi, s0=min(sl), s1=max(sl)))
            except Exception:
                pass
            exp.Next()
    except Exception:
        pass
    return out
