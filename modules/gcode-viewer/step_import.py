"""Импорт STEP: B-rep, тесселяция, истинные рёбра.

Вынесено из mock_backend.py (этап 0 плана переноса на C++). OCCT (OCP) читается
внутри функций: без пакета модуль импортируется, а STEP просто не читается.
"""
import math
import os

from packing import _coalesce_segments
from normals import _smooth_face_normals

from gv_common import _np

np = _np          # код слоя читает numpy как np

def _step_shape(path):
    """Read a STEP file once; the TopoDS_Shape feeds both the skin extraction
    and the analytic surface list. A second STEPControl_Reader pass over the
    same file costs ~0.8 s (bench_viewer.py step.surfaces) for nothing."""
    from OCP.STEPControl import STEPControl_Reader
    from OCP.IFSelect import IFSelect_ReturnStatus
    reader = STEPControl_Reader()
    if reader.ReadFile(path) != IFSelect_ReturnStatus.IFSelect_RetDone:
        raise ValueError("STEP read failed: %s" % path)
    reader.TransferRoots()
    return reader.OneShape()


def _orient_shell_outward(verts):
    """Развернуть оболочку наружу, если её треугольники смотрят внутрь.

    У замкнутого тела площадь-взвешенные нормали должны смотреть от центроида.
    Если OCP отдал внутреннюю развёртку (или STEP/STL экспортирован вывернутым),
    разворачивается вся оболочка целиком - это единственное место, где решается
    её ориентация. Пофейсовый разворот по центроиду здесь не годится: на
    вогнутых участках (карманы, отверстия) наружная нормаль смотрит к центроиду,
    и такой разворот выворачивает их внутрь. Требует numpy; возвращает
    (F,3,3) float64."""

    tri = _np.asarray(verts, dtype=_np.float64).reshape(-1, 3, 3)
    cr = _np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    ctr = tri.mean(axis=1)
    cent = tri.reshape(-1, 3).mean(axis=0)
    if float((cr * (ctr - cent)).sum()) < 0.0:
        tri = _np.ascontiguousarray(tri[:, ::-1, :])
    return tri


def load_step_model(path, deflection=0.05, shape=None):
    """Full model source from a STEP file: triangle-soup vertices, outline
    segments, per-vertex smooth normals.

    The B-rep is read once and used for both: OpenCascade tessellates the
    faces into a triangle soup (the part skin, same flat layout as
    `load_stl_vertices` returns), and every TopoDS edge is sampled to polyline
    segments ((p0, p1) tuples in raw model coordinates) minus the UV-seam
    lines of single curved faces, which are form lines, not model edges.
    Skin and edges share one 0.05 mm deflection: at 146 mm part extent that
    is a fraction of a pixel of chord sagitta, and it cuts the soup in half
    versus the earlier 0.03 (meshing time, normals pass and GPU fill all
    scale with it; the edge polylines have used 0.05 all along).
    Requires `cadquery-ocp` (pip); raises ImportError when it is absent."""
    from OCP.BRepMesh import BRepMesh_IncrementalMesh
    from OCP.BRep import BRep_Tool
    from OCP.TopExp import TopExp_Explorer
    from OCP.TopAbs import TopAbs_FACE, TopAbs_EDGE, TopAbs_REVERSED
    from OCP.TopoDS import TopoDS
    from OCP.TopLoc import TopLoc_Location
    from OCP.BRepAdaptor import BRepAdaptor_Curve
    from OCP.GCPnts import GCPnts_UniformDeflection

    if shape is None:
        shape = _step_shape(path)
    mesher = BRepMesh_IncrementalMesh(shape, deflection, False, deflection * 2.0, True)
    mesher.Perform()

    pts = []
    soup = []
    exp = TopExp_Explorer(shape, TopAbs_FACE)
    while exp.More():
        try:
            face = TopoDS.Face(exp.Current())
            loc = TopLoc_Location()
            tri = BRep_Tool.Triangulation_s(face, loc)
            if tri is not None and tri.NbTriangles() > 0:
                trsf = loc.Transformation()
                rev = face.Orientation() == TopAbs_REVERSED
                base = len(pts)
                for i in range(1, tri.NbNodes() + 1):
                    p = tri.Node(i).Transformed(trsf)
                    pts.append((p.X(), p.Y(), p.Z()))
                for i in range(1, tri.NbTriangles() + 1):
                    t = tri.Triangle(i)
                    a, b, c = t.Value(1), t.Value(2), t.Value(3)
                    if rev:
                        b, c = c, b
                    soup.append(pts[base + a - 1])
                    soup.append(pts[base + b - 1])
                    soup.append(pts[base + c - 1])
        except Exception:
            pass                       # skip faces that refuse to mesh
        exp.Next()
    segments = []
    from OCP.BRepAdaptor import BRepAdaptor_Surface
    from OCP.GeomAbs import GeomAbs_Line, GeomAbs_Plane

    def _line_key(crv):
        # Endpoint pair (sorted, rounded) for a LINE curve, else None.
        if crv.GetType() != GeomAbs_Line:
            return None
        p0 = crv.Value(crv.FirstParameter())
        p1 = crv.Value(crv.LastParameter())
        a = (round(p0.X(), 2), round(p0.Y(), 2), round(p0.Z(), 2))
        b = (round(p1.X(), 2), round(p1.Y(), 2), round(p1.Z(), 2))
        return (a, b) if a <= b else (b, a)

    # Straight edges whose every adjacent face is curved (cylinder seams and
    # generatrices, including seams between arc segments of a bore) are form
    # lines, not model edges - SolidWorks does not draw them either. A
    # straight edge touching a plane at all (where a flat wall blends into a
    # fillet, or a 90 deg corner) stays as a real model edge.
    face_of_line = {}
    curved_faces = set()
    fid = 0
    expf = TopExp_Explorer(shape, TopAbs_FACE)
    while expf.More():
        try:
            fad = BRepAdaptor_Surface(TopoDS.Face(expf.Current()))
            curved = fad.GetType() != GeomAbs_Plane
            if curved:
                curved_faces.add(fid)
            expe = TopExp_Explorer(expf.Current(), TopAbs_EDGE)
            while expe.More():
                try:
                    k = _line_key(BRepAdaptor_Curve(TopoDS.Edge(expe.Current())))
                    if k is not None:
                        face_of_line.setdefault(k, set()).add(fid)
                except Exception:
                    pass
                expe.Next()
        except Exception:
            pass
        fid += 1
        expf.Next()
    seam_keys = {k for k, fs in face_of_line.items()
                 if fs.issubset(curved_faces)}

    exp = TopExp_Explorer(shape, TopAbs_EDGE)
    while exp.More():
        try:
            crv = BRepAdaptor_Curve(TopoDS.Edge(exp.Current()))
            k = _line_key(crv)
            if k is not None and k in seam_keys:
                exp.Next()
                continue                   # face seam line: not a visible edge
            sampler = GCPnts_UniformDeflection(crv, 0.05)   # edge polylines: coarse is enough
            if sampler.IsDone() and sampler.NbPoints() >= 2:
                prev = sampler.Value(1)
                for i in range(2, sampler.NbPoints() + 1):
                    p = sampler.Value(i)
                    a = (prev.X(), prev.Y(), prev.Z())
                    b = (p.X(), p.Y(), p.Z())
                    if abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2]) > 1e-9:
                        segments.append((a, b))
                    prev = p
        except Exception:
            pass                       # skip degenerate/unresolvable edges
        exp.Next()

    segments = _coalesce_segments(segments)
    if _np is not None and soup:
        tri = _orient_shell_outward(soup)
        return tri.reshape(-1, 3), segments, _smooth_face_normals(tri)
    return soup, segments, None
