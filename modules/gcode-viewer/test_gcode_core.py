#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сверка C++-ядра gvcore (этап 2 плана переноса) с питоновским эталоном.

Эталон — mock_backend (фасад над gcode_parser.py и path_prep.py).

Основной прогон — Test_model.ngc и оракул. Сверяется то же, что пишет
oracle_dump.py: ключи path.points (float64 [70915, 3]), path.flags (float64
[70915, 2]), path.last (float64 [3]), path.segments (int64 [1]) и path.stages
(numpy U64 [13], UTF-32LE по 64 знака). Пробник cpp/gcode_probe.cpp считает то же
ядро gvcore и печатает sha256 тех же сериализаций.

Что сравнивается и почему так:
  * массивы — поэлементное равенство (numpy.array_equal): критерий этапа 2 —
    идентичные буферы, а не похожие числа;
  * sha256 — отдельно: совпадение с oracle_manifest.json доказывает, что ни
    питоновский эталон, ни C++ не сдвинулись;
  * этапы — строки '<индекс>, <подпись>' с подписью из UI_TOOL;
  * теги UI_* (tag_*) — разбор C++ против ui_tags.py: подписи этапов опираются
    на них, и расхождение здесь видно раньше, чем в подписи;
  * подготовка траектории (_prepare_ordered) — буферы быстрых и рабочих ходов
    сравниваются байт в байт с живым питоновским результатом: в оракуле этих
    ключей нет, поэтому эталон считается на месте;
  * малые программы ниже — ветки, которых в Test_model нет: G73/G81/G82, дуги по
    R (в том числе отрицательному), G91, дуга без центра, G80, этапы из
    комментариев и по сменам инструмента, кириллица в подписи, пустая программа.

Если тулчейна или собранного пробника нет — проверка пропускается с кодом 0:
сборку стережёт tools/check.ps1.

Запуск: python modules/gcode-viewer/test_gcode_core.py (exit 0 = пойдено)
"""
import hashlib
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

NGC = os.path.join(ROOT, "data", "cad-testdata", "Test_model.ngc")
PROBE = os.path.join(ROOT, "build", "edges", "gcode_probe.exe")
MANIFEST = os.path.join(HERE, "oracle_manifest.json")

# Ключ дампа пробника -> ключ оракула -> файл дампа.
KEYS = (
    ("points", "path.points", "core_points.f64"),
    ("flags", "path.flags", "core_flags.f64"),
    ("last", "path.last", "core_last.f64"),
    ("segments", "path.segments", "core_segments.i64"),
    ("stages", "path.stages", "core_stages.u64"),
)

# Малые программы: каждая ветка ядра, которой нет в Test_model.ngc. Текст один и
# тот же для обеих сторон - файл пишется один раз и читается обоими.
SYNTHETIC = {
    "циклы, дуги всех плоскостей, G91": """%
(UI_OP: 1)
(UI_TOOL: T=1 TYPE=FLAT D=6. CR=0. NF=2)
N10 G90 G94 G17 G91.1
N20 G21
N30 G0 X0. Y0. Z5.
N40 G1 X10. F300.
N50 G1 X20. Y0. Z5. F0
N60 G18 G2 X0. Z0. I-10. K0.
N70 G19 G3 Y10. Z0. J0. K10.
N80 G17 G2 X0. Y0. R10.
N90 G2 X5. Y5. R-10.
N100 G3 X0. Y0.
N110 G91
N120 G1 X1.5 Y-2.
N130 G90
N140 G0 Z10.
(UI_OP: 2)
N150 G98 G81 X5. Y5. Z-5. R2. F100.
N160 G82 X15. Y5. Z-5. R2.
N170 G73 X25. Y5. Z-8. R2. Q2. F80.
N180 G83 X35. Y5. Z-12. R2. Q3. F60.
N190 G99 G83 X45. Y5. Z-12. R2. Q3.
N200 G80
M30
""",
    "этапы из комментариев операций, кириллица": """%
(Заготовка)
N10 G90 G17
N20 G0 X0. Y0. Z5.
(Черновая обработка)
N30 T1 M6
N40 G1 X10. Y0. F200.
(Чистовая обработка)
N50 T02 M06
N60 G1 X20. Y0. F150.
M30
""",
    "этапы только по сменам инструмента": """%
N10 G17 G90
N20 T1 M6
N30 G0 X0. Y0. Z5.
N40 G1 X5. Y0. F100.
N50 T2 M06
N60 G1 X10. Y0. F120.
""",
    "пустая программа": "",
}


def _run_probe(ngc_path, dump_dir):
    """Запуск пробника -> словарь его строк вывода.

    Пробник печатает подписи этапов в UTF-8 (по-русски), поэтому вывод
    разбирается как UTF-8: у subprocess умолчание - кодировка консоли (cp866),
    и подпись доехала бы искажённой.
    """
    res = subprocess.run([PROBE, ngc_path, dump_dir], capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    if res.returncode != 0:
        raise RuntimeError("пробник вернул %d на %s\n%s%s"
                           % (res.returncode, ngc_path, res.stdout, res.stderr))
    out = {}
    for line in res.stdout.splitlines():
        if ":" in line:
            key, _, value = line.partition(":")
            out[key.strip()] = value.strip()
    return out


def _digest(arr, np):
    return hashlib.sha256(np.ascontiguousarray(arr).tobytes()).hexdigest()


def _plug_arrays(dump_dir, np):
    """Массивы C++ из дампа пробника (те же форматы, что у numpy)."""
    def read(name):
        return os.path.join(dump_dir, name)
    return {
        "points": np.fromfile(read("core_points.f64"), dtype="<f8").reshape(-1, 3),
        "flags": np.fromfile(read("core_flags.f64"), dtype="<f8").reshape(-1, 2),
        "last": np.fromfile(read("core_last.f64"), dtype="<f8"),
        "segments": np.fromfile(read("core_segments.i64"), dtype="<i8"),
        "stages": np.fromfile(read("core_stages.u64"), dtype="<U64"),
    }


def _parse_ref(mb, path):
    """Питоновский эталон разбора этой программы."""
    with open(path, "r", encoding="utf-8", errors="ignore") as fh:
        text = fh.read()
    return text, mb.parse_gcode(text)


def _compare_parse(mb, np, path, dump_dir, verbose):
    """Сверка parse_gcode: массивы, этапы и число сегментов одного файла."""
    text, (ordered, last, seg_count, stages) = _parse_ref(mb, path)
    ref = {
        "points": np.asarray([p.point for p in ordered], dtype=np.float64).reshape(-1, 3),
        "flags": np.asarray([[1.0 if p.is_rapid else 0.0, float(p.speed_mm_s)]
                             for p in ordered], dtype=np.float64).reshape(-1, 2),
        "last": np.asarray(last, dtype=np.float64),
        "segments": np.asarray([seg_count], dtype=np.int64),
        "stages": np.asarray([str((s.index, s.label)) for s in stages], dtype="U64"),
    }
    out = _run_probe(path, dump_dir)
    plug = _plug_arrays(dump_dir, np)

    bad = []
    for name, key, _ in KEYS:
        a, b = ref[name], plug[name]
        ref_hash = _digest(a, np)
        plug_hash = _digest(b, np)
        same = a.shape == b.shape and bool(np.array_equal(a, b))
        if verbose:
            print("%-8s %-16s эталон %s C++ %s %s"
                  % (name, str(b.shape), ref_hash[:16], plug_hash[:16],
                     "совпало" if same and ref_hash == plug_hash else "РАСХОЖДЕНИЕ"))
        if not same:
            bad.append("%s: массивы не совпали (%s против %s)" % (key, a.shape, b.shape))
        elif ref_hash != plug_hash:
            bad.append("%s: sha256 C++ %s против эталона %s" % (key, plug_hash[:16], ref_hash[:16]))
    if int(out.get("seg_count", "-1")) != seg_count:
        bad.append("seg_count C++ %s против эталона %d" % (out.get("seg_count"), seg_count))
    if int(out.get("points", "-1")) != len(ordered):
        bad.append("число точек C++ %s против эталона %d" % (out.get("points"), len(ordered)))
    if [str(v) for v in plug["stages"]] != [str(v) for v in ref["stages"]]:
        for i in range(min(len(ref["stages"]), len(plug["stages"]))):
            if str(ref["stages"][i]) != str(plug["stages"][i]):
                print("этап %d: эталон %r C++ %r" % (i, str(ref["stages"][i]), str(plug["stages"][i])))
        bad.append("подписи этапов не совпали")
    return bad, out, ref, plug, text, ordered, stages


def _check_manifest(ref, np):
    """Питоновский эталон на Test_model обязан совпадать с записанным оракулом."""
    with open(MANIFEST, "r", encoding="utf-8") as fh:
        manifest = json.load(fh)
    bad = []
    for name, key, _ in KEYS:
        want = manifest.get(key, {}).get("sha256")
        got = _digest(ref[name], np)
        if want != got:
            bad.append("%s: оракул %s, эталон сейчас %s" % (key, str(want)[:16], got[:16]))
    return bad


def _check_tags(text, out):
    """Разбор тегов UI_* в C++ против ui_tags.py на том же файле."""
    import ui_tags

    bad = []
    floats = lambda key: [float(v) for v in out.get(key, "none").split()]

    line = _first_line(text, "UI_ROTATION")
    ref = ui_tags.parse_ui_rotation(line or "")
    if floats("tag_rotation") != list(ref):
        bad.append("UI_ROTATION: C++ %s против %s" % (out.get("tag_rotation"), ref))

    line = _first_line(text, "UI_ORIGIN")
    ref = ui_tags.parse_ui_origin(line or "")
    if floats("tag_origin") != list(ref):
        bad.append("UI_ORIGIN: C++ %s против %s" % (out.get("tag_origin"), ref))

    line = _first_line(text, "UI_STOCK")
    ref = ui_tags.parse_ui_stock(line or "")
    plug = None if out.get("tag_stock") == "none" else tuple(floats("tag_stock"))
    if (plug is None) != (ref is None) or (ref is not None and plug != tuple(ref)):
        bad.append("UI_STOCK: C++ %s против %s" % (out.get("tag_stock"), ref))

    line = _first_line(text, "UI_CAD_MATRIX")
    ref = ui_tags.parse_ui_cad_matrix(line or "")
    plug = None if out.get("tag_cad_matrix") == "none" else list(floats("tag_cad_matrix"))
    if (plug is None) != (ref is None) or (ref is not None and plug != ref):
        bad.append("UI_CAD_MATRIX: C++ %s против %s" % (out.get("tag_cad_matrix"), ref))

    line = _first_line(text, "UI_CAD_MATRIX_ORIGIN")
    ref = ui_tags.parse_ui_cad_matrix_origin(line or "")
    plug = None if out.get("tag_cad_matrix_origin") == "none" else tuple(floats("tag_cad_matrix_origin"))
    if (plug is None) != (ref is None) or (ref is not None and plug != tuple(ref)):
        bad.append("UI_CAD_MATRIX_ORIGIN: C++ %s против %s" % (out.get("tag_cad_matrix_origin"), ref))

    line = _first_line(text, "UI_HOLDER")
    printed = out.get("tag_holder", "none")
    if line is None:
        if printed != "none":
            bad.append("UI_HOLDER: тега нет, а C++ напечатал %s" % printed)
    else:
        ref = ui_tags.parse_ui_holder(line)
        want = "none" if not ref else "gauge=%s name=%s" % (
            "%.17g" % ref["gauge"] if "gauge" in ref else "none",
            "'%s'" % ref["name"] if "name" in ref else "none")
        if printed != want:
            bad.append("UI_HOLDER: C++ %s против %s" % (printed, want))
    return bad


def _first_line(text, tag):
    """Первая строка с тегом (C++ ищет по всему тексту - совпадение то же)."""
    for line in text.splitlines():
        if tag in line:
            return line
    return None


def _check_path_prep(dump_dir, out, mb, ordered, stages, np):
    """Подготовка траектории в C++ против живого path_prep._prepare_ordered.

    Оракул ключей path_prep не содержит (этап 0 их не писал), поэтому эталон
    считается здесь же: буферы сверяются байт в байт, метрики - точно.
    """
    pd = mb._prepare_ordered(ordered, stages)
    bad = []

    def read(name):
        return os.path.join(dump_dir, name)

    scene = np.fromfile(read("core_scene.f64"), dtype="<f8").reshape(-1, 3)
    ref_scene = np.asarray(pd.pts, dtype=np.float64).reshape(-1, 3)
    if scene.shape != ref_scene.shape or not np.array_equal(scene, ref_scene):
        bad.append("pts (полилиния сцены): %s против %s" % (scene.shape, ref_scene.shape))

    cum = np.fromfile(read("core_cum.f64"), dtype="<f8")
    ref_cum = np.asarray(pd.cum, dtype=np.float64)
    if cum.shape != ref_cum.shape or not np.array_equal(cum, ref_cum):
        bad.append("cum (длины по пути): %s против %s" % (cum.shape, ref_cum.shape))

    rapid = np.fromfile(read("core_rapid.u8"), dtype="u1").astype(bool)
    ref_rapid = np.asarray(pd.rapid, dtype=bool)
    if rapid.shape != ref_rapid.shape or not np.array_equal(rapid, ref_rapid):
        bad.append("rapid (флаги): %s против %s" % (rapid.shape, ref_rapid.shape))

    speeds = np.fromfile(read("core_speeds.f64"), dtype="<f8")
    ref_speeds = np.asarray(pd.speeds, dtype=np.float64)
    if speeds.shape != ref_speeds.shape or not np.array_equal(speeds, ref_speeds):
        bad.append("speeds (скорости): %s против %s" % (speeds.shape, ref_speeds.shape))

    with open(read("core_rapid_stream.bin"), "rb") as fh:
        rapid_bytes = fh.read()
    with open(read("core_feed_stream.bin"), "rb") as fh:
        feed_bytes = fh.read()
    if rapid_bytes != pd.rapid_bytes:
        bad.append("буфер быстрых ходов: %d байт против %d"
                   % (len(rapid_bytes), len(pd.rapid_bytes)))
    if feed_bytes != pd.feed_bytes:
        bad.append("буфер рабочих ходов: %d байт против %d"
                   % (len(feed_bytes), len(pd.feed_bytes)))

    for key, ref in (("pd_total", pd.total), ("pd_radius", pd.radius), ("pd_min_y", pd.min_y)):
        if float(out.get(key, "nan")) != ref:
            bad.append("%s: C++ %s против %s" % (key, out.get(key), ref))
    if [float(v) for v in out.get("pd_center", "").split()] != [float(v) for v in pd.center]:
        bad.append("pd_center: C++ %s против %s" % (out.get("pd_center"), pd.center))
    if ([float(v) for v in out.get("pd_bounds_lo", "").split()] != [float(v) for v in pd.bounds.lo]
            or [float(v) for v in out.get("pd_bounds_hi", "").split()] != [float(v) for v in pd.bounds.hi]):
        bad.append("габарит сцены: C++ %s:%s против %s:%s"
                   % (out.get("pd_bounds_lo"), out.get("pd_bounds_hi"),
                      pd.bounds.lo, pd.bounds.hi))

    if int(out.get("pd_rapid_nv", "-1")) != pd.rapid_nv:
        bad.append("число вершин быстрых ходов: C++ %s против %d"
                   % (out.get("pd_rapid_nv"), pd.rapid_nv))
    if int(out.get("pd_feed_nv", "-1")) != pd.feed_nv:
        bad.append("число вершин рабочих ходов: C++ %s против %d"
                   % (out.get("pd_feed_nv"), pd.feed_nv))

    printed = [k for k in out if k.startswith("pd_stage_")]
    if len(printed) != len(pd.stages):
        bad.append("этапов-диапазонов %d против %d" % (len(printed), len(pd.stages)))
    else:
        for i, st in enumerate(pd.stages):
            start, end, label = out["pd_stage_%d" % i].split(" ", 2)
            if float(start) != st["start"] or float(end) != st["end"] or label != st["label"]:
                bad.append("этап %d: C++ (%s, %s) %s против (%s, %s) %s"
                           % (i, start, end, label, st["start"], st["end"], st["label"]))

    print("path_prep: буферы быстрых %d байт (%d вершин), рабочих %d байт (%d вершин)"
          % (len(rapid_bytes), pd.rapid_nv, len(feed_bytes), pd.feed_nv))
    print("path_prep: сцена %s, длина пути %.17g, центр %s, радиус %.17g, min_y %.17g"
          % (scene.shape, pd.total, pd.center, pd.radius, pd.min_y))
    return bad


def main() -> int:
    if not os.path.exists(NGC):
        print("пропущено: нет %s" % NGC)
        return 0
    if not os.path.exists(PROBE):
        print("пропущено: пробник не собран (build/edges/gcode_probe.exe), "
              "соберите scripts/build-viewer.ps1")
        return 0
    try:
        import numpy as np
        import mock_backend as mb
    except ImportError as exc:
        print("пропущено: нет numpy/PySide6 (%s)" % exc)
        return 0

    dump_dir = os.path.join(tempfile.gettempdir(), "gvcore_core_dump")
    os.makedirs(dump_dir, exist_ok=True)

    bad, out, ref, plug, text, ordered, stages = _compare_parse(mb, np, NGC, dump_dir, verbose=True)
    print("Test_model.ngc: точек %d, этапов %d" % (len(ordered), len(stages)))
    bad += _check_manifest(ref, np)
    bad += _check_tags(text, out)
    bad += _check_path_prep(dump_dir, out, mb, ordered, stages, np)

    # Малые программы: ветки, которых Test_model не задевает. Пути к файлам
    # временные - в репозиторий ничего не пишется.
    for title, program in sorted(SYNTHETIC.items()):
        path = os.path.join(dump_dir, "synthetic.ngc")
        with open(path, "w", encoding="utf-8", newline="\n") as fh:
            fh.write(program)
        problems, s_out, _, s_plug, _, _, _ = _compare_parse(mb, np, path, dump_dir, verbose=False)
        print("малая программа, %s: точек C++ %s, этапов C++ %s -> %s"
              % (title, s_out.get("points"), s_out.get("stages"),
                 "совпало" if not problems else "РАСХОЖДЕНИЕ"))
        bad += ["%s: %s" % (title, p) for p in problems]

    if bad:
        for line in bad:
            print("FAIL: " + line)
        return 1
    print("теги UI_*: разбор C++ совпал с ui_tags.py")
    print("RESULT: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
