#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сверка C++-плагина CNC.Edges (StepEdges) с питоновским эталоном рёбер STEP.

Эталон — mock_backend.load_step_model: рёбра B-Rep отбираются по швам
криволинейных граней, дискретизируются GCPnts_UniformDeflection (0.05 мм) и
склеиваются (_coalesce_segments). Плагин повторяет этот путь
(modules/gcode-viewer/cpp/stepledgesgeometry.cpp), а cpp/stepledges_probe.cpp
печатает число отрезков и sha256 буфера точек и умеет отдать буфер дампом.

Числа на Test_model.STEP: эталон 2426 отрезков, плагин 2426 (до склейки 4884).

Что сравнивается и почему так:
  * число отрезков — равенство (2426);
  * буфер точек — float32 little-endian, тот же формат, что у геометрии на GPU;
    обход в плагине идёт своим порядком, а кластеры склейки в эталоне
    упорядочены своим ключом (устойчивый argsort), поэтому буферы сравниваются
    как множество 24-байтовых кусков (две точки по три float32);
  * перед сравнением координаты округляются до 1e-4 мм и убирается знак нуля:
    у 16 отрезков из 2426 эталон несёт остаток 1e-15 от порядка суммирования в
    numpy (BLAS/FMA), плагин в том же месте даёт точный нуль. Разница в младших
    битах double, на экране её нет, но побайтовый хэш она ломает.
Порядок сборки буфера печатается как улика: с устойчивой сортировкой в эталоне
он совпадает на 2362 позициях из 2426.

Тулчейн берётся из окружения (CNC_QT_DIR, CNC_OCC_DIR — те же умолчания, что у
scripts/build-viewer.ps1). Отдельно проверяется, что движок QML открывает модуль:
пробник с ключом --qml делает import CNC.Edges 1.0 и создаёт элемент StepEdges.
Без собранного пробника проверка пропускается с кодом 0: сборку стережёт
tools/check.ps1.

Запуск: python modules/gcode-viewer/test_stepledges_plugin.py (exit 0 = пойдено)
"""
import hashlib
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

STEP = os.path.join(ROOT, "data", "cad-testdata", "Test_model.STEP")
PROBE = os.path.join(ROOT, "build", "edges", "stepledges_probe.exe")
MODULE_DIR = os.path.join(ROOT, "build", "edges", "CNC", "Edges")

CHUNK = 24                  # отрезок: две точки по три float32
ROUND_MM = 4                # округление координат перед сравнением, знаков после запятой


def _probe_env():
    """PATH для пробника: DLL OCCT и Qt в каталоге сборки не лежат."""
    qt = os.environ.get("CNC_QT_DIR") or r"C:\Qt\6.12.0\msvc2022_64"
    occ = os.environ.get("CNC_OCC_DIR") or r"C:\Users\Tiger\anaconda3\envs\gcode-cpp\Library"
    return dict(os.environ,
                PATH=os.pathsep.join([MODULE_DIR, os.path.join(qt, "bin"),
                                      os.path.join(occ, "bin"),
                                      os.environ.get("PATH", "")]))


def _run_probe(dump_path):
    """Запуск пробника -> словарь его строк вывода."""
    res = subprocess.run([PROBE, STEP, dump_path], capture_output=True, text=True,
                         env=_probe_env())
    if res.returncode != 0:
        raise RuntimeError("пробник вернул %d\n%s%s"
                           % (res.returncode, res.stdout, res.stderr))
    out = {}
    for line in res.stdout.splitlines():
        if ":" in line:
            key, _, value = line.partition(":")
            out[key.strip()] = value.strip()
    return out


def _check_qml_import():
    """Модуль должен открываться движком QML: import CNC.Edges 1.0 + элемент StepEdges."""
    import_path = os.path.join(ROOT, "build", "edges")
    res = subprocess.run([PROBE, "--qml", import_path], capture_output=True, text=True,
                         env=_probe_env())
    if res.returncode != 0:
        return res.stdout.strip() + res.stderr.strip()
    return ""


def _canon(arr_f32, numpy):
    """Куски по 24 байта: координаты до 1e-4 мм, знак нуля снят."""
    q = numpy.round(numpy.asarray(arr_f32, dtype="<f4"), ROUND_MM)
    q = numpy.where(q == 0.0, numpy.float32(0.0), q)
    buf = numpy.ascontiguousarray(q, dtype="<f4").tobytes()
    return [buf[i:i + CHUNK] for i in range(0, len(buf), CHUNK)]


def _digest(chunks):
    return hashlib.sha256(b"".join(sorted(chunks))).hexdigest()


def main() -> int:
    if not os.path.exists(STEP):
        print("STEP: пропущено (нет %s)" % STEP)
        return 0
    if not os.path.exists(PROBE):
        print("пробник: пропущено (не собран; соберите scripts/build-viewer.ps1)")
        return 0

    import numpy as np                                       # noqa: E402
    from mock_backend import load_step_model                 # noqa: E402

    _verts, segments, _normals = load_step_model(STEP)
    ref = np.asarray(segments, dtype=np.float64)
    if ref.ndim != 3 or ref.shape[1:] != (2, 3):
        print("FAIL: эталон вернул %s, ожидалось (N, 2, 3)" % (ref.shape,))
        return 1

    dump = os.path.join(tempfile.gettempdir(), "stepledges_probe_dump.bin")
    out = _run_probe(dump)
    plug = np.fromfile(dump, dtype="<f4").reshape(-1, 2, 3)
    count = int(out["segments"])
    raw = int(out["raw_segments"])

    ref_chunks = _canon(ref, np)
    plug_chunks = _canon(plug, np)
    ref_hash = _digest(ref_chunks)
    plug_hash = _digest(plug_chunks)

    same_pos = sum(1 for i in range(min(len(ref_chunks), len(plug_chunks)))
                   if ref_chunks[i] == plug_chunks[i])

    bad = []
    if count != ref.shape[0]:
        bad.append("отрезков %d против эталонных %d" % (count, ref.shape[0]))
    if len(plug_chunks) != len(ref_chunks):
        bad.append("кусков %d против эталонных %d" % (len(plug_chunks), len(ref_chunks)))
    if plug_hash != ref_hash:
        bad.append("набор точек не совпал (хэш %s против %s)"
                   % (plug_hash[:16], ref_hash[:16]))
    qml_error = _check_qml_import()
    if qml_error:
        bad.append("движок QML не открыл модуль: " + qml_error)

    print("эталон:  отрезков %5d  набор %s" % (ref.shape[0], ref_hash[:16]))
    print("плагин:  отрезков %5d  набор %s  (до склейки %d)"
          % (count, plug_hash[:16], raw))
    print("порядок сборки: совпало позиций %d из %d" % (same_pos, len(plug_chunks)))
    ref_raw = hashlib.sha256(
        np.ascontiguousarray(ref, dtype="<f4").tobytes()).hexdigest()
    print("буфер как есть (порядок + знак нуля): эталон %s плагин %s"
          % (ref_raw[:16], out.get("sha256_order", "?")[:16]))
    print("модуль QML: import CNC.Edges 1.0 + StepEdges %s"
          % ("OK" if not qml_error else "FAIL"))
    if bad:
        for line in bad:
            print("FAIL: " + line)
        try:
            os.remove(dump)
        except OSError:
            pass
        return 1
    os.remove(dump)
    print("RESULT: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
