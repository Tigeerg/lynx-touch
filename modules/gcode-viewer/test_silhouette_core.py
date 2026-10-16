#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сверка C++-топологии силуэта и силуэта на глазах (этап 4c) с питоновским эталоном.

Эталон - modules/gcode-viewer/silhouette.py (_build_silhouette_topology и
_mesh_silhouette_segments). C++-зеркало - cpp/core/silhouette.* (сварка вершин по
сетке 1e-4 мм, рёбра ровно с двумя гранями, отсев острых кромок и вырожденных
граней, знаковый тест s = n·E - d); пробник cpp/silhouette_probe.cpp берёт суп
вершин из STEP через gvstep - тот же путь, что у load_step_model.

Что сравнивается и почему так:
  * массивы топологии (n, d, f0, f1, ea, eb) - побайтово и поэлементно: порядок
    кандидатов задан лексикографической сортировкой ключей np.unique, а ea/eb -
    это представители кластеров сварки, то есть часть результата, а не служебные
    числа; поэтому сравниваются байты, а не набор рёбер;
  * силуэт на пяти фиксированных глазах (те же, что в oracle_dump.py) - тоже
    побайтово: знаковый тест считает float32, и расхождение в младшем бите
    сдвинуло бы набор рёбер у рёбер с s около нуля;
  * sha256 каждого массива - отдельно и с двух сторон: у C++ (печать пробника)
    и у оракула (oracle_manifest.json). Совпадение с оракулом доказывает, что не
    сдвинулся и питоновский эталон, и перенос.

Числа на Test_model.STEP: 136194 вершин, 45398 треугольников, 22639 сваренных
вершин, 57842 кандидатных ребра; силуэт на глазах - 245, 224, 251, 509, 185 рёбер.

Без тулчейна или собранного пробника проверка пропускается с кодом 0: сборку
стережёт tools/check.ps1.

Запуск: python modules/gcode-viewer/test_silhouette_core.py (exit 0 = пойдено)
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
PROBE = os.path.join(ROOT, "build", "edges", "silhouette_probe.exe")
MANIFEST = os.path.join(HERE, "oracle_manifest.json")

# Те же пять глаз, что в oracle_dump.py: расхождение сдвинуло бы эталон.
EYES = ((100.0, 150.0, 300.0), (-200.0, 40.0, 260.0), (60.0, -180.0, 120.0),
        (240.0, 300.0, -60.0), (0.0, 0.0, 400.0))

# Ключ пробника -> ключ оракула -> файл дампа -> dtype (как у numpy.tobytes()).
TOPO_KEYS = (
    ("n", "topo.n", "topo_n.f32", "<f4"),
    ("d", "topo.d", "topo_d.f32", "<f4"),
    ("f0", "topo.f0", "topo_f0.i32", "<i4"),
    ("f1", "topo.f1", "topo_f1.i32", "<i4"),
    ("ea", "topo.ea", "topo_ea.f32", "<f4"),
    ("eb", "topo.eb", "topo_eb.f32", "<f4"),
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
    """Питоновский эталон: массивы топологии, силуэты на глазах и их sha256."""
    from mock_backend import _build_silhouette_topology, _mesh_silhouette_segments
    from mock_backend import load_step_model

    verts, _segments, _normals = load_step_model(STEP)
    topo = _build_silhouette_topology(verts)
    ref = {
        "n": np.asarray(topo.n, dtype=np.float32),
        "d": np.asarray(topo.d, dtype=np.float32),
        "f0": np.asarray(topo.f0, dtype=np.int32),
        "f1": np.asarray(topo.f1, dtype=np.int32),
        "ea": np.asarray(topo.ea, dtype=np.float32),
        "eb": np.asarray(topo.eb, dtype=np.float32),
    }
    for i, eye in enumerate(EYES):
        segs = _mesh_silhouette_segments(topo, eye)
        key = "eye%d" % i
        ref[key] = np.asarray(segs if segs is not None else np.empty((0, 2, 3)),
                              dtype=np.float32)
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
        return ["элементов %d против эталонных %d" % (got.size, want_flat.size)], digest
    if digest != _digest(want_flat):
        ints = np.int32 if dtype in ("<f4", "<i4") else np.int64
        count = int((got.view(ints) != want_flat.view(ints)).sum())
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

    dump_dir = tempfile.mkdtemp(prefix="gvsilhouette_probe_")
    bad = []
    try:
        out = _run_probe(dump_dir)
        faces = ref["n"].shape[0]
        if int(out.get("triangles", -1)) != faces:
            bad.append("треугольников C++ %s против эталонных %d"
                       % (out.get("triangles", "?"), faces))
        if int(out.get("verts", -1)) != faces * 3:
            bad.append("вершин C++ %s против эталонных %d" % (out.get("verts", "?"), faces * 3))
        if int(out.get("candidates", -1)) != ref["f0"].shape[0]:
            bad.append("кандидатов C++ %s против эталонных %d"
                       % (out.get("candidates", "?"), ref["f0"].shape[0]))

        for key, oracle_key, dump_name, dtype in TOPO_KEYS:
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
            print("topo.%-3s эталон %-14s %-8s sha256 %s  C++ %s  %s"
                  % (key, str(list(arr.shape)), str(arr.dtype), ref_hash[key][:16],
                     digest[:16], "совпало" if not problems else "РАСХОЖДЕНИЕ"))
            bad += ["%s: %s" % (key, p) for p in problems]

        for i, _eye in enumerate(EYES):
            key = "eye%d" % i
            oracle_key = "silhouette." + key
            arr = ref[key]
            problems, digest = _compare_dump(
                os.path.join(dump_dir, "silhouette_%s.f32" % key), "<f4", arr, np)
            probe_hash = out.get("sha256_" + key, "?")
            if int(out.get(key, -1)) != arr.shape[0]:
                problems.append("рёбер C++ %s против эталонных %d"
                                % (out.get(key, "?"), arr.shape[0]))
            if probe_hash != digest:
                problems.append("sha256 пробника %s против дампа %s"
                                % (probe_hash[:16], digest[:16]))
            if digest != ref_hash[key]:
                problems.append("sha256 C++ %s против питоновского эталона %s"
                                % (digest[:16], ref_hash[key][:16]))
            saved = manifest.get(oracle_key, {}).get("sha256", "")
            if digest != saved:
                problems.append("sha256 C++ %s против оракула %s" % (digest[:16], saved[:16]))
            print("%-6s эталон %-14s %-8s sha256 %s  C++ %s  %s"
                  % (key, str(list(arr.shape)), str(arr.dtype), ref_hash[key][:16],
                     digest[:16], "совпало" if not problems else "РАСХОЖДЕНИЕ"))
            bad += ["%s: %s" % (key, p) for p in problems]

        # Оракул - не улика, а требование: ключи обязаны быть на месте.
        for _key, oracle_key, _dump, _dtype in TOPO_KEYS:
            if oracle_key not in manifest:
                bad.append("в оракуле нет %s" % oracle_key)
        for i, _eye in enumerate(EYES):
            if "silhouette.eye%d" % i not in manifest:
                bad.append("в оракуле нет silhouette.eye%d" % i)
    finally:
        shutil.rmtree(dump_dir, ignore_errors=True)

    if bad:
        for line in bad:
            print("FAIL: " + line)
        return 1
    print("топология силуэта и силуэт на пяти глазах совпали с эталоном побайтово; "
          "sha256 сошлись с silhouette.py и с оракулом")
    print("RESULT: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
