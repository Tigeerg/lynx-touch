#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сверка C++-аналитических поверхностей STEP и семейств касательных с эталоном.

Эталон - сам питоновский путь: modules/gcode-viewer/step_surfaces.py
(load_step_surfaces), tangents.py (_tangent_families, _tangent_from_families) и
gv_types.py (CylinderFamilies/TangentFamilies). Ключей step_surfaces/tangents в
oracle_manifest.json нет: оракул снят с других промежуточных массивов, здесь
сравнение идёт напрямую с Python - и поэлементно, и по sha256 (та же
сериализация, что у numpy .tobytes()).

C++-зеркало - cpp/occt/step_surfaces.* (поверхности вращения на OCCT SDK) и
cpp/core/tangents.* (семейства и кадровый проход в чистой арифметике), пробник
cpp/tangents_probe.cpp.

Что сравнивается и почему так:
  * список поверхностей (вид грани 0/1 в порядке обхода, поля цилиндров и
    конусов) - побайтово: порядок обхода граней TopExp_Explorer и есть результат,
    по нему собираются семейства;
  * массивы семейств (O, D, E1, E2, R, T0, T1, RREF, A0, A1, FULL, DROP, ARC) -
    побайтово: RREF задаёт общий азимут коаксиальной группы, DROP снимает
    генераторы у стыковых рёбер B-rep, ARC режет их по аналитической дуге -
    расхождение в младшем бите сдвинуло бы отрезки кадрового прохода;
  * отрезки на пяти фиксированных глазах (те же глаза, что в oracle_dump.py) -
    тоже побайтово: это конечный результат слоя.

Вход пробника: упакованные рёбра STEP (packed_edges.f32) пишет сама проверка из
_pack_edge_segments(load_step_model(...)) - ровно так их берёт bench_viewer.py;
числа этих рёбер уже сверены с эталоном в test_stepledges_plugin.py.

Числа на Test_model.STEP: 194 поверхности (180 цилиндров, 14 конусов), 124
семейства цилиндров, 30 полных, 2 конуса после политики и склейки; на пяти глазах
- 57, 58, 53, 53 и 56 отрезков. На Test_model_2.STEP: 6 цилиндров, 5 семейств,
все полные, конусов нет; на глазах - 3, 4, 4, 3 и 4 отрезка.

Без тулчейна или собранного пробника проверка пропускается с кодом 0: сборку
стережёт tools/check.ps1.

Запуск: python modules/gcode-viewer/test_tangents_core.py (exit 0 = пойдено)
"""
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

PROBE = os.path.join(ROOT, "build", "edges", "tangents_probe.exe")
PACKED = "packed_edges.f32"

# Модели в порядке проверки; отсутствие любой из них не ошибка (Test_model_2 -
# приёмочная модель силуэта, её может не быть в дереве).
MODELS = (("Test_model", os.path.join(ROOT, "data", "cad-testdata", "Test_model.STEP")),
          ("Test_model_2", os.path.join(ROOT, "data", "cad-testdata", "Test_model_2.STEP")))

# Те же пять глаз, что в oracle_dump.py: расхождение сдвинуло бы эталон.
EYES = ((100.0, 150.0, 300.0), (-200.0, 40.0, 260.0), (60.0, -180.0, 120.0),
        (240.0, 300.0, -60.0), (0.0, 0.0, 400.0))


def _probe_env():
    """PATH для пробника: DLL OCCT в каталоге сборки не лежат. Qt пробнику не нужен."""
    occ = os.environ.get("CNC_OCC_DIR") or r"C:\Users\Tiger\anaconda3\envs\gcode-cpp\Library"
    return dict(os.environ,
                PATH=os.pathsep.join([os.path.join(occ, "bin"),
                                      os.environ.get("PATH", "")]))


def _run_probe(step_path, dump_dir):
    """Запуск пробника -> словарь его строк вывода."""
    res = subprocess.run([PROBE, step_path, dump_dir], capture_output=True, text=True,
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
    """sha256 байтов массива - та же сериализация, что у numpy .tobytes()."""
    return hashlib.sha256(arr.tobytes()).hexdigest()


def _reference(np, step_path):
    """Питоновский эталон: поверхности, семейства, отрезки на глазах и вход пробника."""
    from mock_backend import (load_step_model, load_step_surfaces,  # noqa: E402
                              _tangent_families, _tangent_from_families,
                              _pack_edge_segments, CylinderSurface, ConeSurface)

    verts, segments, normals = load_step_model(step_path)
    packed = _pack_edge_segments(segments)
    surfs = load_step_surfaces(step_path)

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

    fams = _tangent_families(surfs, packed, (verts, normals))
    arr = fams.cylinders

    def cone_rows(items):
        return np.array([[c.a[0], c.a[1], c.a[2], c.d[0], c.d[1], c.d[2],
                          c.psi, c.s0, c.s1] for c in items],
                        dtype=float).reshape(-1, 9)

    ref = {
        "surfs_kind": (kind, "surfs_kind.i32", "<i4"),
        "surfs_cyl": (np.array(cyl, dtype=float).reshape(-1, 17), "surfs_cyl.f64", "<f8"),
        "surfs_cone": (np.array(cone, dtype=float).reshape(-1, 9), "surfs_cone.f64", "<f8"),
        "fam_O": (np.asarray(arr.O, dtype=float).reshape(-1, 3), "fam_O.f64", "<f8"),
        "fam_D": (np.asarray(arr.D, dtype=float).reshape(-1, 3), "fam_D.f64", "<f8"),
        "fam_E1": (np.asarray(arr.E1, dtype=float).reshape(-1, 3), "fam_E1.f64", "<f8"),
        "fam_E2": (np.asarray(arr.E2, dtype=float).reshape(-1, 3), "fam_E2.f64", "<f8"),
        "fam_R": (np.asarray(arr.R, dtype=float).reshape(-1), "fam_R.f64", "<f8"),
        "fam_T0": (np.asarray(arr.T0, dtype=float).reshape(-1), "fam_T0.f64", "<f8"),
        "fam_T1": (np.asarray(arr.T1, dtype=float).reshape(-1), "fam_T1.f64", "<f8"),
        "fam_RREF": (np.asarray(arr.RREF, dtype=float).reshape(-1), "fam_RREF.f64", "<f8"),
        "fam_A0": (np.asarray(arr.A0, dtype=float).reshape(-1), "fam_A0.f64", "<f8"),
        "fam_A1": (np.asarray(arr.A1, dtype=float).reshape(-1), "fam_A1.f64", "<f8"),
        "fam_FULL": (np.asarray(arr.FULL, dtype=np.uint8).reshape(-1), "fam_FULL.u8", "u1"),
        "fam_DROP": (np.asarray(arr.DROP, dtype=float).reshape(-1, 8), "fam_DROP.f64", "<f8"),
        "fam_ARC": (np.asarray(arr.ARC, dtype=np.uint8).reshape(-1, 64), "fam_ARC.u8", "u1"),
        "cone": (cone_rows(fams.cones), "cone.f64", "<f8"),
    }
    for i, eye in enumerate(EYES):
        segs = _tangent_from_families(fams, eye)
        ref["tangent%d" % i] = (np.array(segs, dtype=float).reshape(-1, 2, 3),
                                "tangent%d.f64" % i, "<f8")

    counts = {
        "surfaces": len(surfs),
        "cylinders": int((kind == 0).sum()),
        "cones": int((kind == 1).sum()),
        "families": int(np.asarray(arr.O).reshape(-1, 3).shape[0]),
        "full": int(np.asarray(arr.FULL, dtype=np.uint8).sum()),
        "tangent_cones": len(fams.cones),
    }
    for i in range(len(EYES)):
        counts["tangent%d" % i] = ref["tangent%d" % i][0].shape[0]
    return ref, packed, counts


def _compare_dump(path, dtype, want, np):
    """Дамп пробника против эталонного массива: размер, байты и поэлементно."""
    got = np.fromfile(path, dtype=dtype)
    want_flat = want.reshape(-1)
    digest = _digest(got)
    problems = []
    if got.size != want_flat.size:
        return ["элементов %d против эталонных %d" % (got.size, want_flat.size)], digest
    if digest != _digest(want_flat):
        # Битовое сравнение: у float NaN != NaN, а расхождение в младшем бите
        # обязано быть видно.
        if got.dtype.kind == "f":
            count = int(np.count_nonzero(got.view(np.uint64) != want_flat.view(np.uint64)))
        else:
            count = int(np.count_nonzero(got != want_flat))
        delta = np.abs(got.astype(np.float64) - want_flat.astype(np.float64))
        problems.append("расходится элементов %d из %d, максимум |dx| = %.17g"
                        % (count, got.size, float(delta.max()) if count else 0.0))
    return problems, digest


def _check_model(np, name, step_path, bad):
    """Одна модель: эталон, прогон пробника, сверка дампов и чисел."""
    ref, packed, counts = _reference(np, step_path)
    if counts["surfaces"] == 0:
        # load_step_surfaces глотает отказ импорта OCP и отдаёт пустой список:
        # эталон посчитать нечем - проверка пропускается, а не краснеет.
        raise ImportError("load_step_surfaces вернул пусто")

    dump_dir = tempfile.mkdtemp(prefix="gvtangents_probe_")
    try:
        with open(os.path.join(dump_dir, PACKED), "wb") as fh:
            fh.write(packed.raw)
        out = _run_probe(step_path, dump_dir)

        for key, want in counts.items():
            got = out.get(key, "?")
            if not got.isdigit() or int(got) != want:
                bad.append("%s: %s C++ %s против эталонных %d" % (name, key, got, want))

        for key, (arr, dump_name, dtype) in ref.items():
            problems, digest = _compare_dump(os.path.join(dump_dir, dump_name), dtype, arr, np)
            probe_hash = out.get("sha256_" + key, "?")
            if probe_hash != digest:
                problems.append("sha256 пробника %s против дампа %s"
                                % (probe_hash[:16], digest[:16]))
            if digest != _digest(arr.reshape(-1)):
                problems.append("sha256 C++ %s против питоновского эталона %s"
                                % (digest[:16], _digest(arr.reshape(-1))[:16]))
            print("%-13s %-11s эталон %-12s %-7s sha256 %s  C++ %s  %s"
                  % (name, key, str(list(arr.shape)), str(arr.dtype),
                     _digest(arr.reshape(-1))[:16], digest[:16],
                     "совпало" if not problems else "РАСХОЖДЕНИЕ"))
            bad += ["%s: %s: %s" % (name, key, p) for p in problems]
    finally:
        shutil.rmtree(dump_dir, ignore_errors=True)


def main() -> int:
    if not os.path.exists(PROBE):
        print("пропущено: пробник не собран (%s), соберите scripts/build-viewer.ps1" % PROBE)
        return 0
    models = [(name, path) for name, path in MODELS if os.path.exists(path)]
    if not models:
        print("пропущено: нет тестовых моделей STEP в data/cad-testdata")
        return 0
    try:
        import numpy as np
    except ImportError as exc:                    # нет numpy
        print("пропущено: нет numpy (%s)" % exc)
        return 0

    bad = []
    for name, step_path in models:
        try:
            _check_model(np, name, step_path, bad)
        except ImportError as exc:                # нет cadquery-ocp
            print("пропущено: %s (%s)" % (name, exc))
            continue

    if bad:
        for line in bad:
            print("FAIL: " + line)
        return 1
    print("поверхности STEP, семейства касательных и отрезки на пяти глазах совпали "
          "с питоновским эталоном побайтово: %s"
          % ", ".join(name for name, _path in models))
    print("RESULT: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
