#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Оракул промежуточных результатов: эталон для переноса бэкенда на C++.

Считает промежуточные массивы Python-пути на тестовых данных и сверяет их хэши с
записанным манифестом. В репозитории лежит только манифест: сами массивы - это
десятки мегабайт, их получают прогоном этого же скрипта. Так перенос сверяется по
шагам (нормали, топология силуэта, силуэт на фиксированных глазах), а не по
конечной картинке.

    python modules/gcode-viewer/oracle_dump.py           # сверка, exit 0 = пойдено
    python modules/gcode-viewer/oracle_dump.py --write   # перезаписать манифест

Манифест перезаписывают осознанно, вместе с правкой, которая изменила результат;
проверка ловит расхождение как регресс.
"""
import hashlib
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import numpy as np  # noqa: E402

import mock_backend as mb  # noqa: E402

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
                out["topo." + key] = np.asarray(topo[key])
            for i, eye in enumerate(EYES):
                segs = mb._mesh_silhouette_segments(topo, eye)
                out["silhouette.eye%d" % i] = np.asarray(
                    segs if segs is not None else np.empty((0, 2, 3)), dtype=np.float32)

    if os.path.exists(NGC):
        with open(NGC, "r", encoding="utf-8", errors="ignore") as fh:
            text = fh.read()
        ordered, last, seg_count, stages = mb.parse_gcode(text)
        out["path.points"] = np.asarray([p[0] for p in ordered], dtype=np.float64)
        out["path.flags"] = np.asarray(
            [[1.0 if p[1] else 0.0, float(p[2])] for p in ordered], dtype=np.float64)
        out["path.last"] = np.asarray(last, dtype=np.float64)
        out["path.segments"] = np.asarray([seg_count], dtype=np.int64)
        out["path.stages"] = np.asarray([str((s[0], s[1])) for s in stages], dtype="U64")

    if os.path.exists(STL):
        soup = np.asarray(mb.load_stl_vertices(STL), dtype=np.float64)
        out["stl.soup"] = soup
        raw, nv, _bounds = mb._feature_edges_np(soup, 20.0)
        segs = mb.packed_positions((raw, nv, _bounds))
        out["stl.feature_edges"] = np.asarray(
            segs if segs is not None else np.empty((0, 2, 3)), dtype=np.float32)

    return out


def main() -> int:
    if mb._np is None:
        print("numpy отсутствует - оракул не считается")
        return 0

    arrays = collect()
    manifest = {name: _digest(arr) for name, arr in sorted(arrays.items())}

    if "--write" in sys.argv:
        with open(MANIFEST, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(manifest, fh, ensure_ascii=False, indent=2, sort_keys=True)
            fh.write("\n")
        print("манифест записан: %s (%d записей)" % (MANIFEST, len(manifest)))
        for name in sorted(manifest):
            print("  %-22s %-14s %s" % (name, str(manifest[name]["shape"]),
                                        manifest[name]["sha256"][:16]))
        return 0

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
