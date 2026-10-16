#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""C++-контроллер вьювера против питоновского эталона (этапы 5.4а и 5.4б).

Проверяется одно и то же УП (data/cad-testdata/Test_model.ngc) вместе с моделью
рядом с ним (Test_model.STEP), загруженное двумя путями:

  * питоновский эталон - mock_backend (controller.py, @QmlElement) в отдельном
    процессе с пустым CNC_CPP;
  * C++-контроллер - QML-плагин MockBackend (cpp/qmlbackend, цель gvqmlbackend) в
    отдельном процессе с CNC_CPP=controller.

Процессы разные не случайно: движок отказывается от двух регистраций одного имени
("Namespace 'MockBackend' has already been used for type registration"), поэтому
питоновские модули того же URI в процессе с плагином не импортируются, и один
процесс не может показать оба пути.

Что сверяется: число сегментов, свойства center/radius/moveLength/minY,
toolPosition (и toolX/Y/Z), toolInfo, этапы (диапазоны, подписи, теги UI_TOOL),
holderGauge, modelPath, заготовка (hasStock/stockCenter/stockSize), буферы
rapid/feed (число вершин, длина и sha256 байт, stride-контракт тот же), выборка
sampleAt/isRapidAt/speedAt на фиксированных длинах, пустой toolCheck и число
предупреждений движка (у C++-пути их не должно быть ни одного).

Модель сверяется двумя снимками - после load_gcode (ветка STEP) и после
loadModel (ветка STL, файл Test_model.STL рядом): буферы partGeometry, partEdges,
hlrEdges и силуэт hlrTangents в обоих режимах CNC_SILHOUETTE (mesh и analytic),
габарит (partMeshCenter, partRadius) и трансформа детали
(partRotation, partPosition) с числом вершин и sha256 байт вершинного буфера.
Силуэт считается слотом refreshTangents с одним и тем же глазом в обоих
процессах, поэтому числа сопоставимы.

Чего в сверке нет (заглушки этого шага, следующие части этапа 5.4): скан коллизий
(collisions/collisionCount) и инструмент (toolGeometry, toolOutline, toolRings,
holderGeometry, holderOutline, holderRings, toolLength, toolRadius, holderRadius).
Итог перечисляет их отдельной строкой, чтобы остаток был назван, а не замолчан.

Без собранного модуля build/qmlbackend/MockBackend проверка пропускается с кодом 0:
сборку стережёт scripts/build-viewer.ps1.

Запуск: python modules/gcode-viewer/test_controller_cpp.py (exit 0 = пойдено)
"""
import hashlib
import json
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, HERE)

os.environ.setdefault("QT_QPA_PLATFORM", "offscreen")

NGC = os.path.join(ROOT, "data", "cad-testdata", "Test_model.ngc")
STL = os.path.join(ROOT, "data", "cad-testdata", "Test_model.STL")
MODULE_DIR = os.path.join(ROOT, "build", "qmlbackend", "MockBackend")
MODULE_QMLDIR = os.path.join(MODULE_DIR, "qmldir")

# Глаз для силуэта: одна и та же точка сцены в обоих процессах (иначе числа
# силуэта несопоставимы).
EYE = (120.0, 90.0, 140.0)

# Числа, которые обязаны совпасть: сравнение идёт побитово (float64 у свойств и
# у выборки, float32 внутри буферов), поэтому допуска здесь нет.
SCALARS = ("moveLength", "radius", "minY", "toolX", "toolY", "toolZ", "holderGauge")
VECTORS = ("center", "toolPosition", "stockCenter", "stockSize")
LISTS = ("stages", "toolCheck")
MAPS = ("toolInfo",)
FLAGS = ("loading", "hasStock")
INTS = ("toolIssueCount",)

# Заглушки шага: у питоновского пути они заполнены, у C++ - пусты по замыслу
# (скан коллизий и инструмент).
DEFERRED = ("collisions", "collisionCount", "toolLength", "toolRadius", "holderRadius",
            "toolGeometry", "toolOutline", "toolRings", "holderGeometry", "holderOutline",
            "holderRings")

# Поля снимка модели: сверяются и для STEP, и для STL.
MODEL_FIELDS = ("partMeshCenter", "partRadius", "partRotation", "partPosition",
                "modelPath")
# Буферы снимка модели: имя свойства, поле счётчика и раскладка для сверки
# набором отрезков (вершин на отрезок, float на вершину; None - побитовая сверка).
MODEL_BUFFERS = (("partGeometry", "triangleCount", None),
                 ("partEdges", "vertexCount", (2, 5)),
                 ("hlrEdges", "vertexCount", (6, 12)))

QML = b'import MockBackend 1.0\nGcodeToolpath { objectName: "gcodeToolpath" }\n'


def _vec3(value):
    """QVector3D -> список трёх чисел (None - свойства нет)."""
    if value is None:
        return None
    return [float(value.x()), float(value.y()), float(value.z())]


def _quat(value):
    """QQuaternion -> список четырёх чисел scalar, x, y, z (None - свойства нет)."""
    if value is None:
        return None
    return [float(value.scalar()), float(value.x()), float(value.y()), float(value.z())]


def _backend():
    """Контроллер текущего процесса и признак C++-пути.

    Путь выбирает тот же helper, что у точек входа вьювера: CNC_CPP=controller и
    собранный модуль. Питоновскую регистрацию в C++-процесс не тянем.

    Возвращает ещё и движок с компонентом: их уничтожение удаляет созданный объект
    (libshiboken: Internal C++ object already deleted), поэтому держать их обязан
    вызывающий, пока читает свойства.
    """
    import gv_cpp
    cpp = gv_cpp.controller_enabled()
    if cpp:
        # Каталог модуля в PATH до создания движка: точка входа плагина ищет свои
        # зависимости по PATH, а не через add_dll_directory (см. preview_3d.py).
        gv_cpp.prepare_qml_backend_path()

    from PySide6.QtCore import QUrl
    from PySide6.QtQml import QQmlComponent, QQmlEngine
    # QQuick3DGeometry* в свойствах контроллера читается через конвертер PySide6:
    # без импорта модуля QtQuick3D чтение свойства падает.
    from PySide6.QtQuick3D import QQuick3DGeometry  # noqa: F401

    if not cpp:
        import mock_backend  # noqa: F401  (регистрирует "MockBackend")

    engine = QQmlEngine()
    engine.addImportPath(HERE)
    if cpp:
        engine.addImportPath(gv_cpp.qml_backend_import_path())
    warnings = []
    engine.warnings.connect(warnings.extend)
    component = QQmlComponent(engine)
    component.setData(QML, QUrl())
    if component.status() != QQmlComponent.Status.Ready:
        raise RuntimeError("QML-компонент не собран: %s"
                           % "; ".join(e.toString() for e in component.errors()))
    backend = component.create()
    if backend is None:
        raise RuntimeError("GcodeToolpath не создан")
    return backend, cpp, warnings, (engine, component)


def _canonical_segments(raw, verts_per_segment, stride_floats):
    """Набор отрезков буфера: координаты округлены до 1e-4 мм, куски отсортированы.

    Порядок склеенных рёбер STEP побитово не воспроизводится: эталон упорядочивает
    кластеры неустойчивым numpy.argsort (quicksort) и несёт остаток 1e-15 от
    порядка суммирования, C++ сортирует устойчиво (тот же разбор и та же мера -
    test_stepledges_plugin.py). Поэтому буфер сверяется как набор отрезков, а
    форма каждого отрезка (порядок концов, нормали и сторона ленты) - побитово.
    """
    import numpy as np
    arr = np.frombuffer(raw, dtype="<f4").reshape(-1, verts_per_segment, stride_floats)
    rounded = np.round(arr[:, :, 0:3], 4)
    rounded = np.where(rounded == 0.0, np.float32(0.0), rounded)
    buf = np.ascontiguousarray(rounded, dtype="<f4").tobytes()
    chunk = verts_per_segment * 3 * 4
    parts = [buf[i:i + chunk] for i in range(0, len(buf), chunk)]
    return {"segments": len(parts),
            "set_sha256": hashlib.sha256(b"".join(sorted(parts))).hexdigest()}


def _buffer(backend, name, count_field="vertexCount", canonical=None):
    """Буфер геометрии: число вершин, длина и sha256 байт вершинного буфера.

    У меша счётчик называется triangleCount, у линий - vertexCount.
    canonical задаёт раскладку для сверки набором отрезков (см.
    _canonical_segments): у таких буферов побитовый хэш не сверяется.
    """
    geo = backend.property(name)
    if geo is None:
        return {"present": False}
    raw = bytes(geo.vertexData())
    count = geo.property(count_field)
    if count is None:
        count = getattr(geo, count_field, 0)
    info = {"present": True, "nv": int(count or 0), "bytes": len(raw)}
    if canonical is not None and raw:
        info.update(_canonical_segments(raw, canonical[0], canonical[1]))
    else:
        info["sha256"] = hashlib.sha256(raw).hexdigest()
    return info


def _settle(app, backend, timeout_s=30.0):
    """Дождаться обводки: она публикуется вторым проходом (эталон - свой поток,
    C++ - задача на пуле), поэтому до прокрутки очереди событий буфер пуст."""
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        app.processEvents()
        edges = backend.property("partEdges")
        hlr = backend.property("hlrEdges")
        edges_nv = int(edges.property("vertexCount") or 0) if edges is not None else 0
        hlr_nv = int(hlr.property("vertexCount") or 0) if hlr is not None else 0
        if edges_nv > 0 and hlr_nv > 0:
            app.processEvents()
            return
        time.sleep(0.02)
    app.processEvents()


def _model_snapshot(app, backend):
    """Снимок модели: буферы, габарит, трансформа и силуэт в обоих режимах."""
    from PySide6.QtGui import QVector3D
    _settle(app, backend)
    snap = {}
    for name, field, canonical in MODEL_BUFFERS:
        snap[name] = _buffer(backend, name, field, canonical)
    previous = os.environ.get("CNC_SILHOUETTE")
    for mode, key in (("mesh", "hlrTangentsMesh"), ("analytic", "hlrTangentsAnalytic")):
        os.environ["CNC_SILHOUETTE"] = mode
        backend.refreshTangents(QVector3D(*EYE))
        snap[key] = _buffer(backend, "hlrTangents")
    if previous is None:
        os.environ.pop("CNC_SILHOUETTE", None)
    else:
        os.environ["CNC_SILHOUETTE"] = previous
    for name in MODEL_FIELDS:
        value = backend.property(name)
        if name == "partRotation":
            snap[name] = _quat(value)
        elif name == "partMeshCenter" or name == "partPosition":
            snap[name] = _vec3(value)
        else:
            snap[name] = value
    return snap


def _samples(backend, total):
    """Выборка вдоль пути: те же длины в обоих процессах."""
    dists = [0.0, 1.0, 100.0, 2303.0, total * 0.001, total * 0.25, total * 0.5,
             total * 0.75, total * 0.999, total]
    return [{"d": d,
             "point": _vec3(backend.sampleAt(d)),
             "rapid": bool(backend.isRapidAt(d)),
             "speed": float(backend.speedAt(d))} for d in dists]


def _backend_values(app):
    """Значения контроллера этого процесса одним словарём."""
    backend, cpp, warnings, keep_alive = _backend()
    _ = keep_alive                       # движок и компонент живут до конца чтения
    segments = int(backend.load_gcode(NGC))
    values = {"cpp": cpp,
              "python_registration": ("mock_backend" in sys.modules
                                      or "geometry" in sys.modules),
              "segments": segments,
              "modelPath": backend.property("modelPath"),
              "rapid": _buffer(backend, "rapidGeometry"),
              "feed": _buffer(backend, "feedGeometry"),
              "warnings": [str(w) for w in warnings]}
    for name in SCALARS + FLAGS:
        values[name] = backend.property(name)
    for name in INTS:
        values[name] = int(backend.property(name) or 0)
    for name in VECTORS:
        values[name] = _vec3(backend.property(name))
    for name in LISTS:
        values[name] = backend.property(name)
    for name in MAPS:
        values[name] = backend.property(name)
    total = float(values["moveLength"] or 0.0)
    values["samples"] = _samples(backend, total)
    # Этапы QML читает по полям: сверяем именно словари, а не их число.
    values["stage_count"] = len(values["stages"] or [])
    # Модель рядом с УП (STEP) и та же деталь в формате STL: обе ветки чтения.
    values["step"] = _model_snapshot(app, backend)
    if os.path.exists(STL):
        backend.loadModel(STL)
        values["stl"] = _model_snapshot(app, backend)
    return values


def _run_dump(out_path, mode):
    """Прогон одного пути в отдельном процессе (env задаёт mode).

    CNC_NO_CACHE=1 обязателен: дисковый кэш носит суп float32, а C++-контроллер
    читает модель всегда заново (перенос кэша - отдельное решение, PORT-CPP §5).
    С тёплым кэшем эталон сравнивался бы не с тем же расчётом: границы габарита и
    сварка вершин у float32-супа другие. Оба процесса идут холодным путём.
    """
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen", CNC_CPP=mode,
               CNC_NO_CACHE="1")
    code = ("import sys; sys.path.insert(0, %r); "
            "import test_controller_cpp as t; t._write_dump(%r)" % (HERE, out_path))
    res = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True,
                         env=env, cwd=ROOT)
    output = res.stdout + res.stderr
    if res.returncode != 0 or not os.path.exists(out_path):
        return None, output
    with open(out_path, "r", encoding="utf-8") as fh:
        return json.load(fh), output


def _write_dump(path):
    """Точка входа дочернего процесса: дамп и выход.

    Экземпляр приложения обязателен до создания движка (offscreen - у вьювера нет
    окна, а тип и свойства читаются как обычно).
    """
    from PySide6.QtGui import QGuiApplication
    app = QGuiApplication([])
    values = _backend_values(app)
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(values, fh)
    print("DUMP OK segments=%d cpp=%s" % (values["segments"], values["cpp"]))


def _diff(ref, got, problems):
    """Сверка одного поля: расхождение - строкой в список проблем."""
    if ref == got:
        return
    problems.append("поле разошлось: эталон %r, C++ %r" % (ref, got))


def _compare(ref, got, problems):
    """Сверка всех сверяемых полей двух дампов."""
    for name in SCALARS + VECTORS + LISTS + MAPS + FLAGS + INTS:
        _diff(ref.get(name), got.get(name), problems)
    _diff(ref.get("stage_count"), got.get("stage_count"), problems)
    _diff(ref.get("rapid"), got.get("rapid"), problems)
    _diff(ref.get("feed"), got.get("feed"), problems)
    _diff(ref.get("samples"), got.get("samples"), problems)
    _diff(ref.get("modelPath"), got.get("modelPath"), problems)
    # Снимки модели (STEP и STL) сверяются целиком: и буферы, и трансформа.
    for folder in ("step", "stl"):
        _diff(ref.get(folder), got.get(folder), problems)


def main() -> int:
    if not os.path.exists(MODULE_QMLDIR):
        print("C++-контроллер: пропущено (нет %s; соберите scripts/build-viewer.ps1)"
              % MODULE_QMLDIR)
        return 0
    if not os.path.exists(NGC):
        print("C++-контроллер: пропущено (нет УП %s)" % os.path.basename(NGC))
        return 0
    try:
        import PySide6  # noqa: F401
    except ImportError as exc:
        print("C++-контроллер: пропущено (нет PySide6: %s)" % exc)
        return 0

    problems = []
    tmp = tempfile.mkdtemp(prefix="gv_controller_")
    ref_path = os.path.join(tmp, "python.json")
    got_path = os.path.join(tmp, "cpp.json")

    ref, ref_log = _run_dump(ref_path, "")
    if ref is None:
        print(ref_log[-800:])
        print("RESULT: FAIL (питоновский эталон не прогнался)")
        return 1
    got, got_log = _run_dump(got_path, "controller")
    if got is None:
        print(got_log[-800:])
        print("RESULT: FAIL (C++-путь не прогнался)")
        return 1

    if ref.get("cpp") or not got.get("cpp"):
        problems.append("пути перепутаны: эталон cpp=%s, C++ cpp=%s"
                        % (ref.get("cpp"), got.get("cpp")))
    if got.get("python_registration"):
        problems.append("в процессе C++-пути импортирован питоновский MockBackend")
    if got.get("warnings"):
        problems.append("предупреждения движка у C++-пути: %s" % got["warnings"][:3])

    _compare(ref, got, problems)

    print("сегменты: эталон %d, C++ %d" % (ref["segments"], got["segments"]))
    print("буфер rapid: nv %s->%s, sha %s -> %s"
          % (ref["rapid"].get("nv"), got["rapid"].get("nv"),
             (ref["rapid"].get("sha256") or "")[:16],
             (got["rapid"].get("sha256") or "")[:16]))
    print("буфер feed : nv %s->%s, sha %s -> %s"
          % (ref["feed"].get("nv"), got["feed"].get("nv"),
             (ref["feed"].get("sha256") or "")[:16],
             (got["feed"].get("sha256") or "")[:16]))
    print("center %s|%s radius %r|%r moveLength %r|%r minY %r|%r"
          % (ref["center"], got["center"], ref["radius"], got["radius"],
             ref["moveLength"], got["moveLength"], ref["minY"], got["minY"]))
    print("этапов: %d, выборок: %d, предупреждений движка: эталон %d, C++ %d"
          % (ref["stage_count"], len(ref["samples"]), len(ref["warnings"]),
             len(got["warnings"])))
    for folder in ("step", "stl"):
        block_ref, block_got = ref.get(folder), got.get(folder)
        if block_ref is None or block_got is None:
            continue
        print("%s: меш %s->%s tri, обводка %s->%s вершин, контур %s->%s, силуэт "
              "(mesh) %s->%s, силуэт (analytic) %s->%s"
              % (folder.upper(),
                 block_ref["partGeometry"].get("nv"), block_got["partGeometry"].get("nv"),
                 block_ref["partEdges"].get("nv"), block_got["partEdges"].get("nv"),
                 block_ref["hlrEdges"].get("nv"), block_got["hlrEdges"].get("nv"),
                 block_ref["hlrTangentsMesh"].get("nv"),
                 block_got["hlrTangentsMesh"].get("nv"),
                 block_ref["hlrTangentsAnalytic"].get("nv"),
                 block_got["hlrTangentsAnalytic"].get("nv")))
        print("   sha256 меша: %s -> %s; набор обводки %s -> %s (%d отрезков)"
              % ((block_ref["partGeometry"].get("sha256") or "")[:16],
                 (block_got["partGeometry"].get("sha256") or "")[:16],
                 (block_ref["partEdges"].get("set_sha256") or "")[:16],
                 (block_got["partEdges"].get("set_sha256") or "")[:16],
                 block_got["partEdges"].get("segments", 0)))
        print("   sha256 силуэта: mesh %s -> %s, analytic %s -> %s"
              % ((block_ref["hlrTangentsMesh"].get("sha256") or "")[:16],
                 (block_got["hlrTangentsMesh"].get("sha256") or "")[:16],
                 (block_ref["hlrTangentsAnalytic"].get("sha256") or "")[:16],
                 (block_got["hlrTangentsAnalytic"].get("sha256") or "")[:16]))
        print("   partMeshCenter %s|%s partRadius %r|%r partPosition %s|%s"
              % (block_ref["partMeshCenter"], block_got["partMeshCenter"],
                 block_ref["partRadius"], block_got["partRadius"],
                 block_ref["partPosition"], block_got["partPosition"]))
        print("   partRotation %s|%s" % (block_ref["partRotation"], block_got["partRotation"]))
    print("заглушки шага (не сверяются): %s" % ", ".join(DEFERRED))

    if problems:
        for text in problems:
            print("FAIL: %s" % text)
        print("RESULT: FAIL")
        return 1
    print("RESULT: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
