#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сверка C++-слоя STL и обводки меша (этап 4b) с питоновским эталоном.

Эталон - modules/gcode-viewer/stl_import.py (load_stl_vertices, _verts_bounds) и
outline.py (_feature_edges_np со склейкой цепочек _simplify_edge_chains).
C++-зеркало - cpp/core/stl_import.*, cpp/core/outline.* и cpp/core/packing.*;
цель CMake gvcore, пробник cpp/stl_probe.cpp.

Что сравнивается и почему так:
  * суп вершин (stl.soup, float64 [562776, 3]) - побайтово и поэлементно: критерий
    этапа - те же массивы, а не похожие числа. Порядок вершин в файле здесь и есть
    результат, поэтому сравниваются байты, а не набор точек;
  * контур (stl.feature_edges, float32 [1435, 2, 3]) - тоже побайтово: он идёт
    прямо в буфер обводки. Его порядок зависит от нумерации сваренных вершин,
    устойчивости сортировки рёбер по ключу и порядка цепочек, поэтому совпадение
    байтов проверяет весь путь, а не только геометрию;
  * габарит супа (_verts_bounds) - сверяется с питоновским, в оракул он не входит;
  * sha256 каждого массива - с двух сторон: у C++ (печать пробника) и у оракула
    (oracle_manifest.json). Совпадение с оракулом доказывает, что не сдвинулся и
    питоновский эталон, и перенос;
  * малые STL (ASCII, бинарный и пустой файл) - отдельные прогоны: ветки чтения,
    которых нет в Test_model.STL (он бинарный), иначе они остались бы непроверенными.

Числа на Test_model.STL: 562776 вершин, 187592 треугольника, 1435 отрезков обводки.

Без тулчейна или собранного пробника проверка пропускается с кодом 0: сборку
стережёт tools/check.ps1.

Запуск: python modules/gcode-viewer/test_stl_core.py (exit 0 = пойдено)
"""
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

STL = os.path.join(ROOT, "data", "cad-testdata", "Test_model.STL")
PROBE = os.path.join(ROOT, "build", "edges", "stl_probe.exe")
MANIFEST = os.path.join(HERE, "oracle_manifest.json")

ANGLE_DEG = 20.0            # порог двугранного угла обводки, градусы
CRLF = "\r\n"               # ASCII STL: возврат каретки и перевод строки

# Ключ пробника -> ключ оракула -> файл дампа -> dtype (как у numpy.tobytes()).
KEYS = (
    ("soup", "stl.soup", "stl_soup.f64", "<f8"),
    ("feature_edges", "stl.feature_edges", "stl_feature_edges.f32", "<f4"),
)


def _run_probe(stl_path, dump_dir):
    """Запуск пробника -> словарь его строк вывода.

    Пробник печатает путь в UTF-8, поэтому вывод разбирается как UTF-8: у
    subprocess умолчание - кодировка консоли (cp866), и кириллица доехала бы
    искажённой. DLL пробнику не нужны: Qt и OCCT в цели gvcore нет.
    """
    res = subprocess.run([PROBE, stl_path, dump_dir], capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    if res.returncode != 0:
        raise RuntimeError("пробник вернул %d на %s\n%s%s"
                           % (res.returncode, stl_path, res.stdout, res.stderr))
    out = {}
    for line in res.stdout.splitlines():
        if ":" in line:
            key, _, value = line.partition(":")
            out[key.strip()] = value.strip()
    return out


def _digest(arr, np):
    return hashlib.sha256(np.ascontiguousarray(arr).tobytes()).hexdigest()


def _reference(np, stl_path):
    """Питоновский эталон: суп, контур, габарит и их sha256."""
    from mock_backend import (load_stl_vertices, _verts_bounds,       # noqa: E402
                              _feature_edges_np, packed_positions)

    soup = np.asarray(load_stl_vertices(stl_path), dtype=np.float64).reshape(-1, 3)
    packed = _feature_edges_np(soup, ANGLE_DEG)
    segs = packed_positions(packed)
    edges = np.asarray(segs if segs is not None else np.empty((0, 2, 3)), dtype=np.float32)
    box = _verts_bounds(soup)
    ref = {"soup": soup, "feature_edges": edges}
    return ref, {key: _digest(arr, np) for key, arr in ref.items()}, box


def _compare_dump(path, dtype, want, np):
    """Дамп пробника против эталонного массива: байты и поэлементно."""
    got = np.fromfile(path, dtype=dtype)
    want_flat = np.ascontiguousarray(want).reshape(-1)
    digest = _digest(got, np)
    bad = []
    if got.size != want_flat.size:
        bad.append("элементов %d против эталонных %d" % (got.size, want_flat.size))
        return bad, digest
    if digest != _digest(want_flat, np):
        view = np.int64 if dtype == "<f8" else np.int32
        count = int((got.view(view) != want_flat.view(view)).sum())
        delta = np.abs(got.astype(np.float64) - want_flat.astype(np.float64))
        bad.append("расходится элементов %d из %d, максимум |dx| = %.17g"
                   % (count, got.size, float(delta.max()) if count else 0.0))
    return bad, digest


def _compare_case(np, stl_path, dump_dir, out, manifest, verbose):
    """Один прогон пробника: суп, контур и габарит против питоновского эталона.

    `out` - разобранный вывод пробника (None: запустить), `manifest` - оракул
    (None: сверять не с чем - ключи оракула есть только у Test_model.STL).
    """
    ref, ref_hash, box = _reference(np, stl_path)
    if out is None:
        out = _run_probe(stl_path, dump_dir)
    bad = []

    if int(out.get("verts", -1)) != ref["soup"].shape[0]:
        bad.append("вершин C++ %s против эталонных %d"
                   % (out.get("verts", "?"), ref["soup"].shape[0]))
    if int(out.get("triangles", -1)) != ref["soup"].shape[0] // 3:
        bad.append("треугольников C++ %s против эталонных %d"
                   % (out.get("triangles", "?"), ref["soup"].shape[0] // 3))

    for key, oracle_key, dump_name, dtype in KEYS:
        arr = ref[key]
        problems, digest = _compare_dump(os.path.join(dump_dir, dump_name), dtype, arr, np)
        probe_hash = out.get("sha256_" + key, "?")
        if probe_hash != digest:
            problems.append("sha256 пробника %s против дампа %s" % (probe_hash[:16], digest[:16]))
        if digest != ref_hash[key]:
            problems.append("sha256 C++ %s против питоновского эталона %s"
                            % (digest[:16], ref_hash[key][:16]))
        if manifest is not None:
            saved = manifest.get(oracle_key, {})
            if not saved:
                problems.append("в оракуле нет %s" % oracle_key)
            else:
                if arr.size != int(np.prod(saved["shape"])):
                    problems.append("элементов %d против оракула %s"
                                    % (arr.size, saved["shape"]))
                if digest != saved.get("sha256", ""):
                    problems.append("sha256 C++ %s против оракула %s"
                                    % (digest[:16], saved.get("sha256", "")[:16]))
        if verbose:
            print("%-14s эталон %-14s %-8s sha256 %s  C++ %s  %s"
                  % (key, str(list(arr.shape)), str(arr.dtype), ref_hash[key][:16],
                     digest[:16], "совпало" if not problems else "РАСХОЖДЕНИЕ"))
        bad += ["%s: %s" % (key, p) for p in problems]

    got_lo = [float(v) for v in out.get("bounds_lo", "").split()]
    got_hi = [float(v) for v in out.get("bounds_hi", "").split()]
    if got_lo != list(box.lo) or got_hi != list(box.hi):
        bad.append("габарит C++ %s..%s против эталонного %s..%s"
                   % (got_lo, got_hi, list(box.lo), list(box.hi)))
    elif verbose:
        print("габарит супа: %s .. %s - совпал" % (got_lo, got_hi))
    return bad


def _ascii_stl(tris):
    """Текст ASCII STL по треугольникам (по три точки на грань)."""
    lines = ["solid probe", CRLF]
    for tri in tris:
        lines.append("  facet normal 0.0 0.0 0.0" + CRLF)
        lines.append("    outer loop" + CRLF)
        for p in tri:
            lines.append("      vertex %.1f %.1f %.1f%s" % (p[0], p[1], p[2], CRLF))
        lines.append("    endloop" + CRLF)
        lines.append("  endfacet" + CRLF)
    lines.append("endsolid probe" + CRLF)
    return "".join(lines)


def _binary_stl(tris):
    """Бинарный STL по треугольникам (запись 50 байт: нормаль, три вершины, атрибут)."""
    out = bytearray(b"binary probe".ljust(80, b"\0"))
    out += struct.pack("<I", len(tris))
    for tri in tris:
        out += struct.pack("<3f", 0.0, 0.0, 0.0)         # нормаль записи не читается
        for p in tri:
            out += struct.pack("<3f", p[0], p[1], p[2])
        out += struct.pack("<H", 0)
    return bytes(out)


def _cube_tris():
    """Куб из 12 треугольников: замкнутая оболочка с прямыми двугранными углами."""
    v = [(x, y, z) for x in (0.0, 2.0) for y in (0.0, 3.0) for z in (0.0, 4.0)]
    quads = ((0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1),
             (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3))
    tris = []
    for a, b, c, d in quads:
        tris.append([v[a], v[b], v[c]])
        tris.append([v[a], v[c], v[d]])
    return tris


def _tetra_tris():
    """Тетраэдр из 4 треугольников: короткий замкнутый меш для бинарной ветки."""
    v = [(0.0, 0.0, 0.0), (4.0, 0.0, 0.0), (0.0, 4.0, 0.0), (0.0, 0.0, 4.0)]
    return [[v[0], v[1], v[2]], [v[0], v[2], v[3]],
            [v[0], v[3], v[1]], [v[1], v[3], v[2]]]


def _check_small(np, dump_root):
    """Малые STL: ветки ASCII, бинарного чтения и пустого файла."""
    bad = []
    cases = (
        ("ascii", _ascii_stl(_cube_tris()).encode("utf-8")),
        ("binary", _binary_stl(_tetra_tris())),
        ("empty", b""),
    )
    for name, data in cases:
        case_dir = os.path.join(dump_root, name)
        os.makedirs(case_dir, exist_ok=True)
        stl_path = os.path.join(dump_root, name + ".stl")
        with open(stl_path, "wb") as fh:
            fh.write(data)
        out = _run_probe(stl_path, case_dir)
        problems = _compare_case(np, stl_path, case_dir, out, None, verbose=False)
        print("малый STL %-7s: вершин %s, треугольников %s, отрезков %s - %s"
              % (name, out.get("verts", "?"), out.get("triangles", "?"),
                 out.get("segments", "?"),
                 "совпало" if not problems else "РАСХОЖДЕНИЕ"))
        bad += ["малый STL (%s): %s" % (name, p) for p in problems]
    return bad


def main() -> int:
    if not os.path.exists(STL):
        print("пропущено: нет %s" % STL)
        return 0
    if not os.path.exists(PROBE):
        print("пропущено: пробник не собран (%s), соберите scripts/build-viewer.ps1" % PROBE)
        return 0
    try:
        import numpy as np
    except ImportError as exc:
        print("пропущено: нет numpy (%s)" % exc)
        return 0

    with open(MANIFEST, "r", encoding="utf-8") as fh:
        manifest = json.load(fh)

    dump_root = tempfile.mkdtemp(prefix="gvstl_probe_")
    bad = []
    try:
        dump_dir = os.path.join(dump_root, "model")
        os.makedirs(dump_dir, exist_ok=True)
        problems = _compare_case(np, STL, dump_dir, None, manifest, verbose=True)
        bad += ["Test_model.STL: %s" % p for p in problems]
        bad += _check_small(np, dump_root)
    finally:
        shutil.rmtree(dump_root, ignore_errors=True)

    if bad:
        for line in bad:
            print("FAIL: " + line)
        return 1
    print("суп и контур совпали с эталоном побайтово; sha256 сошлись с "
          "load_stl_vertices/_feature_edges_np и с оракулом")
    print("RESULT: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
