#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Оракул промежуточных результатов: эталон для переноса бэкенда на C++.

Считает промежуточные массивы Python-пути на тестовых данных и сверяет их хэши с
записанным манифестом. В репозитории лежит только манифест: сами массивы - это
десятки мегабайт, их получают прогоном этого же скрипта. Так перенос сверяется по
шагам (нормали, топология силуэта, силуэт на фиксированных глазах), а не по
конечной картинке.

    python modules/gcode-viewer/oracle_dump.py           # сверка, exit 0 = пойдено
    python modules/gcode-viewer/oracle_dump.py --write   # дописать манифест

Записи geom.* - буферы вершин геометрий Qt Quick 3D (geometry.py) по случаям;
их сверяет C++-пробник cpp/geometry_probe.cpp в режиме --manifest без Python.

Запись может только ДОБАВИТЬ записи: изменившуюся существующую отвергают (причина
печатается), пока не передан --write --force - правка существующей осознанна.
Проверка ловит расхождение как регресс.
"""
import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

# Геометрии Qt Quick 3D (записи geom.*) - объекты Qt: нужен движок, окно не
# открывается. Значение ставится до импорта PySide6, как в test_geometry_core.py.
os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

import numpy as np  # noqa: E402

import mock_backend as mb  # noqa: E402
import ui_tags  # noqa: E402

DATA = os.path.abspath(os.path.join(HERE, "..", "..", "data", "cad-testdata"))
STEP = os.path.join(DATA, "Test_model.STEP")
STL = os.path.join(DATA, "Test_model.STL")
NGC = os.path.join(DATA, "Test_model.ngc")
MANIFEST = os.path.join(HERE, "oracle_manifest.json")

EYES = [(100.0, 150.0, 300.0), (-200.0, 40.0, 260.0), (60.0, -180.0, 120.0),
        (240.0, 300.0, -60.0), (0.0, 0.0, 400.0)]


def _digest(arr):
    a = np.ascontiguousarray(arr)
    return {
        "shape": list(a.shape),
        "dtype": str(a.dtype),
        "sha256": hashlib.sha256(a.tobytes()).hexdigest(),
    }


def _optional(values, size):
    """Необязательный тег одним массивом: ведущий флаг 1.0/0.0, далее значения.

    Отсутствие тега (None из Python) кодируется явно: ведущий ноль, остальные
    числа нули - иначе отсутствие тега не отличить от тега из нулей.
    """
    head = [1.0 if values is not None else 0.0]
    body = [float(v) for v in values] if values is not None else [0.0] * size
    return np.asarray(head + body, dtype=np.float64)


def _tangent_arrays(surfs, packed, skin):
    """Поверхности вращения STEP и семейства касательных (этап 4d).

    Те же массивы, что сверяет test_tangents_core.py, под именами записей
    tangents.*: по ним пробник cpp/tangents_probe.cpp сверяет свой путь без
    Python. Вход - упакованные рёбра питоновского пути (_pack_edge_segments);
    пробник строит свой буфер сам, и он не совпадает с этим байт в байт (см.
    оговорку в cpp/tangents_probe.cpp): набор отрезков один, порядок и младшие
    биты - нет. Поэтому значения записей берутся здесь, а не у пробника.
    """
    from gv_types import ConeSurface, CylinderSurface

    kind = np.array([0 if isinstance(s, CylinderSurface) else 1 for s in surfs],
                    dtype=np.int32)
    cyl = [[s.o[0], s.o[1], s.o[2], s.d[0], s.d[1], s.d[2],
            s.e1[0], s.e1[1], s.e1[2], s.e2[0], s.e2[1], s.e2[2],
            s.r, s.t0, s.t1,
            np.nan if s.arc[0] is None else float(s.arc[0]),
            np.nan if s.arc[1] is None else float(s.arc[1])]
           for s in surfs if isinstance(s, CylinderSurface)]
    cone = [[s.a[0], s.a[1], s.a[2], s.d[0], s.d[1], s.d[2], s.psi, s.s0, s.s1]
            for s in surfs if isinstance(s, ConeSurface)]

    fams = mb._tangent_families(surfs, packed, skin)
    arr = fams.cylinders
    out = {
        "tangents.surfs_kind": kind,
        "tangents.surfs_cyl": np.array(cyl, dtype=float).reshape(-1, 17),
        "tangents.surfs_cone": np.array(cone, dtype=float).reshape(-1, 9),
        "tangents.fam_O": np.asarray(arr.O, dtype=float).reshape(-1, 3),
        "tangents.fam_D": np.asarray(arr.D, dtype=float).reshape(-1, 3),
        "tangents.fam_E1": np.asarray(arr.E1, dtype=float).reshape(-1, 3),
        "tangents.fam_E2": np.asarray(arr.E2, dtype=float).reshape(-1, 3),
        "tangents.fam_R": np.asarray(arr.R, dtype=float).reshape(-1),
        "tangents.fam_T0": np.asarray(arr.T0, dtype=float).reshape(-1),
        "tangents.fam_T1": np.asarray(arr.T1, dtype=float).reshape(-1),
        "tangents.fam_RREF": np.asarray(arr.RREF, dtype=float).reshape(-1),
        "tangents.fam_A0": np.asarray(arr.A0, dtype=float).reshape(-1),
        "tangents.fam_A1": np.asarray(arr.A1, dtype=float).reshape(-1),
        "tangents.fam_FULL": np.asarray(arr.FULL, dtype=np.uint8).reshape(-1),
        "tangents.fam_DROP": np.asarray(arr.DROP, dtype=float).reshape(-1, 8),
        "tangents.fam_ARC": np.asarray(arr.ARC, dtype=np.uint8).reshape(-1, 64),
        "tangents.cone": np.array([[c.a[0], c.a[1], c.a[2], c.d[0], c.d[1], c.d[2],
                                    c.psi, c.s0, c.s1] for c in fams.cones],
                                  dtype=float).reshape(-1, 9),
    }
    for i, eye in enumerate(EYES):
        segs = mb._tangent_from_families(fams, eye)
        out["tangents.tangent%d" % i] = np.array(segs, dtype=float).reshape(-1, 2, 3)
    return out


# Профиль инструмента для случая geom.tool: концевой фрез с радиусом скругления
# (BULL) даёт обе ветки веера и четырёхугольники.
GEOM_TOOL_INFO = {"diameter": 6.0, "type": "BULL", "corner_radius": 1.0,
                  "flute_length": 20.0, "shank_diameter": 6.0,
                  "body_length": 40.0, "overall_length": 60.0}


def _geometry_arrays(pd):
    """Буферы геометрий Qt Quick 3D (geometry.py) по случаям - записи geom.*.

    Те же входы, что у C++-пробника cpp/geometry_probe.cpp: траектория - из УП тем
    же путём (_prepare_ordered), профиль инструмента - питоновским построителем,
    остальное - те же числа. Пробник строит свой вход сам и сверяет буферы с этими
    дайджестами (--manifest) без Python; расхождение ловит и вход, и геометрию.
    Буфер вершин - uint8 формы (N,), как его хэширует test_geometry_core.py.
    """
    from PySide6.QtGui import QGuiApplication
    import geometry as geo
    from gv_types import Aabb, ProfilePoint

    if QGuiApplication.instance() is None:
        QGuiApplication([])                # геометрии - объекты Qt Quick 3D

    bound_box = Aabb(tuple(pd.bounds.lo), tuple(pd.bounds.hi))

    # Звенья контура: обычные, нулевой (ветка ln == 0) и вдоль оси Y.
    segs = [((i * 3.0, 0.0, i * 1.5), (i * 3.0 + 10.0, 2.5 * i, i * 1.5)) for i in range(7)]
    segs.append(((5.0, 5.0, 5.0), (5.0, 5.0, 5.0)))
    segs.append(((0.0, 0.0, 0.0), (0.0, 10.0, 0.0)))
    hlr = np.asarray(segs, dtype=np.float32)

    grid = [200.0, 10.0, -2.5, 3.25, -7.5, 0.45]
    grid_flat = [60.0, 7.0, 0.0, -1.5, 2.5, 0.0]        # растворения нет: все альфы 1
    axis = [12.0, 3.5, 2.0, 0.7]
    axis_short = [3.0, 4.0, 2.0, 0.7]                    # голова длиннее указателя

    # Куб из 12 треугольников плюс вырожденный: ветка ln == 0 в нормалях.
    c = [(0.0, 0.0, 0.0), (12.7, 0.0, 0.0), (12.7, 12.7, 0.0), (0.0, 12.7, 0.0),
         (0.0, 0.0, 6.3), (12.7, 0.0, 6.3), (12.7, 12.7, 6.3), (0.0, 12.7, 6.3)]
    faces = [(0, 1, 2), (0, 2, 3), (4, 6, 5), (4, 7, 6), (0, 4, 5), (0, 5, 1),
             (1, 5, 6), (1, 6, 2), (2, 6, 7), (2, 7, 3), (3, 7, 4), (3, 4, 0)]
    verts = [c[i] for face in faces for i in face]
    verts.extend([(1.1, 2.2, 3.3)] * 3)
    mesh = np.asarray(verts, dtype="<f8")

    prof = mb._tool_profile(GEOM_TOOL_INFO)
    low = [(0.0, 0.0), (3.0, 0.0), (3.0, 20.0), (0.0, 20.0)]

    cases = []

    def add(name, geom):
        cases.append((name, geom))

    g = geo.ToolpathGeometry()
    g.set_packed(pd.feed_bytes, bound_box, pd.feed_nv)
    add("toolpath", g)
    g = geo.ToolpathGeometry()
    g.set_packed(pd.feed_bytes, None, pd.feed_nv)             # габарита нет
    add("toolpath_nobounds", g)
    g = geo.ToolpathGeometry()
    g.set_packed(b"", None, 0)
    add("toolpath_empty", g)

    g = geo.HLRLineGeometry()
    g.set_reference((12.5, -4.25, 7.0))
    g.set_segments(hlr)
    add("hlr", g)
    g = geo.HLRLineGeometry()
    g.set_segments(hlr)                                      # опорной точки нет
    add("hlr_noref", g)
    g = geo.HLRLineGeometry()
    g.set_segments(None)
    add("hlr_empty", g)

    for name, values in (("grid", grid), ("grid_flat", grid_flat)):
        g = geo.GridGeometry()
        g.size = values[0]
        g.step = values[1]
        g.level = values[2]
        g.centerX = values[3]
        g.centerZ = values[4]
        g.feather = values[5]
        add(name, g)
    for name, values in (("axis", axis), ("axis_short", axis_short)):
        g = geo.AxisArrowGeometry()
        g.length = values[0]
        g.headLength = values[1]
        g.headWidth = values[2]
        g.shaftWidth = values[3]
        add(name, g)

    g = geo.MeshGeometry()
    g.set_triangles(mesh)
    add("mesh", g)
    g = geo.MeshGeometry()
    g.set_triangles(mesh)
    g.reset()
    add("mesh_empty", g)

    g = geo.ToolGeometry()
    g.set_profile(prof, 48)
    add("tool", g)
    g = geo.ToolGeometry()
    g.set_profile([ProfilePoint(r, y) for r, y in low], 3)
    add("tool_low", g)

    return {"geom.%s" % name: np.frombuffer(bytes(g.vertexData()), dtype=np.uint8)
            for name, g in cases}


def collect():
    """Считает оракул: (имя -> массив) для промежуточных результатов."""
    out = {}
    step_missing = not os.path.exists(STEP)

    if not step_missing:
        verts, segments, normals = mb.load_step_model(STEP)
        out["step.verts"] = np.asarray(verts, dtype=np.float64)
        out["step.segments"] = np.asarray(segments, dtype=np.float64)
        if normals is not None:
            out["step.normals"] = np.asarray(normals, dtype=np.float32)

        tri = np.asarray(verts, dtype=np.float64).reshape(-1, 3, 3)
        out["step.tri"] = tri

        topo = mb._build_silhouette_topology(verts)
        if topo is not None:
            for key in ("n", "d", "f0", "f1", "ea", "eb"):
                out["topo." + key] = np.asarray(getattr(topo, key))
            for i, eye in enumerate(EYES):
                segs = mb._mesh_silhouette_segments(topo, eye)
                out["silhouette.eye%d" % i] = np.asarray(
                    segs if segs is not None else np.empty((0, 2, 3)), dtype=np.float32)

        # Аналитические поверхности вращения STEP и семейства касательных
        # (step_surfaces.py, tangents.py): то же, что сверяет
        # test_tangents_core.py. Вход - упакованные рёбра питоновского пути.
        surfs = mb.load_step_surfaces(STEP)
        if surfs:
            packed = mb._pack_edge_segments(segments)
            out.update(_tangent_arrays(surfs, packed, (verts, normals)))

    if os.path.exists(NGC):
        with open(NGC, "r", encoding="utf-8", errors="ignore") as fh:
            text = fh.read()
        ordered, last, seg_count, stages = mb.parse_gcode(text)
        out["path.points"] = np.asarray([p.point for p in ordered], dtype=np.float64)
        out["path.flags"] = np.asarray(
            [[1.0 if p.is_rapid else 0.0, float(p.speed_mm_s)] for p in ordered], dtype=np.float64)
        out["path.last"] = np.asarray(last, dtype=np.float64)
        out["path.segments"] = np.asarray([seg_count], dtype=np.int64)
        out["path.stages"] = np.asarray([str((s.index, s.label)) for s in stages], dtype="U64")

        # Подготовка траектории (path_prep._prepare_ordered): буферы потоков,
        # полилиния сцены и метрики - то же, что сверяет test_gcode_core.py.
        # Числовые метрики записаны массивами формы (1,), чтобы у каждой записи
        # был dtype и форма, как у остальных.
        pd = mb._prepare_ordered(ordered, stages)
        out["pd.pts"] = np.asarray(pd.pts, dtype=np.float64)
        out["pd.cum"] = np.asarray(pd.cum, dtype=np.float64)
        out["pd.rapid"] = np.asarray(pd.rapid, dtype=bool)
        out["pd.speeds"] = np.asarray(pd.speeds, dtype=np.float64)
        out["pd.rapid_stream"] = np.frombuffer(pd.rapid_bytes, dtype=np.uint8)
        out["pd.feed_stream"] = np.frombuffer(pd.feed_bytes, dtype=np.uint8)
        out["pd.total"] = np.asarray([pd.total], dtype=np.float64)
        out["pd.radius"] = np.asarray([pd.radius], dtype=np.float64)
        out["pd.min_y"] = np.asarray([pd.min_y], dtype=np.float64)
        out["pd.center"] = np.asarray(pd.center, dtype=np.float64)
        out["pd.bounds_lo"] = np.asarray(pd.bounds.lo, dtype=np.float64)
        out["pd.bounds_hi"] = np.asarray(pd.bounds.hi, dtype=np.float64)
        out["pd.rapid_nv"] = np.asarray([pd.rapid_nv], dtype=np.int64)
        out["pd.feed_nv"] = np.asarray([pd.feed_nv], dtype=np.int64)
        out["pd.stage_start"] = np.asarray([s["start"] for s in pd.stages], dtype=np.float64)
        out["pd.stage_end"] = np.asarray([s["end"] for s in pd.stages], dtype=np.float64)
        out["pd.stage_label"] = np.asarray([s["label"] for s in pd.stages], dtype="U64")

        # Геометрии Qt Quick 3D (geometry.py): то же, что сверяет
        # test_geometry_core.py и C++-пробник cpp/geometry_probe.cpp без Python.
        out.update(_geometry_arrays(pd))

        # Теги UI_*: то же, что сверяет test_gcode_core.py через ui_tags.py.
        # Разбор идёт по всему тексту: питоновские re.search и C++ ищут первое
        # вхождение с начала, поэтому первая строка тега и целый текст дают одно.
        # Необязательные заготовка и матрицы CAD - через _optional; оправка -
        # ведущий флаг присутствия и вылет, имя - отдельной записью.
        out["tag.rotation"] = np.asarray(ui_tags.parse_ui_rotation(text), dtype=np.float64)
        out["tag.origin"] = np.asarray(ui_tags.parse_ui_origin(text), dtype=np.float64)
        out["tag.stock"] = _optional(ui_tags.parse_ui_stock(text), 6)
        out["tag.cad_matrix"] = _optional(ui_tags.parse_ui_cad_matrix(text), 9)
        out["tag.cad_matrix_origin"] = _optional(ui_tags.parse_ui_cad_matrix_origin(text), 3)
        holder = ui_tags.parse_ui_holder(text)
        out["tag.holder"] = np.asarray(
            [1.0 if holder else 0.0, float(holder.get("gauge", 0.0))], dtype=np.float64)
        out["tag.holder_name"] = np.asarray([holder.get("name", "")], dtype="U64")

    if os.path.exists(STL):
        soup = np.asarray(mb.load_stl_vertices(STL), dtype=np.float64)
        out["stl.soup"] = soup
        packed = mb._feature_edges_np(soup, 20.0)
        segs = mb.packed_positions(packed)
        out["stl.feature_edges"] = np.asarray(
            segs if segs is not None else np.empty((0, 2, 3)), dtype=np.float32)

    return out


def _write(manifest) -> int:
    """Запись манифеста: существующие записи только добавляются.

    Перебазировка может лишь ДОБАВИТЬ записи: изменение формы, dtype или sha256
    уже записанной - осознанный шаг (правка, изменившая результат), поэтому без
    --force она отвергается. Так случайная правка кода не переписывает эталон.
    """
    saved = {}
    if os.path.exists(MANIFEST):
        with open(MANIFEST, "r", encoding="utf-8") as fh:
            saved = json.load(fh)

    changed = []
    for name in sorted(saved):
        want, got = saved[name], manifest.get(name)
        if got is None:
            changed.append((name, "запись исчезла"))
        elif want["shape"] != got["shape"]:
            changed.append((name, "форма %s -> %s" % (want["shape"], got["shape"])))
        elif want["dtype"] != got["dtype"]:
            changed.append((name, "dtype %s -> %s" % (want["dtype"], got["dtype"])))
        elif want["sha256"] != got["sha256"]:
            changed.append((name, "sha256 %s -> %s"
                            % (want["sha256"][:16], got["sha256"][:16])))

    force = "--force" in sys.argv
    if changed and not force:
        print("манифест не записан: изменяются существующие записи (%d):" % len(changed))
        for name, why in changed:
            print("  %-22s %s" % (name, why))
        print("изменение существующей записи - осознанный шаг: --write --force")
        return 1

    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(manifest, fh, ensure_ascii=False, indent=2, sort_keys=True)
        fh.write("\n")

    added = sorted(set(manifest) - set(saved))
    print("манифест записан: %s (%d записей, добавлено %d)"
          % (MANIFEST, len(manifest), len(added)))
    for name in added:
        print("  + %-22s %-14s %s" % (name, str(manifest[name]["shape"]),
                                      manifest[name]["sha256"][:16]))
    if changed:
        print("  перезаписано существующих записей (--force): %d" % len(changed))
    return 0


def main() -> int:
    arrays = collect()
    manifest = {name: _digest(arr) for name, arr in sorted(arrays.items())}

    if "--write" in sys.argv:
        return _write(manifest)

    if not os.path.exists(MANIFEST):
        print("манифеста нет; запишите его: oracle_dump.py --write")
        return 1
    with open(MANIFEST, "r", encoding="utf-8") as fh:
        saved = json.load(fh)

    bad = []
    for name in sorted(set(saved) | set(manifest)):
        a, b = saved.get(name), manifest.get(name)
        if a is None:
            bad.append(name)
            print("лишнее в оракуле: %s" % name)
        elif b is None:
            bad.append(name)
            print("нет в оракуле: %s" % name)
        elif a["sha256"] != b["sha256"] or a["shape"] != b["shape"]:
            bad.append(name)
            print("расхождение %-22s %s -> %s" % (name, a["shape"], b["shape"]))
        else:
            print("ok    %-22s %-14s" % (name, str(b["shape"])))
    print("RESULT: %s" % ("OK" if not bad else "FAIL (%d)" % len(bad)))
    return 0 if not bad else 1


if __name__ == "__main__":
    sys.exit(main())
