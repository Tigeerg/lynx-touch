#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сверка C-ABI-моста gvbridge с питоновским эталоном (этап 5 плана переноса).

Проверяется то, что мост обязан держать:

  * флаг CNC_CPP: пусто/off - всё на Python, normals - нормали STL, step - ветка
    STEP, all - оба, незнакомое значение отключает C++ и видно в журнале;
  * нормали STL бит-в-бит (gv_cpp.smooth_face_normals против
    normals._smooth_face_normals) на супе Test_model.STL;
  * STEP бит-в-бит: суп вершин и crease-aware нормали (gv_cpp.load_step_model
    против step_import.load_step_model) и упакованный буфер обводки после
    питоновской склейки рёбер;
  * ветку выбирает мост: с выключенным питоновским эталоном (подмена на отказ)
    результат даёт только C++-путь, и наоборот;
  * отказ моста виден в журнале и не ломает результат: подпроцесс с битым
    CNC_CPP_LIB печатает предупреждение и считает питоновским путём;
  * питоновские проверки под CNC_CPP=all (test_normals_golden.py,
    test_normals_orientation.py) остаются зелёными.

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


def _flag_checks(bad):
    """Флаг CNC_CPP включает ровно тот слой, который просит."""
    import gv_cpp

    cases = [("", (False, False)), ("off", (False, False)), ("0", (False, False)),
             ("normals", (True, False)), ("step", (False, True)),
             ("all", (True, True)), ("мусор", (False, False))]
    for value, want in cases:
        os.environ["CNC_CPP"] = value
        got = (gv_cpp.normals_enabled(), gv_cpp.step_enabled())
        if got != want:
            bad.append("CNC_CPP=%r дал (нормали %s, STEP %s), ожидалось %s"
                       % (value, got[0], got[1], want))
            print("FAIL  CNC_CPP=%r -> %s, ожидалось %s" % (value, got, want))
        else:
            print("ok    CNC_CPP=%-8r -> нормали %-5s STEP %s"
                  % (value, got[0], got[1]))
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


def _fallback_check(bad):
    """Битый мост: результат питоновский, причина отказа - в журнале."""
    env = dict(os.environ, CNC_CPP="all",
               CNC_CPP_LIB=os.path.join(HERE, "нет-такой-библиотеки.dll"))
    res = subprocess.run([sys.executable, "-c", FALLBACK_SNIPPET.format(here=HERE)],
                         capture_output=True, text=True, env=env, cwd=ROOT)
    out = res.stdout + res.stderr
    seen = "мост не загружен" in out
    ok = res.returncode == 0 and "FALLBACK OK" in out and seen
    print("фолбэк: код %d, предупреждение в журнале %s" % (res.returncode, seen))
    if not ok:
        bad.append("фолбэк на Python не сработал (код %d): %s"
                   % (res.returncode, out.strip()[:400]))


def _python_checks(bad):
    """Питоновские проверки под CNC_CPP=all: результат не меняется."""
    env = dict(os.environ, CNC_CPP="all")
    for name in ("test_normals_golden.py", "test_normals_orientation.py"):
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
