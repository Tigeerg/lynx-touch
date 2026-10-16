#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""C++-код, собранный против отдельной установки Qt, внутри процесса PySide6.

Эксперимент этапа 5.3 плана переноса (PORT-CPP.md): процесс PySide6 несёт свою Qt
6.12.0 в site-packages/PySide6, а C++-слои вьювера собираются сборкой Ninja против
C:\\Qt\\6.12.0\\msvc2022_64. От вопроса «уживаются ли эти две записи Qt в одном
процессе» зависит перенос геометрий Qt и контроллера: объекты должны жить там же,
где QML.

Что проверяется и чем доказано:
  * загруженная Qt6Core.dll берётся из site-packages/PySide6 (kernel32
    GetModuleHandleW/GetModuleFileNameW), каталог C:\\Qt в PATH не добавляется;
  * обычная SHARED-библиотека build/cpp/gvqtshim.dll (цель gvqtshim) грузится
    ctypes и её импорт Qt6Core.dll разрешается на уже загруженную Qt PySide6:
    версия Qt при сборке и в процессе совпадают, путь Qt6Core.dll изнутри C++
    тот же;
  * объект QQuick3DGeometry из этой библиотеки, обёрнутый
    shiboken6.wrapInstance, живой: setStride/setVertexData/addAttribute работают,
    буфер читается обратно, владение остаётся у C++ (gvq_alive после del + gc);
  * настоящая gvgeom::ToolpathGeometry (этап 3) на рабочих ходах Test_model.ngc,
    разобранных C++-ядром (gvbridge.gv_prepare_path), даёт буфер вершин, равный
    питоновской геометрии geometry.py байт в байт (stride 20);
  * буферы всех шести C++-геометрий этапа 3 (траектория, лента контура, сетка,
    указатель оси, меш, инструмент) совпадают с питоновским эталоном geometry.py
    побайтово - на том же наборе случаев, что у test_geometry_core.py (14 случаев,
    включая пустые геометрии), и сверяется не только буфер, но и stride, тип
    примитива, атрибуты (semantic, смещение, тип компонента) и габарит. Те же
    числа проходит и оболочка gv_qtgeom.py, которой пользуется живой вьювер при
    CNC_CPP=qtgeom;
  * собранный QML-модуль CNC.Edges инстанцируется в том же процессе: StepEdges
    строит контур Test_model.STEP (2426 отрезков, до склейки 4884) и не даёт ни
    одной строки warning движка;
  * каталог модуля CNC/Edges и DLL OCCT попадают в PATH тем же helper'ом
    (gv_qtgeom.prepare_dll_path), которым пользуются точки входа вьювера.

Найденное условие для QML-плагина (Qt-плагин MODULE, в отличие от обычной
SHARED-библиотеки): загрузчик Qt разрешает зависимости точки входа
cncedgespluginplugin.dll через PATH, а не через os.add_dll_directory. Поэтому
каталог модуля (резервная библиотека cncedgesplugin.dll) и каталог DLL OCCT
кладутся в PATH до создания движка. Без них ошибка одна и та же:
"Cannot load library ...cncedgespluginplugin.dll: не найден указанный модуль".
Разбор - cpp/exp/exp_qml_diag.py.

Без собранных gvqtshim.dll и gvbridge.dll проверка пропускается с кодом 0: сборку
стережёт scripts/build-viewer.ps1.

Запуск: python modules/gcode-viewer/test_qt_bridge.py (exit 0 = пойдено)
"""
import ctypes
import gc
import hashlib
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

SHIM = os.path.join(ROOT, "build", "cpp", "gvqtshim.dll")
BRIDGE = os.path.join(ROOT, "build", "cpp", "gvbridge.dll")
IMPORT_PATH = os.path.join(ROOT, "build", "edges")
MODULE_DIR = os.path.join(IMPORT_PATH, "CNC", "Edges")
NGC = os.path.join(ROOT, "data", "cad-testdata", "Test_model.ngc")
STEP = os.path.join(ROOT, "data", "cad-testdata", "Test_model.STEP")
# Заморожено test_stepledges_plugin.py: контур Test_model.STEP.
STEP_SEGMENTS = 2426
STEP_RAW_SEGMENTS = 4884

# Проверка переключения классов идёт отдельным процессом: подмена читается
# контроллером при импорте, а в этом процессе MockBackend уже зарегистрирован.
FLAG_SNIPPET = """
import sys
sys.path.insert(0, {here!r})
import controller, geometry, gv_qtgeom
for name in ("ToolpathGeometry", "HLRLineGeometry", "MeshGeometry", "ToolGeometry"):
    got = getattr(controller, name)
    want = getattr(gv_qtgeom, name)
    assert got is want, "%s не C++: %s" % (name, got)
for name in ("GridGeometry", "AxisArrowGeometry"):
    got = getattr(controller, name)
    want = getattr(geometry, name)
    assert got is want, "%s ушёл из Python: %s" % (name, got)
print("FLAG OK")
"""


def module_path(name):
    """Полный путь загруженного в процесс модуля по имени."""
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.GetModuleHandleW.argtypes = [ctypes.c_wchar_p]
    k32.GetModuleHandleW.restype = ctypes.c_void_p
    k32.GetModuleFileNameW.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_uint]
    k32.GetModuleFileNameW.restype = ctypes.c_uint
    handle = k32.GetModuleHandleW(name)
    if not handle:
        return None
    buf = ctypes.create_unicode_buffer(4096)
    if not k32.GetModuleFileNameW(handle, buf, 4096):
        return None
    return buf.value


def load_shim():
    """Загрузка gvqtshim.dll с типами C-ABI: без argtypes ctypes портит указатели."""
    lib = ctypes.WinDLL(SHIM)
    for name in ("gvq_qt_build_version", "gvq_qt_runtime_version", "gvq_qt_core_path"):
        getattr(lib, name).restype = ctypes.c_char_p
        getattr(lib, name).argtypes = []
    lib.gvq_make_test_geometry.restype = ctypes.c_void_p
    lib.gvq_make_test_geometry.argtypes = []
    lib.gvq_make_toolpath.restype = ctypes.c_void_p
    lib.gvq_make_toolpath.argtypes = []
    # Сборка геометрий этапа 3 для живого вьювера (этап 5.3а).
    lib.gvq_build_toolpath.restype = ctypes.c_void_p
    lib.gvq_build_toolpath.argtypes = [
        ctypes.POINTER(ctypes.c_ubyte), ctypes.c_int, ctypes.POINTER(ctypes.c_double),
        ctypes.c_int, ctypes.c_int]
    lib.gvq_build_hlr.restype = ctypes.c_void_p
    lib.gvq_build_hlr.argtypes = [
        ctypes.POINTER(ctypes.c_float), ctypes.c_int, ctypes.POINTER(ctypes.c_double)]
    lib.gvq_build_grid.restype = ctypes.c_void_p
    lib.gvq_build_grid.argtypes = [ctypes.POINTER(ctypes.c_double)]
    lib.gvq_build_axis.restype = ctypes.c_void_p
    lib.gvq_build_axis.argtypes = [ctypes.POINTER(ctypes.c_double)]
    lib.gvq_build_mesh.restype = ctypes.c_void_p
    lib.gvq_build_mesh.argtypes = [
        ctypes.POINTER(ctypes.c_double), ctypes.c_int, ctypes.POINTER(ctypes.c_double)]
    lib.gvq_mesh_reset.restype = None
    lib.gvq_mesh_reset.argtypes = [ctypes.c_void_p]
    lib.gvq_build_tool.restype = ctypes.c_void_p
    lib.gvq_build_tool.argtypes = [
        ctypes.POINTER(ctypes.c_double), ctypes.c_int, ctypes.c_int]
    lib.gvq_vertex_copy.restype = ctypes.c_int
    lib.gvq_vertex_copy.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(ctypes.POINTER(ctypes.c_ubyte)),
        ctypes.POINTER(ctypes.c_int)]
    lib.gvq_buf_free.restype = None
    lib.gvq_buf_free.argtypes = [ctypes.POINTER(ctypes.c_ubyte)]
    lib.gvq_bounds_unpadded.restype = ctypes.c_int
    lib.gvq_bounds_unpadded.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_double)]
    lib.gvq_toolpath_set_packed.restype = None
    lib.gvq_toolpath_set_packed.argtypes = [
        ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int, ctypes.POINTER(ctypes.c_double),
        ctypes.c_int]
    lib.gvq_toolpath_vertex_count.restype = ctypes.c_int
    lib.gvq_toolpath_vertex_count.argtypes = [ctypes.c_void_p]
    lib.gvq_alive.restype = ctypes.c_int
    lib.gvq_alive.argtypes = [ctypes.c_void_p]
    lib.gvq_live_count.restype = ctypes.c_int
    lib.gvq_live_count.argtypes = []
    lib.gvq_free_object.restype = None
    lib.gvq_free_object.argtypes = [ctypes.c_void_p]
    return lib


def check_qt_object(shim, QQuick3DGeometry, shiboken6, problems):
    """Объект Qt из C++-библиотеки, обёрнутый shiboken6, должен быть живым."""
    ptr = shim.gvq_make_test_geometry()
    if not ptr:
        problems.append("gvq_make_test_geometry вернул NULL")
        return
    geo = shiboken6.wrapInstance(ptr, QQuick3DGeometry)
    raw = bytes(range(20))
    geo.setStride(20)
    geo.setVertexData(raw)
    geo.setPrimitiveType(QQuick3DGeometry.PrimitiveType.Lines)
    geo.addAttribute(QQuick3DGeometry.Attribute.Semantic.PositionSemantic, 0,
                     QQuick3DGeometry.Attribute.ComponentType.F32Type)
    geo.update()
    if int(geo.stride()) != 20:
        problems.append("stride обёрнутого объекта %s, ожидался 20" % geo.stride())
    if int(geo.attributeCount()) != 1:
        problems.append("attributeCount %s, ожидался 1" % geo.attributeCount())
    if int(geo.primitiveType().value) != 2:
        problems.append("primitiveType %s, ожидались Lines" % geo.primitiveType())
    if bytes(geo.vertexData()) != raw:
        problems.append("буфер обёрнутого объекта не совпал с записанным")
    del geo
    gc.collect()
    if shim.gvq_alive(ptr):
        print("  владение объектом осталось у C++ (gvq_alive после del + gc)")
        shim.gvq_free_object(ptr)
    else:
        print("  объект удалён питоновским владельцем: shim освобождать не должен")
    if shim.gvq_live_count() != 0:
        problems.append("живых объектов шима %d, ожидался 0" % shim.gvq_live_count())


def check_toolpath(shim, QQuick3DGeometry, shiboken6, problems):
    """ToolpathGeometry этапа 3 на реальных ходах: буфер как у питоновской."""
    import gv_cpp
    from geometry import ToolpathGeometry as PyToolpath
    from gv_types import Aabb

    if not gv_cpp.available():
        problems.append("мост gvbridge не загрузился: %s" % gv_cpp.load_error())
        return
    with open(NGC, "r", encoding="utf-8", errors="ignore") as fh:
        text = fh.read()
    pd = gv_cpp.prepare_path(text)
    reference = PyToolpath()
    reference.set_packed(pd.feed_bytes, Aabb(tuple(pd.bounds.lo), tuple(pd.bounds.hi)), pd.feed_nv)
    want = hashlib.sha256(bytes(reference.vertexData())).hexdigest()

    ptr = shim.gvq_make_toolpath()
    if not ptr:
        problems.append("gvq_make_toolpath вернул NULL")
        return
    bounds = (ctypes.c_double * 6)(*pd.bounds.lo, *pd.bounds.hi)
    shim.gvq_toolpath_set_packed(ptr, pd.feed_bytes, len(pd.feed_bytes), bounds, pd.feed_nv)
    geo = shiboken6.wrapInstance(ptr, QQuick3DGeometry)
    got = hashlib.sha256(bytes(geo.vertexData())).hexdigest()
    print("  контур: вершин %d, stride %d" % (shim.gvq_toolpath_vertex_count(ptr), geo.stride()))
    if shim.gvq_toolpath_vertex_count(ptr) != pd.feed_nv:
        problems.append("вершин %d, у ядра %d" % (shim.gvq_toolpath_vertex_count(ptr), pd.feed_nv))
    if int(geo.stride()) != 20:
        problems.append("stride %s, ожидался 20" % geo.stride())
    if got != want:
        problems.append("sha256 буфера %s, у питоновской геометрии %s" % (got, want))
    else:
        print("  sha256 буфера = питоновской геометрии: %s" % got[:16])
    del geo
    gc.collect()
    if shim.gvq_alive(ptr):
        shim.gvq_free_object(ptr)


def _u8(raw):
    """Указатель на байты буфера: у ctypes нет bytes для POINTER(c_ubyte)."""
    import numpy as np

    if not raw:
        return None
    return np.frombuffer(raw, dtype=np.uint8).ctypes.data_as(
        ctypes.POINTER(ctypes.c_ubyte))


def _f64(values):
    """Плоский float64-массив для C-ABI (вход геометрий пишется в float64)."""
    import numpy as np

    arr = np.ascontiguousarray(np.asarray(values, dtype=np.float64)).reshape(-1)
    return arr.ctypes.data_as(ctypes.POINTER(ctypes.c_double)), int(arr.size)


def _cpp_cases(shim, inputs):
    """Те же 14 случаев, что у test_geometry_core.py, но геометрию строит шим.

    Порядок и числа обязаны совпадать с питоновскими случаями: сравнение идёт
    попарно по имени, поэтому здесь не «похожие», а те же входные данные.
    """
    out = []
    lo, hi = inputs["tp_bounds"].lo, inputs["tp_bounds"].hi
    bounds = (ctypes.c_double * 6)(*[float(v) for v in lo], *[float(v) for v in hi])
    raw = inputs["tp_raw"]
    out.append(("toolpath", shim.gvq_build_toolpath(
        _u8(raw), len(raw), bounds, 1, int(inputs["tp_nv"]))))
    out.append(("toolpath_nobounds", shim.gvq_build_toolpath(
        _u8(raw), len(raw), None, 0, int(inputs["tp_nv"]))))
    out.append(("toolpath_empty", shim.gvq_build_toolpath(None, 0, None, 0, 0)))

    segs, seg_count = _f64_float(inputs["hlr"])
    ref = (ctypes.c_double * 3)(*[float(v) for v in inputs["hlr_ref"]])
    out.append(("hlr", shim.gvq_build_hlr(segs, seg_count, ref)))
    out.append(("hlr_noref", shim.gvq_build_hlr(segs, seg_count, None)))
    out.append(("hlr_empty", shim.gvq_build_hlr(None, 0, None)))

    for name in ("grid", "grid_flat"):
        params = (ctypes.c_double * 6)(*[float(v) for v in inputs[name]])
        out.append((name, shim.gvq_build_grid(params)))
    for name in ("axis", "axis_short"):
        params = (ctypes.c_double * 4)(*[float(v) for v in inputs[name]])
        out.append((name, shim.gvq_build_axis(params)))

    verts, vert_floats = _f64(inputs["mesh"])
    out.append(("mesh", shim.gvq_build_mesh(verts, vert_floats, None)))
    # Эталон этот случай строит как set_triangles + reset: у пустого меша шаг
    # вершины остаётся от прошлого наполнения (так же ведёт себя пробник).
    empty_mesh = shim.gvq_build_mesh(verts, vert_floats, None)
    shim.gvq_mesh_reset(empty_mesh)
    out.append(("mesh_empty", empty_mesh))

    prof, prof_floats = _f64([v for p in inputs["tool"] for v in (p.radius_mm, p.height_mm)])
    out.append(("tool", shim.gvq_build_tool(prof, prof_floats // 2, 48)))
    low, low_floats = _f64([v for p in inputs["low"] for v in p])
    out.append(("tool_low", shim.gvq_build_tool(low, low_floats // 2, 3)))
    return out


def _f64_float(values):
    """(указатель на float32-звенья, число звеньев)."""
    import numpy as np

    arr = np.ascontiguousarray(np.asarray(values, dtype=np.float32))
    if arr.ndim == 2:
        arr = arr.reshape(-1, 2, 3)
    return arr.ctypes.data_as(ctypes.POINTER(ctypes.c_float)), int(arr.shape[0])


def _wrapper_cases(geo, inputs):
    """Те же числа, но через оболочку живого вьювера gv_qtgeom.py."""
    from gv_types import Aabb

    out = []
    g = geo.ToolpathGeometry()
    g.set_packed(inputs["tp_raw"],
                 Aabb(tuple(inputs["tp_bounds"].lo), tuple(inputs["tp_bounds"].hi)),
                 inputs["tp_nv"])
    out.append(("toolpath", g))
    g = geo.HLRLineGeometry()
    g.set_reference(inputs["hlr_ref"])
    g.set_segments(inputs["hlr"])
    out.append(("hlr", g))
    g = geo.MeshGeometry()
    g.set_triangles(inputs["mesh"])
    out.append(("mesh", g))
    g = geo.ToolGeometry()
    g.set_profile(inputs["tool"], 48)
    out.append(("tool", g))
    return out


# Поля состояния геометрии, которые обязаны совпасть: сам буфер, шаг вершины,
# тип примитива, габарит и набор атрибутов. Имена полей - как у test_geometry_core.
STATE_FIELDS = ("stride", "primitiveType", "attributeCount", "boundsMin",
                "boundsMax", "bytes", "sha256", "data")


def _state_diff(ref, got):
    """Расхождения состояния C++-геометрии с питоновским эталоном."""
    bad = []
    for field in STATE_FIELDS:
        if ref[field] != got[field]:
            bad.append("%s: C++ %s против эталона %s" % (field, got[field], ref[field]))
    for i in range(int(ref["attributeCount"])):
        key = "attribute%d" % i
        if ref[key] != got.get(key):
            bad.append("%s: C++ %s против эталона %s" % (key, got.get(key), ref[key]))
    return bad


def check_geometry_buffers(shim, QQuick3DGeometry, shiboken6, problems):
    """Шесть геометрий этапа 3 в живом процессе: буфер C++ против эталона.

    Числа берутся у test_geometry_core.py (тот же набор из 14 случаев, включая
    пустые геометрии), но проверка идёт здесь, в процессе PySide6: важно, что
    объекты gvgeom, созданные библиотекой, собранной против отдельной Qt, дают
    в этом процессе тот же буфер, что питоновский эталон geometry.py. Отдельно
    проходят те же числа через оболочку gv_qtgeom.py, которой пользуется живой
    вьювер: она ставит байты C++ в геометрию, которой владеет QML.
    """
    import numpy as np

    import mock_backend as mb
    import test_geometry_core as core
    import geometry

    in_dir = os.path.join(tempfile.gettempdir(), "gvqt_bridge", "in")
    os.makedirs(in_dir, exist_ok=True)
    inputs = core._write_inputs(in_dir, np, mb)
    reference = {name: core._state(g, 0, 0)
                 for name, g, _nv, _tri in core._build_geometries(geometry, inputs)}

    for name, ptr in _cpp_cases(shim, inputs):
        if not ptr:
            problems.append("геометрия %s: шим вернул NULL" % name)
            continue
        geo = shiboken6.wrapInstance(ptr, QQuick3DGeometry)
        state = core._state(geo, 0, 0)
        bad = _state_diff(reference[name], state)
        for line in bad:
            problems.append("C++-геометрия %s: %s" % (name, line))
        print("  %-17s вершин %6d stride %2d атрибутов %d буфер %7d байт  %s"
              % (name, state["bytes"] // max(1, state["stride"]), state["stride"],
                 state["attributeCount"], state["bytes"],
                 "совпало" if not bad else "РАСХОЖДЕНИЕ"))
        del geo
        gc.collect()
        shim.gvq_free_object(ptr)

    import gv_qtgeom
    for name, geo in _wrapper_cases(gv_qtgeom, inputs):
        bad = _state_diff(reference[name], core._state(geo, 0, 0))
        for line in bad:
            problems.append("оболочка %s: %s" % (name, line))
        print("  оболочка %-8s -> %s" % (name, "совпало" if not bad else "РАСХОЖДЕНИЕ"))


def check_flag_switch(problems):
    """CNC_CPP=qtgeom переключает бэкендные геометрии на C++, а QML-типы - нет.

    Проверка отдельным процессом: подмена классов читается контроллером при
    импорте, а в этом процессе MockBackend уже зарегистрирован - повторная
    регистрация типов была бы двойной.
    """
    env = dict(os.environ, CNC_CPP="qtgeom", QT_QPA_PLATFORM="offscreen")
    res = subprocess.run([sys.executable, "-c", FLAG_SNIPPET.format(here=HERE)],
                         capture_output=True, text=True, env=env, cwd=ROOT)
    line = (res.stdout + res.stderr).strip().splitlines()[-1:] or [""]
    print("  переключение классов: код %d, %s" % (res.returncode, line[0]))
    if res.returncode != 0 or "FLAG OK" not in res.stdout:
        problems.append("CNC_CPP=qtgeom не переключил геометрии: %s"
                        % (res.stdout + res.stderr)[-400:])


def check_wiring(added, problems):
    """Каталог модуля CNC/Edges в PATH: его туда кладёт helper точек входа."""
    parts = [p for p in os.environ.get("PATH", "").split(os.pathsep) if p]
    if MODULE_DIR not in added.split(os.pathsep):
        problems.append("проводка PATH не добавила каталог модуля: %r" % added)
    elif MODULE_DIR not in parts:
        problems.append("каталог модуля назван, но в PATH его нет: %r" % parts[:3])
    else:
        print("  проводка PATH: %s добавлен helper'ом" % MODULE_DIR)


def check_qml(problems, warnings):
    """QML-модуль CNC.Edges в этом же процессе: StepEdges и его контур."""
    from PySide6.QtGui import QGuiApplication
    from PySide6.QtQml import QQmlApplicationEngine, QQmlComponent

    qml_file = os.path.join(IMPORT_PATH, "qt_bridge_test.qml")
    with open(qml_file, "w", encoding="utf-8") as fh:
        fh.write('import QtQuick\nimport QtQuick.Window\nimport QtQuick3D\n'
                 'import CNC.Edges 1.0\n\n'
                 'Window {\n    width: 320\n    height: 240\n    visible: false\n'
                 '    View3D {\n        anchors.fill: parent\n'
                 '        StepEdges {\n            objectName: "stepEdges"\n'
                 '            source: "%s"\n        }\n    }\n}\n'
                 % STEP.replace("\\", "/"))
    engine = QQmlApplicationEngine()
    engine.warnings.connect(lambda errs: warnings.extend(errs))
    engine.addImportPath(IMPORT_PATH)
    comp = QQmlComponent(engine, qml_file)
    if comp.status() != QQmlComponent.Status.Ready:
        problems.append("компонент не собран: %s"
                        % "; ".join(e.toString() for e in comp.errors()))
        return
    win = comp.create()
    if win is None:
        problems.append("окно не создано")
        return
    win.show()
    QGuiApplication.processEvents()
    from PySide6.QtCore import QObject
    edges = win.findChild(QObject, "stepEdges")
    if edges is None:
        problems.append("StepEdges не найден в дереве окна")
        return
    count = int(edges.property("segmentCount") or 0)
    raw = int(edges.property("rawSegmentCount") or 0)
    print("  StepEdges: segmentCount=%d rawSegmentCount=%d" % (count, raw))
    if count != STEP_SEGMENTS or raw != STEP_RAW_SEGMENTS:
        problems.append("отрезков %d/%d, ожидалось %d/%d"
                        % (count, raw, STEP_SEGMENTS, STEP_RAW_SEGMENTS))
    if warnings:
        problems.append("warning движка: %s" % "; ".join(str(w) for w in warnings[:3]))


def main() -> int:
    if not (os.path.exists(SHIM) and os.path.exists(BRIDGE) and os.path.exists(MODULE_DIR)):
        print("Qt-мост: пропущено (не собрано; соберите scripts/build-viewer.ps1)")
        return 0
    if not (os.path.exists(NGC) and os.path.exists(STEP)):
        print("Qt-мост: пропущено (нет данных %s или %s)"
              % (os.path.basename(NGC), os.path.basename(STEP)))
        return 0
    try:
        from PySide6.QtGui import QGuiApplication
        from PySide6.QtQuick3D import QQuick3DGeometry
        import shiboken6
    except ImportError as exc:
        print("Qt-мост: пропущено (нет PySide6: %s)" % exc)
        return 0

    problems = []
    warnings = []

    import gv_qtgeom

    # Проводка PATH - тем же helper'ом, что у точек входа вьювера: каталог
    # QML-модуля и DLL OCCT обязаны попасть в PATH до создания движка.
    check_wiring(gv_qtgeom.prepare_dll_path(), problems)
    app = QGuiApplication(sys.argv)
    _ = app

    qt_dirs = [p for p in os.environ.get("PATH", "").split(os.pathsep)
               if os.path.abspath(p).lower().startswith(r"c:\qt")]
    if qt_dirs:
        problems.append("каталог C:\\Qt попал в PATH: %s" % "; ".join(qt_dirs))
    core_before = module_path("Qt6Core.dll")
    if not core_before or "site-packages" not in core_before:
        problems.append("Qt6Core.dll загружена не из PySide6: %s" % core_before)
    else:
        print("Qt6Core.dll: %s" % core_before)

    shim = load_shim()
    build_version = shim.gvq_qt_build_version().decode()
    runtime_version = shim.gvq_qt_runtime_version().decode()
    print("Qt шима при сборке %s, в процессе %s" % (build_version, runtime_version))
    if build_version != runtime_version:
        problems.append("версии Qt разошлись: сборка %s, процесс %s"
                        % (build_version, runtime_version))
    if shim.gvq_qt_core_path().decode() != (core_before or ""):
        problems.append("C++ видит другую Qt6Core.dll: %s" % shim.gvq_qt_core_path().decode())

    check_qt_object(shim, QQuick3DGeometry, shiboken6, problems)
    check_toolpath(shim, QQuick3DGeometry, shiboken6, problems)
    check_geometry_buffers(shim, QQuick3DGeometry, shiboken6, problems)
    check_flag_switch(problems)
    check_qml(problems, warnings)

    if problems:
        for text in problems:
            print("FAIL: %s" % text)
        print("RESULT: FAIL")
        return 1
    print("RESULT: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
