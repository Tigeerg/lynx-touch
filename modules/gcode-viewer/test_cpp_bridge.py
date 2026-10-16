#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сверка C-ABI-моста gvbridge с питоновским эталоном (этап 5 плана переноса).

Проверяется то, что мост обязан держать:

  * флаг CNC_CPP: пусто/off - всё на Python, normals - нормали STL, step - ветка
    STEP, surfaces - аналитические поверхности, silhouette - силуэт и
    касательные, parser - разбор УП, all - всё, незнакомое значение отключает
    C++ и видно в журнале;
  * нормали STL бит-в-бит (gv_cpp.smooth_face_normals против
    normals._smooth_face_normals) на супе Test_model.STL;
  * STEP бит-в-бит: суп вершин и crease-aware нормали (gv_cpp.load_step_model
    против step_import.load_step_model) и упакованный буфер обводки после
    питоновской склейки рёбер;
  * поверхности STEP бит-в-бит (gv_cpp.load_step_surfaces против
    step_surfaces.load_step_surfaces) и тот же набор одним чтением с моделью
    (gv_cpp.load_step_all): при CNC_CPP=all файл читается один раз на оба слоя -
    проверка отключает питоновское чтение STEP и всё равно получает модель;
  * топология силуэта и силуэт на пяти фиксированных глазах бит-в-бит
    (gv_cpp.build_silhouette_topology / mesh_silhouette_segments против
    silhouette.*);
  * семейства касательных и отрезки бит-в-бит (gv_cpp.tangent_families /
    tangent_segments против tangents.*), в том числе семейства без кожи и без
    обводки - так их строит загрузка модели;
  * разбор УП и подготовка траектории бит-в-бит (gv_cpp.prepare_path против
    gcode_parser.parse_gcode + path_prep._prepare_ordered): точки, флаги,
    скорости, потоки вершин, габарит, этапы;
  * ветку выбирает мост: с выключенным питоновским эталоном (подмена на отказ)
    результат даёт только C++-путь, и наоборот;
  * отказ моста виден в журнале и не ломает результат: подпроцесс с битым
    CNC_CPP_LIB печатает предупреждение и считает питоновским путём;
  * питоновские проверки под CNC_CPP=all (test_normals_golden.py,
    test_normals_orientation.py, test_tangent_policy.py) остаются зелёными.

Мост не собран - проверка пропускается с кодом 0: сборку стережёт
tools/check.ps1 (scripts/build-viewer.ps1).

Запуск: python modules/gcode-viewer/test_cpp_bridge.py (exit 0 = пойдено)
"""
import hashlib
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

DATA = os.path.join(ROOT, "data", "cad-testdata")
STEP = os.path.join(DATA, "Test_model.STEP")
STL = os.path.join(DATA, "Test_model.STL")
NGC = os.path.join(DATA, "Test_model.ngc")

# Подпроцесс с битым CNC_CPP_LIB: мост недоступен, результат обязан остаться
# питоновским, а причина отказа - попасть в журнал.
FALLBACK_SNIPPET = """
import logging, os, sys
sys.path.insert(0, r'{here}')
logging.basicConfig(level=logging.WARNING, format='%(levelname)s %(message)s')
import numpy as np
from controller import GcodeToolpath
soup = np.array([[0, 0, 0], [10, 0, 0], [0, 10, 0],
                 [0, 0, 0], [0, 10, 0], [0, 0, 10]], dtype=float)
n = GcodeToolpath._stl_normals(soup)
ok = n is not None and n.shape == (2, 3, 3)
print('FALLBACK %s' % ('OK' if ok else 'FAIL'))
"""


def _sha(arr, numpy):
    return hashlib.sha256(numpy.ascontiguousarray(arr).tobytes()).hexdigest()


def _bits_equal(want, got, numpy):
    """Побитовое равенство массивов: у эталона в A0/A1/DROP бывает NaN, а NaN != NaN."""
    a = numpy.asarray(want)
    b = numpy.asarray(got)
    return (a.shape == b.shape and str(a.dtype) == str(b.dtype)
            and a.tobytes() == b.tobytes())


def _py_step_model():
    """Питоновский эталон модели STEP: суп, рёбра до склейки, нормали."""
    from step_import import load_step_model

    return load_step_model(STEP)


def _coalesce(segments):
    """Склейка рёбер: её ведёт Python и на C++-пути (буфер обводки не меняется)."""
    from packing import _coalesce_segments

    return _coalesce_segments(segments)


def _pack(segments):
    from packing import _pack_edge_segments

    return _pack_edge_segments(segments)


def _flag_checks(bad):
    """Флаг CNC_CPP включает ровно тот слой, который просит."""
    import gv_cpp

    cases = [("", (False, False, False, False, False)),
             ("off", (False, False, False, False, False)),
             ("0", (False, False, False, False, False)),
             ("normals", (True, False, False, False, False)),
             ("step", (False, True, False, False, False)),
             ("surfaces", (False, False, True, False, False)),
             ("silhouette", (False, False, False, True, False)),
             ("parser", (False, False, False, False, True)),
             ("all", (True, True, True, True, True)),
             ("мусор", (False, False, False, False, False))]
    for value, want in cases:
        os.environ["CNC_CPP"] = value
        got = (gv_cpp.normals_enabled(), gv_cpp.step_enabled(),
               gv_cpp.surfaces_enabled(), gv_cpp.silhouette_enabled(),
               gv_cpp.parser_enabled())
        if got != want:
            bad.append("CNC_CPP=%r дал %s, ожидалось %s" % (value, got, want))
            print("FAIL  CNC_CPP=%r -> %s, ожидалось %s" % (value, got, want))
        else:
            print("ok    CNC_CPP=%-11r -> нормали %-5s STEP %-5s поверхн. %-5s "
                  "силуэт %-5s разбор %s"
                  % (value, got[0], got[1], got[2], got[3], got[4]))
    os.environ["CNC_CPP"] = "all"


def _stl_normals_check(bad):
    """Нормали STL: C++ против питоновского эталона, байт в байт."""
    import numpy as np

    import gv_cpp
    import controller
    import normals
    from stl_import import load_stl_vertices
    from step_import import _orient_shell_outward

    soup = _orient_shell_outward(
        np.asarray(load_stl_vertices(STL), dtype=np.float64)).reshape(-1, 3)
    ref = np.asarray(normals._smooth_face_normals(soup.reshape(-1, 3, 3)),
                     dtype=np.float32)
    cpp = gv_cpp.smooth_face_normals(soup)
    print("нормали STL: треугольников %d  python %s  cpp %s"
          % (soup.shape[0] // 3, _sha(ref, np)[:16], _sha(cpp, np)[:16]))
    if ref.shape != cpp.shape or not np.array_equal(ref, cpp):
        bad.append("нормали STL разошлись с эталоном (shape %s против %s)"
                   % (ref.shape, cpp.shape))

    # Ветку выбирает gv_cpp: с выключенным питоновским эталоном результат может
    # дать только C++-путь.
    os.environ["CNC_CPP"] = "normals"
    saved = controller._smooth_face_normals

    def boom(*_args, **_kwargs):
        raise RuntimeError("питоновский эталон отключён проверкой")

    controller._smooth_face_normals = boom
    try:
        via_controller = controller.GcodeToolpath._stl_normals(soup)
    finally:
        controller._smooth_face_normals = saved
    if via_controller is None or not np.array_equal(np.asarray(via_controller), ref):
        bad.append("_stl_normals с CNC_CPP=normals не пошёл на C++ (эталон отключён)")

    # Обратный случай: с выключенным флагом питоновский путь единственный.
    os.environ["CNC_CPP"] = ""
    controller._smooth_face_normals = boom
    try:
        off = controller.GcodeToolpath._stl_normals(soup)
    finally:
        controller._smooth_face_normals = saved
    if off is not None:
        bad.append("_stl_normals без флага вернул %s, ожидалось None" % type(off))
    print("ok    ветку выбирает флаг: CNC_CPP=normals -> C++, пусто -> Python")
    os.environ["CNC_CPP"] = "all"


def _step_check(bad):
    """Ветка STEP: суп, нормали, буфер обводки - байт в байт."""
    import numpy as np

    import controller
    import gv_cpp
    from packing import _coalesce_segments, _pack_edge_segments
    from step_import import load_step_model

    py_verts, py_segments, py_normals = load_step_model(STEP)
    py_packed = _pack_edge_segments(py_segments)
    cpp_verts, cpp_raw, cpp_normals = gv_cpp.load_step_model(STEP)
    cpp_packed = _pack_edge_segments(_coalesce_segments(cpp_raw))

    same_v = np.array_equal(np.asarray(py_verts), cpp_verts)
    same_n = np.array_equal(np.asarray(py_normals), cpp_normals)
    same_p = py_packed.raw == cpp_packed.raw and py_packed.bounds == cpp_packed.bounds
    print("STEP: вершин %d  нормалей %s  рёбер %d (до склейки %d)"
          % (cpp_verts.shape[0], cpp_normals.shape, cpp_packed.nv // 2, len(cpp_raw)))
    print("      суп %s = %s  нормали %s = %s  буфер обводки %d байт = %s"
          % (_sha(np.asarray(py_verts), np)[:16], _sha(cpp_verts, np)[:16],
             _sha(np.asarray(py_normals), np)[:16], _sha(cpp_normals, np)[:16],
             len(py_packed.raw), same_p))
    if not same_v:
        bad.append("суп вершин C++ разошёлся с эталоном")
    if not same_n:
        bad.append("нормали STEP C++ разошлись с эталоном")
    if not same_p:
        bad.append("буфер обводки C++ разошёлся с эталоном (%d байт против %d)"
                   % (len(cpp_packed.raw), len(py_packed.raw)))

    # Ветку выбирает gv_cpp: с отключённым питоновским чтением STEP результат
    # может дать только C++-путь.
    os.environ["CNC_CPP"] = "step"
    saved = controller.load_step_model

    def boom(*_args, **_kwargs):
        raise RuntimeError("питоновское чтение STEP отключено проверкой")

    controller.load_step_model = boom
    try:
        verts, segments, normals = controller._step_model(STEP, None)
    finally:
        controller.load_step_model = saved
    if not (np.array_equal(np.asarray(verts), cpp_verts)
            and np.array_equal(np.asarray(normals), cpp_normals)
            and _pack_edge_segments(segments).raw == cpp_packed.raw):
        bad.append("_step_model с CNC_CPP=step не пошёл на C++ (эталон отключён)")
    else:
        print("ok    _step_model с CNC_CPP=step считает мост (эталон отключён)")
    os.environ["CNC_CPP"] = "all"


def _surfaces_check(bad):
    """Поверхности STEP: один вызов моста против питоновского эталона."""
    import numpy as np

    import gv_cpp
    from step_surfaces import load_step_surfaces

    py_surfs = load_step_surfaces(STEP)
    cpp_verts, cpp_segments, cpp_normals, cpp_surfs = gv_cpp.load_step_all(STEP)
    cpp_surfs_only = gv_cpp.load_step_surfaces(STEP)

    def rows(surfs):
        """Дескрипторы одним массивом: 18 чисел на поверхность, вид впереди."""
        out = []
        for s in surfs:
            if isinstance(s, gv_cpp.CylinderSurface):
                arc = s.arc or (None, None)
                out.append([0.0] + list(s.o) + list(s.d) + list(s.e1)
                           + list(s.e2) + [s.r, s.t0, s.t1,
                                           np.nan if arc[0] is None else arc[0],
                                           np.nan if arc[1] is None else arc[1]])
            else:
                out.append([1.0] + list(s.a) + list(s.d) + [s.psi, s.s0, s.s1]
                           + [0.0] * 8)
        return np.asarray(out, dtype=np.float64).reshape(-1, 18)

    py = rows(py_surfs)
    cpp = rows(cpp_surfs)
    cpp_only = rows(cpp_surfs_only)
    same_all = py.shape == cpp.shape and np.array_equal(py, cpp, equal_nan=True)
    same_only = py.shape == cpp_only.shape and np.array_equal(py, cpp_only,
                                                              equal_nan=True)
    print("поверхности: %d (цилиндров %d, конусов %d)  python %s  cpp %s"
          % (len(cpp_surfs),
             sum(1 for s in cpp_surfs if isinstance(s, gv_cpp.CylinderSurface)),
             sum(1 for s in cpp_surfs if isinstance(s, gv_cpp.ConeSurface)),
             _sha(py, np)[:16], _sha(cpp, np)[:16]))
    if not same_all:
        bad.append("поверхности из load_step_all разошлись с эталоном")
    if not same_only:
        bad.append("поверхности из load_step_surfaces разошлись с эталоном")

    # Модель при этом та же, что у gv_load_step_model: один проход STEP отдаёт и
    # суп, и поверхности, и расхождение здесь означало бы разные чтения.
    py_verts, py_segments, py_normals = _py_step_model()
    packed_py = _pack(py_segments)
    packed_cpp = _pack(_coalesce(cpp_segments))
    same_model = (np.array_equal(np.asarray(py_verts), cpp_verts)
                  and np.array_equal(np.asarray(py_normals), cpp_normals)
                  and packed_py.raw == packed_cpp.raw)
    print("      модель из load_step_all: суп и нормали %s, обводка %d байт = %s"
          % (same_model, len(packed_cpp.raw), packed_py.raw == packed_cpp.raw))
    if not same_model:
        bad.append("модель из load_step_all разошлась с эталоном")

    # Один проход STEP на оба слоя: с отключённым питоновским чтением STEP модель
    # и поверхности всё равно приходят (иначе чтений было бы два).
    import controller
    os.environ["CNC_CPP"] = "all"
    saved = (controller._step_shape, controller.load_step_model,
             controller.load_step_surfaces)

    def boom(*_args, **_kwargs):
        raise RuntimeError("питоновское чтение STEP отключено проверкой")

    controller._step_shape = boom
    controller.load_step_model = boom
    controller.load_step_surfaces = boom
    try:
        got = controller._read_step_model(STEP)
    finally:
        (controller._step_shape, controller.load_step_model,
         controller.load_step_surfaces) = saved
    ok = (len(got) == 4 and np.array_equal(np.asarray(got[0]), cpp_verts)
          and len(got[3]) == len(cpp_surfs))
    print("ok    _read_step_model с CNC_CPP=all читает STEP один раз: %s" % ok)
    if not ok:
        bad.append("_read_step_model с CNC_CPP=all не обошёлся одним чтением STEP")


def _silhouette_check(bad):
    """Топология силуэта и силуэт: мост против питоновского эталона."""
    import numpy as np

    import controller
    import gv_cpp
    from silhouette import _build_silhouette_topology, _mesh_silhouette_segments

    verts, _segs, _normals = _py_step_model()
    py = _build_silhouette_topology(verts)
    cpp = gv_cpp.build_silhouette_topology(verts)
    same = all(np.array_equal(np.asarray(getattr(py, key)),
                              np.asarray(getattr(cpp, key)))
               for key in ("ea", "eb", "f0", "f1", "n", "d"))
    print("топология силуэта: рёбер %d, граней %d, python %s  cpp %s  %s"
          % (len(cpp.f0), len(cpp.n), _sha(np.asarray(py.f0), np)[:16],
             _sha(np.asarray(cpp.f0), np)[:16], "совпало" if same else "РАСХОЖДЕНИЕ"))
    if not same:
        bad.append("топология силуэта разошлась с эталоном")

    eyes = [(100.0, 150.0, 300.0), (-200.0, 40.0, 260.0), (60.0, -180.0, 120.0),
            (240.0, 300.0, -60.0), (0.0, 0.0, 400.0)]
    for i, eye in enumerate(eyes):
        want = np.asarray(_mesh_silhouette_segments(py, eye), dtype=np.float32)
        got = gv_cpp.mesh_silhouette_segments(cpp, eye)
        if not (want.shape == got.shape and np.array_equal(want, got)):
            bad.append("силуэт на глазу %d разошёлся с эталоном" % i)
        else:
            print("ok    силуэт eye%d: %d рёбер, python %s = cpp %s"
                  % (i, got.shape[0], _sha(want, np)[:16], _sha(got, np)[:16]))

    # Ветку выбирает gv_cpp: с отключённым питоновским эталоном результат может
    # дать только C++-путь, и он обязан совпасть с эталонным.
    saved_topo = controller._build_silhouette_topology
    saved_seg = controller._mesh_silhouette_segments

    def boom(*_args, **_kwargs):
        raise RuntimeError("питоновский силуэт отключён проверкой")

    controller._build_silhouette_topology = boom
    controller._mesh_silhouette_segments = boom
    try:
        via = controller.GcodeToolpath._mesh_topology(verts)
        via_seg = controller.GcodeToolpath._mesh_segments(via, eyes[0])
    finally:
        controller._build_silhouette_topology = saved_topo
        controller._mesh_silhouette_segments = saved_seg
    if not (via is not None and np.array_equal(np.asarray(via.f0), np.asarray(py.f0))
            and np.array_equal(np.asarray(via_seg),
                               np.asarray(_mesh_silhouette_segments(py, eyes[0])))):
        bad.append("силуэт с CNC_CPP=silhouette не пошёл на C++ (эталон отключён)")
    else:
        print("ok    силуэт с CNC_CPP=silhouette считает мост (эталон отключён)")


def _tangents_check(bad):
    """Семейства касательных и отрезки: мост против питоновского эталона."""
    import numpy as np

    import controller
    import gv_cpp
    import tangents
    from step_surfaces import load_step_surfaces

    verts, segments, normals = _py_step_model()
    packed = _pack(segments)
    py_surfs = load_step_surfaces(STEP)
    cpp_surfs = gv_cpp.load_step_surfaces(STEP)
    eyes = [(100.0, 150.0, 300.0), (-200.0, 40.0, 260.0), (60.0, -180.0, 120.0),
            (240.0, 300.0, -60.0), (0.0, 0.0, 400.0)]

    # Кожа и обводка есть: так семейства строит загрузка модели из кэша.
    variants = [("с кожей и обводкой", packed, (np.asarray(verts), np.asarray(normals))),
                ("без кожи", packed, None),
                ("без обводки", None, (np.asarray(verts), np.asarray(normals)))]
    py_fams = None
    for name, pk, skin in variants:
        want = tangents._tangent_families(py_surfs, pk, skin)
        got = gv_cpp.tangent_families(cpp_surfs, pk, skin)
        keys = ("O", "D", "E1", "E2", "R", "T0", "T1", "RREF", "A0", "A1",
                "FULL", "DROP", "ARC")
        diff = [key for key in keys
                if not _bits_equal(getattr(want.cylinders, key),
                                   getattr(got.cylinders, key), np)]
        cones_want = np.asarray([list(c.a) + list(c.d) + [c.psi, c.s0, c.s1]
                                 for c in want.cones], dtype=np.float64)
        cones_got = np.asarray([list(c.a) + list(c.d) + [c.psi, c.s0, c.s1]
                                for c in got.cones], dtype=np.float64)
        if not _bits_equal(cones_want, cones_got, np):
            diff.append("cones")
        same = not diff
        print("семейства %-20s цилиндров %d, конусов %d, python %s  cpp %s  %s"
              % (name, len(got.cylinders.O), len(got.cones),
                 _sha(np.asarray(want.cylinders.O), np)[:16],
                 _sha(np.asarray(got.cylinders.O), np)[:16],
                 "совпало" if same else "РАСХОЖДЕНИЕ: " + ",".join(diff)))
        if not same:
            bad.append("семейства касательных (%s) разошлись с эталоном" % name)
        if name == "с кожей и обводкой":
            py_fams = want
            cpp_fams = got

    for i, eye in enumerate(eyes):
        want = np.asarray(tangents._tangent_from_families(py_fams, eye), dtype=np.float64)
        got = gv_cpp.tangent_segments(cpp_fams, eye)
        if not (want.shape == got.shape and np.array_equal(want, got)):
            bad.append("касательные на глазу %d разошлись с эталоном" % i)
            print("FAIL  касательные eye%d: %s против %s" % (i, want.shape, got.shape))
        else:
            print("ok    касательные eye%d: %d отрезков, python %s = cpp %s"
                  % (i, got.shape[0], _sha(want, np)[:16], _sha(got, np)[:16]))

    # Загрузка модели строит семейства без кожи (skin=None): ветка тоже обязана
    # идти на C++ при отключённом питоновском эталоне.
    saved = (controller._tangent_families, controller._tangent_from_families)

    def boom(*_args, **_kwargs):
        raise RuntimeError("питоновские касательные отключены проверкой")

    controller._tangent_families = boom
    controller._tangent_from_families = boom
    try:
        via = controller.GcodeToolpath._tangents(cpp_surfs, packed)
        via_seg = controller.GcodeToolpath._tangent_segments(via, eyes[0])
    finally:
        controller._tangent_families, controller._tangent_from_families = saved
    want = np.asarray(tangents._tangent_from_families(
        tangents._tangent_families(py_surfs, packed, None), eyes[0]), dtype=np.float64)
    if not np.array_equal(np.asarray(via_seg), want):
        bad.append("касательные с CNC_CPP=silhouette не пошли на C++ (эталон отключён)")
    else:
        print("ok    касательные с CNC_CPP=silhouette считает мост (эталон отключён)")


def _parser_check(bad):
    """Разбор УП и подготовка траектории: мост против питоновского эталона."""
    import numpy as np

    import controller
    import gv_cpp
    from gcode_parser import parse_gcode
    from path_prep import _prepare_ordered

    with open(NGC, "r", encoding="utf-8", errors="ignore") as fh:
        text = fh.read()
    ordered, last, seg_count, stages = parse_gcode(text)
    py = _prepare_ordered(ordered, stages)
    cpp = gv_cpp.prepare_path(text)

    same = (py.seg_count == cpp.seg_count and py.last == cpp.last
            and np.array_equal(py.pts, cpp.pts)
            and np.array_equal(py.cum, cpp.cum)
            and np.array_equal(np.asarray(py.rapid, dtype=np.uint8),
                               np.asarray(cpp.rapid, dtype=np.uint8))
            and np.array_equal(py.speeds, cpp.speeds)
            and py.total == cpp.total and py.radius == cpp.radius
            and py.min_y == cpp.min_y and py.center == cpp.center
            and py.bounds == cpp.bounds
            and py.rapid_bytes == cpp.rapid_bytes and py.rapid_nv == cpp.rapid_nv
            and py.feed_bytes == cpp.feed_bytes and py.feed_nv == cpp.feed_nv
            and py.stages == cpp.stages)
    print("траектория: точек %d, отрезков %d, этапов %d  python %s  cpp %s  %s"
          % (cpp.pts.shape[0], cpp.seg_count, len(cpp.stages),
             _sha(py.rapid_bytes, np)[:16], _sha(cpp.rapid_bytes, np)[:16],
             "совпало" if same else "РАСХОЖДЕНИЕ"))
    if not same:
        bad.append("подготовка траектории разошлась с эталоном")

    # Ветку выбирает gv_cpp: с отключённым питоновским эталоном результат может
    # дать только C++-путь.
    saved = (controller.parse_gcode, controller._prepare_ordered)

    def boom(*_args, **_kwargs):
        raise RuntimeError("питоновский разбор УП отключён проверкой")

    controller.parse_gcode = boom
    controller._prepare_ordered = boom
    try:
        via = controller.GcodeToolpath._prepare_toolpath(text)
    finally:
        controller.parse_gcode, controller._prepare_ordered = saved
    if not (np.array_equal(via.pts, cpp.pts) and via.stages == cpp.stages
            and via.seg_count == cpp.seg_count):
        bad.append("_prepare_toolpath с CNC_CPP=parser не пошёл на C++ (эталон отключён)")
    else:
        print("ok    _prepare_toolpath с CNC_CPP=parser считает мост (эталон отключён)")


def _fallback_check(bad):
    """Битый мост: результат питоновский, причина отказа - в журнале."""
    env = dict(os.environ, CNC_CPP="all",
               CNC_CPP_LIB=os.path.join(HERE, "нет-такой-библиотеки.dll"))
    res = subprocess.run([sys.executable, "-c", FALLBACK_SNIPPET.format(here=HERE)],
                         capture_output=True, env=env, cwd=ROOT)
    # Байты, а не текст: кодировку вывода подпроцесса задаёт PYTHONIOENCODING
    # вызывающего, и чтение её локалью превратило бы русскую строку в мусор.
    raw = res.stdout + res.stderr
    out = raw.decode("utf-8", "replace")
    if "мост не загружен" not in out:
        out = raw.decode("cp1251", "replace")
    seen = "мост не загружен" in out
    ok = res.returncode == 0 and "FALLBACK OK" in out and seen
    print("фолбэк: код %d, предупреждение в журнале %s" % (res.returncode, seen))
    if not ok:
        bad.append("фолбэк на Python не сработал (код %d): %s"
                   % (res.returncode, out.strip()[:400]))


def _python_checks(bad):
    """Питоновские проверки под CNC_CPP=all: результат не меняется."""
    env = dict(os.environ, CNC_CPP="all")
    for name in ("test_normals_golden.py", "test_normals_orientation.py",
                 "test_tangent_policy.py"):
        res = subprocess.run([sys.executable, os.path.join(HERE, name)],
                             capture_output=True, text=True, env=env, cwd=ROOT)
        tail = (res.stdout + res.stderr).strip().splitlines()[-1:]
        print("      %-28s код %d  %s" % (name, res.returncode, tail[0] if tail else ""))
        if res.returncode != 0:
            bad.append("%s под CNC_CPP=all вернул %d" % (name, res.returncode))


def main() -> int:
    import gv_cpp

    if not os.path.exists(STEP) or not os.path.exists(STL):
        print("данных нет: пропущено (нет %s)" % DATA)
        return 0
    if not os.path.exists(NGC):
        print("данных нет: пропущено (нет %s)" % NGC)
        return 0
    if not gv_cpp.available():
        print("мост: пропущено (нет %s; соберите scripts/build-viewer.ps1)"
              % gv_cpp.library_path())
        return 0

    print("мост: %s  ABI %d  OCCT %s"
          % (gv_cpp.library_path(), gv_cpp.ABI_VERSION, gv_cpp.occt_bin_dir()))
    bad = []
    _flag_checks(bad)
    _stl_normals_check(bad)
    _step_check(bad)
    _surfaces_check(bad)
    _silhouette_check(bad)
    _tangents_check(bad)
    _parser_check(bad)
    _fallback_check(bad)
    print("питоновские проверки под CNC_CPP=all:")
    _python_checks(bad)

    os.environ.pop("CNC_CPP", None)
    if bad:
        for line in bad:
            print("FAIL: " + line)
        print("RESULT: FAIL (%d)" % len(bad))
        return 1
    print("RESULT: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
