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
