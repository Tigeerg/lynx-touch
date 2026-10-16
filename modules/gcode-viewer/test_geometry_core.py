#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Сверка C++-геометрий Qt Quick 3D (этап 3 плана переноса) с питоновским эталоном.

Эталон - modules/gcode-viewer/geometry.py: шесть классов QQuick3DGeometry, чьи
форматы буферов заморожены (контракт QML и шейдеров). C++-зеркало лежит в
modules/gcode-viewer/cpp/qt/gv_geometry.*, его пробник cpp/geometry_probe.cpp
строит те же геометрии на тех же числах и пишет буферы дампом.

Что сравнивается и почему так:
  * буфер вершин - побайтово (`vertexData()` против дампа пробника) плюс sha256:
    критерий этапа 3 - идентичные буферы, а не похожие числа;
  * stride, тип примитива, число вершин, габарит побитно (float32), атрибуты
    (semantic, смещение, тип компонента) - равенство: это и есть контракт
    шейдеров toolpath.* (stride 20) и stepledges.* (stride 48);
  * входные данные пишет этот же тест в каталог и отдаёт пробнику: обе стороны
    считают на одних числах, а не на «похожих». Траектория - рабочие ходы
    Test_model.ngc (тот же путь, что у контроллера), контур - звенья с нулевым
    отрезком, меш - куб с вырожденным треугольником, инструмент - профиль
    концевого фреза с радиусом скругления (ветки веера и четырёхугольников);
  * пустые геометрии (нет данных) проверяются отдельно: у них другой набор
    атрибутов и нулевой габарит.

Нужен Qt-движок: геометрии - объекты Qt Quick 3D, поэтому приложение создаётся
(QGuiApplication, QT_QPA_PLATFORM=offscreen), хотя окно не открывается.

Без собранного пробника проверка пропускается с кодом 0: сборку стережёт
tools/check.ps1.

Запуск: python modules/gcode-viewer/test_geometry_core.py (exit 0 = пойдено)
"""
import hashlib
import os
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

PROBE = os.path.join(ROOT, "build", "edges", "geometry_probe.exe")
NGC = os.path.join(ROOT, "data", "cad-testdata", "Test_model.ngc")

# Замороженный контракт: шаг вершины у шейдеров. Расхождение здесь - сломанный
# рендер, а не расхождение замеров.
TOOLPATH_STRIDE = 20
HLR_STRIDE = 48

# Поля, которые пробник печатает по каждому случаю, кроме атрибутов.
FIELDS = ("vertexCount", "triangleCount", "stride", "primitiveType", "attributeCount",
          "boundsMin", "boundsMax", "bytes", "sha256")

CASES = ("toolpath", "toolpath_nobounds", "toolpath_empty", "hlr", "hlr_noref", "hlr_empty",
         "grid", "grid_flat", "axis", "axis_short", "mesh", "mesh_empty", "tool", "tool_low")


def _probe_env():
    """PATH для пробника: DLL Qt в каталоге сборки не лежат."""
    qt = os.environ.get("CNC_QT_DIR") or r"C:\Qt\6.12.0\msvc2022_64"
    return dict(os.environ, QT_QPA_PLATFORM="offscreen",
                PATH=os.pathsep.join([os.path.join(ROOT, "build", "edges"),
                                      os.path.join(qt, "bin"), os.environ.get("PATH", "")]))


def _run_probe(in_dir, dump_dir):
    """Запуск пробника -> словарь его строк вывода («случай.поле» -> значение)."""
    res = subprocess.run([PROBE, in_dir, dump_dir], capture_output=True, text=True,
                         encoding="utf-8", errors="replace", env=_probe_env())
    if res.returncode != 0:
        raise RuntimeError("пробник вернул %d\n%s%s" % (res.returncode, res.stdout, res.stderr))
    out = {}
    for line in res.stdout.splitlines():
        if ":" in line:
            key, _, value = line.partition(":")
            out[key.strip()] = value.strip()
    return out


def _write_blob(path, data):
    with open(path, "wb") as fh:
        fh.write(data)


def _write_f64(path, values, np):
    _write_blob(path, np.asarray(values, dtype="<f8").tobytes())


def _bits(value):
    """Биты float32: габарит сравнивается побитно, а не «примерно равен»."""
    return struct.unpack("<I", struct.pack("<f", value))[0]


def _mesh_verts(np):
    """Куб из 12 треугольников плюс вырожденный: ветка ln == 0 в нормалях.

    Координаты нарочно не целые (12.7 и т. п.): на целых нормали совпали бы и при
    ошибке в порядке округления.
    """
    c = [(0.0, 0.0, 0.0), (12.7, 0.0, 0.0), (12.7, 12.7, 0.0), (0.0, 12.7, 0.0),
         (0.0, 0.0, 6.3), (12.7, 0.0, 6.3), (12.7, 12.7, 6.3), (0.0, 12.7, 6.3)]
    faces = [(0, 1, 2), (0, 2, 3), (4, 6, 5), (4, 7, 6), (0, 4, 5), (0, 5, 1),
             (1, 5, 6), (1, 6, 2), (2, 6, 7), (2, 7, 3), (3, 7, 4), (3, 4, 0)]
    verts = [c[i] for face in faces for i in face]
    verts.extend([(1.1, 2.2, 3.3)] * 3)
    return np.asarray(verts, dtype="<f8")


def _hlr_segments(np):
    """Звенья контура: обычные, нулевой (ветка ln == 0) и вдоль оси Y."""
    segs = [((i * 3.0, 0.0, i * 1.5), (i * 3.0 + 10.0, 2.5 * i, i * 1.5)) for i in range(7)]
    segs.append(((5.0, 5.0, 5.0), (5.0, 5.0, 5.0)))
    segs.append(((0.0, 0.0, 0.0), (0.0, 10.0, 0.0)))
    return np.asarray(segs, dtype=np.float32)


def _write_inputs(in_dir, np, mb):
    """Входные данные для обеих сторон: тот же файл читает пробник."""
    with open(NGC, "r", encoding="utf-8", errors="ignore") as fh:
        text = fh.read()
    ordered, _last, _seg_count, stages = mb.parse_gcode(text)
    pd = mb._prepare_ordered(ordered, stages)
    _write_blob(os.path.join(in_dir, "tp_raw.bin"), pd.feed_bytes)
    _write_f64(os.path.join(in_dir, "tp_bounds.f64"),
               [*pd.bounds.lo, *pd.bounds.hi], np)

    hlr = _hlr_segments(np)
    # Значения звеньев точно представимы во float32, поэтому запись их в float64
    # (и обратное приведение в C++) ничего не сдвигает.
    _write_f64(os.path.join(in_dir, "hlr_segs.f64"), hlr.reshape(-1).astype("<f8"), np)
    _write_f64(os.path.join(in_dir, "hlr_ref.f64"), [12.5, -4.25, 7.0], np)

    grid = [200.0, 10.0, -2.5, 3.25, -7.5, 0.45]
    grid_flat = [60.0, 7.0, 0.0, -1.5, 2.5, 0.0]        # растворения нет: все альфы 1
    axis = [12.0, 3.5, 2.0, 0.7]
    axis_short = [3.0, 4.0, 2.0, 0.7]                    # голова длиннее указателя
    for name, values in (("grid_params.f64", grid), ("grid_flat_params.f64", grid_flat),
                         ("axis_params.f64", axis), ("axis_short_params.f64", axis_short)):
        _write_f64(os.path.join(in_dir, name), values, np)

    verts = _mesh_verts(np)
    _write_f64(os.path.join(in_dir, "mesh_verts.f64"), verts.reshape(-1), np)

    # Профиль инструмента берётся у эталонного построителя: концевой фрез с
    # радиусом скругления (BULL) даёт обе ветки веера и четырёхугольники.
    info = {"diameter": 6.0, "type": "BULL", "corner_radius": 1.0, "flute_length": 20.0,
            "shank_diameter": 6.0, "body_length": 40.0, "overall_length": 60.0}
    prof = mb._tool_profile(info)
    _write_f64(os.path.join(in_dir, "tool_profile.f64"),
               [v for p in prof for v in (p.radius_mm, p.height_mm)], np)
    _write_f64(os.path.join(in_dir, "tool_segments.f64"), [48.0], np)

    low = [(0.0, 0.0), (3.0, 0.0), (3.0, 20.0), (0.0, 20.0)]
    _write_f64(os.path.join(in_dir, "tool_low_profile.f64"),
               [v for p in low for v in p], np)
    _write_f64(os.path.join(in_dir, "tool_low_segments.f64"), [3.0], np)

    return {"tp_raw": pd.feed_bytes, "tp_bounds": pd.bounds, "tp_nv": pd.feed_nv,
            "hlr": hlr, "hlr_ref": (12.5, -4.25, 7.0), "grid": grid,
            "grid_flat": grid_flat, "axis": axis, "axis_short": axis_short,
            "mesh": verts, "tool": prof, "low": low}


def _state(geom, vertex_count, triangle_count):
    """Состояние геометрии так, как его печатает пробник, плюс сам буфер."""
    data = bytes(geom.vertexData())
    state = {
        "vertexCount": int(vertex_count),
        "triangleCount": int(triangle_count),
        "stride": int(geom.stride()),
        "primitiveType": int(geom.primitiveType().value),
        "attributeCount": int(geom.attributeCount()),
        "boundsMin": [_bits(geom.boundsMin().x()), _bits(geom.boundsMin().y()),
                      _bits(geom.boundsMin().z())],
        "boundsMax": [_bits(geom.boundsMax().x()), _bits(geom.boundsMax().y()),
                      _bits(geom.boundsMax().z())],
        "bytes": len(data),
        "sha256": hashlib.sha256(data).hexdigest(),
        "data": data,
    }
    for i in range(state["attributeCount"]):
        attr = geom.attribute(i)
        state["attribute%d" % i] = (int(attr.semantic.value), int(attr.offset),
                                    int(attr.componentType.value))
    return state


def _build_geometries(geo, inputs):
    """Питоновские геометрии на тех же входах и в том же порядке, что у пробника."""
    from gv_types import Aabb, ProfilePoint      # локально: импорт стоит после пропуска

    out = []
    bound_box = Aabb(tuple(inputs["tp_bounds"].lo), tuple(inputs["tp_bounds"].hi))

    g = geo.ToolpathGeometry()
    g.set_packed(inputs["tp_raw"], bound_box, inputs["tp_nv"])
    out.append(("toolpath", g, g.vertexCount, 0))

    g = geo.ToolpathGeometry()
    g.set_packed(inputs["tp_raw"], None, inputs["tp_nv"])       # габарита нет
    out.append(("toolpath_nobounds", g, g.vertexCount, 0))

    g = geo.ToolpathGeometry()
    g.set_packed(b"", None, 0)
    out.append(("toolpath_empty", g, g.vertexCount, 0))

    g = geo.HLRLineGeometry()
    g.set_reference(inputs["hlr_ref"])
    g.set_segments(inputs["hlr"])
    out.append(("hlr", g, g.vertexCount, 0))

    g = geo.HLRLineGeometry()
    g.set_segments(inputs["hlr"])                                # опорной точки нет
    out.append(("hlr_noref", g, g.vertexCount, 0))

    g = geo.HLRLineGeometry()
    g.set_segments(None)
    out.append(("hlr_empty", g, g.vertexCount, 0))

    for name in ("grid", "grid_flat"):
        values = inputs[name]
        g = geo.GridGeometry()
        g.size = values[0]
        g.step = values[1]
        g.level = values[2]
        g.centerX = values[3]
        g.centerZ = values[4]
        g.feather = values[5]
        out.append((name, g, len(bytes(g.vertexData())) // int(g.stride()), 0))

    for name in ("axis", "axis_short"):
        values = inputs[name]
        g = geo.AxisArrowGeometry()
        g.length = values[0]
        g.headLength = values[1]
        g.headWidth = values[2]
        g.shaftWidth = values[3]
        out.append((name, g, len(bytes(g.vertexData())) // int(g.stride()), 0))

    g = geo.MeshGeometry()
    g.set_triangles(inputs["mesh"])
    out.append(("mesh", g, g.triangleCount * 3, g.triangleCount))

    g = geo.MeshGeometry()
    g.set_triangles(inputs["mesh"])
    g.reset()
    out.append(("mesh_empty", g, 0, g.triangleCount))

    g = geo.ToolGeometry()
    g.set_profile(inputs["tool"], 48)
    out.append(("tool", g, g.triangleCount * 3, g.triangleCount))

    g = geo.ToolGeometry()
    g.set_profile([ProfilePoint(r, y) for r, y in inputs["low"]], 3)
    out.append(("tool_low", g, g.triangleCount * 3, g.triangleCount))
    return out


def _probe_field(out, case, field):
    key = "%s.%s" % (case, field)
    if key not in out:
        raise RuntimeError("пробник не напечатал %s" % key)
    return out[key]


def _compare(case, state, data, out, dump_dir):
    """Поля пробника против состояния питоновской геометрии одного случая."""
    bad = []
    for field in FIELDS:
        text = _probe_field(out, case, field)
        if field in ("boundsMin", "boundsMax"):
            want = state[field]
            got = [int(v, 16) for v in text.split()]
            if got != want:
                bad.append("%s: %s 0x%s против 0x%s"
                           % (field, case, " 0x".join("%08x" % v for v in got),
                              " 0x".join("%08x" % v for v in want)))
        elif field == "sha256":
            if text != state[field]:
                bad.append("%s: sha256 C++ %s против эталона %s" % (case, text[:16], state[field][:16]))
        elif int(text) != state[field]:
            bad.append("%s: %s C++ %s против эталона %s" % (case, field, text, state[field]))

    for i in range(state["attributeCount"]):
        got = tuple(int(v) for v in _probe_field(out, case, "attribute%d" % i).split())
        if got != state["attribute%d" % i]:
            bad.append("%s: атрибут %d C++ %s против эталона %s"
                       % (case, i, got, state["attribute%d" % i]))
    extra = int(_probe_field(out, case, "attributeCount"))
    if ("%s.attribute%d" % (case, extra)) in out:
        bad.append("%s: пробник напечатал лишний атрибут %d" % (case, extra))

    with open(os.path.join(dump_dir, case + ".bin"), "rb") as fh:
        dumped = fh.read()
    if dumped != data:
        bad.append("%s: буфер C++ %d байт против эталона %d байт, байты расходятся"
                   % (case, len(dumped), len(data)))
    return bad


def main() -> int:
    if not os.path.exists(NGC):
        print("пропущено: нет %s" % NGC)
        return 0
    if not os.path.exists(PROBE):
        print("пропущено: пробник не собран (%s), соберите scripts/build-viewer.ps1" % PROBE)
        return 0
    try:
        import numpy as np
        from PySide6.QtGui import QGuiApplication
        import mock_backend as mb
        import geometry as geo
    except ImportError as exc:
        print("пропущено: нет numpy/PySide6 (%s)" % exc)
        return 0

    app = QGuiApplication(sys.argv)                    # noqa: F841 - движок нужен до геометрий
    root = os.path.join(tempfile.gettempdir(), "gvgeometry_dump")
    in_dir = os.path.join(root, "in")
    dump_dir = os.path.join(root, "out")
    os.makedirs(in_dir, exist_ok=True)
    os.makedirs(dump_dir, exist_ok=True)

    inputs = _write_inputs(in_dir, np, mb)
    out = _run_probe(in_dir, dump_dir)
    cases = _build_geometries(geo, inputs)

    if [name for name, _, _, _ in cases] != list(CASES):
        print("FAIL: набор случаев разошёлся с ожидаемым")
        return 1

    bad = []
    for name, geom, vertex_count, triangle_count in cases:
        state = _state(geom, vertex_count, triangle_count)
        problems = _compare(name, state, state["data"], out, dump_dir)
        print("%-18s вершин %7d stride %2d prim %d  атрибутов %d  буфер %8d байт  %s"
              % (name, state["vertexCount"], state["stride"], state["primitiveType"],
                 state["attributeCount"], state["bytes"],
                 "совпало" if not problems else "РАСХОЖДЕНИЕ"))
        bad += ["%s: %s" % (name, p) for p in problems]

    # Контракт шейдеров: у траектории stride 20, у ленты контура - 48.
    for case, stride in (("toolpath", TOOLPATH_STRIDE), ("hlr", HLR_STRIDE)):
        got = int(_probe_field(out, case, "stride"))
        if got != stride:
            bad.append("%s: stride %d вместо контрактного %d" % (case, got, stride))
        if int(out.get(case + ".stride", -1)) < 0:
            bad.append("%s: stride не измерен" % case)

    if bad:
        for line in bad:
            print("FAIL: " + line)
        return 1
    print("буферы, stride, атрибуты, тип примитива и габарит совпали побайтово "
          "на %d случаях" % len(cases))
    print("RESULT: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
