#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сверка C++-слоя STEP и crease-aware нормалей (этап 4a) с питоновским эталоном.

Эталон - modules/gcode-viewer/step_import.py (load_step_model) и normals.py
(_smooth_face_normals). C++-зеркало - cpp/occt/step_import.* (тесселяция
BRepMesh_IncrementalMesh, суп вершин с учётом локации грани, разворот оболочки
наружу) и cpp/core/normals.* (сварка вершин и кластеризация по 20 градусов); цель
CMake gvstep, пробник cpp/step_probe.cpp.

Что сравнивается и почему так:
  * вершинный суп - побайтово и поэлементно (`load_step_model` против дампа
    пробника): критерий этапа 4a - те же массивы, а не похожие числа. Порядок
    обхода граней TopExp_Explorer и порядок вершин внутри треугольника здесь и
    есть результат, поэтому сравниваются байты, а не набор точек;
  * sha256 каждого массива - отдельно и с двух сторон: у C++ (печать пробника)
    и у оракула (`oracle_manifest.json`). Совпадение с оракулом доказывает, что
    не сдвинулся и питоновский эталон, и перенос;
  * нормали float32 - тоже побайтово: они идут прямо в GPU, и расхождение в
    младшем бите ломает уже принятое требование бит-в-бит (тест
    test_normals_golden.py держит тот же критерий для питоновской ветки);
  * рёбра STEP (step.segments) здесь не сверяются: они перенесены плагином
    CNC.Edges и проверяются test_stepledges_plugin.py.

Числа на Test_model.STEP: 136194 вершин, 45398 треугольников, 22603 сваренных
вершины, крупнейшая группа сварки - 82 слота.

Без тулчейна или собранного пробника проверка пропускается с кодом 0: сборку
стережёт tools/check.ps1.

Запуск: python modules/gcode-viewer/test_step_core.py (exit 0 = пойдено)
"""
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

STEP = os.path.join(ROOT, "data", "cad-testdata", "Test_model.STEP")
PROBE = os.path.join(ROOT, "build", "edges", "step_probe.exe")
MANIFEST = os.path.join(HERE, "oracle_manifest.json")

# Ключ пробника -> ключ оракула -> файл дампа -> dtype (как у numpy.tobytes()).
KEYS = (
    ("verts", "step.verts", "step_verts.f64", "<f8"),
    ("tri", "step.tri", "step_tri.f64", "<f8"),
    ("normals", "step.normals", "step_normals.f32", "<f4"),
)


def _probe_env():
    """PATH для пробника: DLL OCCT в каталоге сборки не лежат. Qt пробнику не нужен."""
    occ = os.environ.get("CNC_OCC_DIR") or r"C:\Users\Tiger\anaconda3\envs\gcode-cpp\Library"
    return dict(os.environ,
                PATH=os.pathsep.join([os.path.join(occ, "bin"),
                                      os.environ.get("PATH", "")]))


def _run_probe(dump_dir):
    """Запуск пробника -> словарь его строк вывода."""
    res = subprocess.run([PROBE, STEP, dump_dir], capture_output=True, text=True,
                         encoding="utf-8", errors="replace", env=_probe_env())
    if res.returncode != 0:
        raise RuntimeError("пробник вернул %d\n%s%s" % (res.returncode, res.stdout, res.stderr))
    out = {}
    for line in res.stdout.splitlines():
        if ":" in line:
            key, _, value = line.partition(":")
            out[key.strip()] = value.strip()
    return out


def _digest(arr):
    """sha256 байтов массива - та же сериализация, что в oracle_dump.py."""
    return hashlib.sha256(arr.tobytes()).hexdigest()


def _reference(np):
    """Питоновский эталон: массивы и их sha256."""
    from mock_backend import load_step_model                  # noqa: E402

    verts, _segments, normals = load_step_model(STEP)
    ref = {
        "verts": np.asarray(verts, dtype=np.float64),
        "tri": np.asarray(verts, dtype=np.float64).reshape(-1, 3, 3),
        "normals": np.asarray(normals, dtype=np.float32),
    }
    return ref, {key: _digest(arr) for key, arr in ref.items()}


def _compare_dump(path, dtype, want, np):
    """Дамп пробника против эталонного массива: байты и поэлементно.

    Возвращает (строки расхождений, sha256 дампа).
    """
    got = np.fromfile(path, dtype=dtype)
    want_flat = want.reshape(-1)
    digest = _digest(got)
    bad = []
    if got.size != want_flat.size:
        bad.append("элементов %d против эталонных %d" % (got.size, want_flat.size))
        return bad, digest
    if digest != _digest(want_flat):
        diff = got.view(np.int64) != want_flat.view(np.int64) if dtype == "<f8" \
            else got.view(np.int32) != want_flat.view(np.int32)
        count = int(diff.sum())
        delta = np.abs(got.astype(np.float64) - want_flat.astype(np.float64))
        bad.append("расходится элементов %d из %d, максимум |dx| = %.17g"
                   % (count, got.size, float(delta.max()) if count else 0.0))
    return bad, digest


def main() -> int:
    if not os.path.exists(STEP):
        print("пропущено: нет %s" % STEP)
        return 0
    if not os.path.exists(PROBE):
        print("пропущено: пробник не собран (%s), соберите scripts/build-viewer.ps1" % PROBE)
        return 0
    try:
        import numpy as np
        ref, ref_hash = _reference(np)
    except ImportError as exc:                    # нет numpy или cadquery-ocp
        print("пропущено: нет numpy/cadquery-ocp (%s)" % exc)
        return 0

    with open(MANIFEST, "r", encoding="utf-8") as fh:
        manifest = json.load(fh)

    dump_dir = tempfile.mkdtemp(prefix="gvstep_probe_")
    bad = []
    try:
        out = _run_probe(dump_dir)
        faces = ref["tri"].shape[0]
        if int(out.get("verts", -1)) != ref["verts"].shape[0]:
            bad.append("вершин C++ %s против эталонных %d"
                       % (out.get("verts", "?"), ref["verts"].shape[0]))
        if int(out.get("triangles", -1)) != faces:
            bad.append("треугольников C++ %s против эталонных %d"
                       % (out.get("triangles", "?"), faces))

        for key, oracle_key, dump_name, dtype in KEYS:
            arr = ref[key]
            problems, digest = _compare_dump(os.path.join(dump_dir, dump_name), dtype, arr, np)
            probe_hash = out.get("sha256_" + key, "?")
            if probe_hash != digest:
                problems.append("sha256 пробника %s против дампа %s"
                                % (probe_hash[:16], digest[:16]))
            if digest != ref_hash[key]:
                problems.append("sha256 C++ %s против питоновского эталона %s"
                                % (digest[:16], ref_hash[key][:16]))
            saved = manifest.get(oracle_key, {}).get("sha256", "")
            if digest != saved:
                problems.append("sha256 C++ %s против оракула %s" % (digest[:16], saved[:16]))
            print("%-8s эталон %-14s %-8s sha256 %s  C++ %s  %s"
                  % (key, str(list(arr.shape)), str(arr.dtype), ref_hash[key][:16],
                     digest[:16], "совпало" if not problems else "РАСХОЖДЕНИЕ"))
            bad += ["%s: %s" % (key, p) for p in problems]

        # Оракул - не улика, а требование: ключи обязаны быть на месте.
        for _key, oracle_key, _dump, _dtype in KEYS:
            if oracle_key not in manifest:
                bad.append("в оракуле нет %s" % oracle_key)
    finally:
        shutil.rmtree(dump_dir, ignore_errors=True)

    if bad:
        for line in bad:
            print("FAIL: " + line)
        return 1
    print("суп вершин, треугольники и нормали совпали с эталоном побайтово; "
          "sha256 сошлись с load_step_model и с оракулом")
    print("RESULT: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
