"""Render probe: loads the viewer offscreen (real RHI), grabs a frame and
reports objective line statistics - no eyeballs involved.

    py -3 modules/gcode-viewer/render_probe.py [dist] [mode] [flags]

Режимы: по умолчанию - УП рядом со скриптом; `async` - загрузка УП в рабочем
потоке; `model` - только STEP, без траектории; `none` - без УП и модели.
Флаги: `solid` (непрозрачная кожа), `nostock`, `pitch=`, `yaw=`.
Печатает количество тёмных пикселей обводки (полоса верхней плиты и боковые
стенки) и пикселей заготовки, PNG сохраняет рядом со скриптом. Строкой выше
печатается фактическое состояние камеры, DPR и размер кадра, а также число
треугольников меша и вершин обводки: по ним видно, попал ли в кадр мусор.

Кадр берётся по готовности бэкенда, а не по фиксированной задержке: при
таймерах авто-подгонка камеры в QML (переписывает dist на pathChanged) и
фоновая обводка успевали прийти в разное время, и edge_px/stock_px расходились
в разы между одинаковыми прогонами.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
# Живой дисплей обязателен: Qt Quick 3D на Windows в offscreen отдаёт чёрный
# кадр (см. test_ribbon_width.py). QT_QPA_PLATFORM=offscreen снаружи - на
# свой риск: статистика будет обнулена.

from PySide6.QtCore import QUrl, QTimer, QObject  # noqa: E402
from PySide6.QtGui import QGuiApplication  # noqa: E402
from PySide6.QtQml import QQmlApplicationEngine  # noqa: E402

OUT = os.path.join(HERE, "probe_render.png")
NGC = os.path.abspath(os.path.join(HERE, "..", "..", "data", "cad-testdata", "Test_model.ngc"))
STEP = os.path.abspath(os.path.join(HERE, "..", "..", "data", "cad-testdata", "Test_model.STEP"))

SETTLE_MS = 400           # кадр на неподвижной сцене после установки камеры
READY_TIMEOUT_MS = 10000  # страховка: ждём меш и обводку не дольше этого
POLL_MS = 50
WATCHDOG_MS = 40000


def _is_number(text) -> bool:
    try:
        float(text)
        return True
    except ValueError:
        return False


def _find_viewer(win):
    def walk(o):
        for c in o.findChildren(object):
            if c.property("viewerVersion") is not None:
                return c
            r = walk(c)
            if r is not None:
                return r
        return None
    return walk(win)


def main() -> int:
    argv = sys.argv[1:]
    dist = 250.0
    if argv and _is_number(argv[0]):
        dist = float(argv.pop(0))
    args = argv
    model_only = "model" in args
    no_program = "none" in args

    app = QGuiApplication(sys.argv)
    engine = QQmlApplicationEngine()
    engine.addImportPath(HERE)
    import mock_backend  # noqa: F401  (регистрирует "MockBackend")

    done = {"ok": False, "started": False}
    cam = {}
    refs = {}
    seen = {"mesh": 0}

    def grab():
        img = refs["win"].grabWindow()
        if img.isNull():
            print("FAIL: grabWindow null")
            app.quit()
            return
        img.save(OUT)

        mesh = refs["backend"].property("partGeometry")
        print("camera: dist=%.1f yaw=%.1f pitch=%.1f dpr=%.2f size=%dx%d" % (
            refs["viewer"].property("dist"), refs["viewer"].property("yaw"),
            refs["viewer"].property("pitch"), img.devicePixelRatio(),
            img.width(), img.height()))
        print("geometry: mesh=%s meshChanged-after-load=%d" % (
            mesh.property("triangleCount") if mesh is not None else "-",
            seen["mesh"]))

        try:
            import numpy as np
        except ImportError:
            print("numpy missing; saved PNG only")
            app.quit()
            return
        rgba = img.convertToFormat(img.Format.Format_RGBA8888)
        w, h = rgba.width(), rgba.height()
        buf = bytes(rgba.constBits())
        arr = np.frombuffer(buf, dtype=np.uint8)
        arr = arr.reshape(h, rgba.bytesPerLine() // 4, 4)[:, :w, :]
        r = arr[:, :, 0].astype(int)
        g = arr[:, :, 1].astype(int)
        b = arr[:, :, 2].astype(int)
        # тёмный жжёно-оранжевый контур #7A3E0C: r>g>b, насыщенный
        # чёрная обводка по серому телу: все каналы темные
        edge = (r < 80) & (g < 80) & (b < 80)
        grayish = (abs(r - g) < 18) & (abs(g - b) < 22) \
            & (r > 95) & (r < 190)
        stats = {
            "edge_px": int(edge.sum()),
            "stock_px": int(grayish.sum()),
        }
        for k, v in stats.items():
            print(f"{k}: {v}")
        done["ok"] = True
        app.quit()

    def settle():
        # Камера ставится последней и целиком: qml-авто-подгонка переписывает
        # dist на pathChanged, поэтому просьба dist= должна идти после того,
        # как траектория и обводка уже применены.
        viewer = refs["viewer"]
        viewer.setProperty("dist", dist)
        viewer.setProperty("yaw", cam["yaw"])
        viewer.setProperty("pitch", cam["pitch"])
        viewer.setProperty("panX", 0.0)
        viewer.setProperty("panY", 0.0)
        QTimer.singleShot(SETTLE_MS, grab)

    def wait_ready(elapsed: int):
        backend = refs["backend"]
        if not backend.property("modelPath"):
            settle()                        # модели нет: ждать нечего
            return
        mesh = backend.property("partGeometry")
        mesh_ok = mesh is not None and int(mesh.property("triangleCount") or 0) > 0
        # Обводка публикуется вторым meshChanged (первый - сам меш, он уже
        # прошёл до подключения счётчика).
        edges_ok = seen["mesh"] >= 1
        if mesh_ok and edges_ok:
            settle()
            return
        if elapsed >= READY_TIMEOUT_MS:
            print("warn: readiness timeout (mesh=%s, meshChanged-after-load=%d),"
                  " grabbing as is" % (mesh_ok, seen["mesh"]))
            settle()
            return
        QTimer.singleShot(POLL_MS, lambda: wait_ready(elapsed + POLL_MS))

    def on_mesh_changed():
        seen["mesh"] += 1

    def on_load_finished():
        if done["started"]:
            return
        done["started"] = True
        for a in args:
            if a.startswith("pitch="):
                cam["pitch"] = float(a[6:])
            if a.startswith("yaw="):
                cam["yaw"] = float(a[4:])
            if a == "solid":
                refs["viewer"].setProperty("xray", False)
        refs["backend"].meshChanged.connect(on_mesh_changed)
        wait_ready(0)

    def run():
        roots = engine.rootObjects()
        if not roots:
            print("FAIL: no root objects")
            app.quit()
            return
        win = roots[0]
        viewer = _find_viewer(win)
        if viewer is None:
            print("FAIL: viewer item not found")
            app.quit()
            return
        backend = win.findChild(QObject, "gcodeToolpath")
        if backend is None:
            print("FAIL: backend not found")
            app.quit()
            return
        refs.update(win=win, viewer=viewer, backend=backend)
        cam["yaw"] = viewer.property("yaw")
        cam["pitch"] = viewer.property("pitch")
        print("window version:", viewer.property("viewerVersion"))
        # Слои, не входящие в метрику, гасятся до загрузки, а не за 400 мс до
        # захвата: переключение на глазах могло попасть в измеряемый кадр.
        viewer.setProperty("showToolpath", False)
        viewer.setProperty("showCollisions", False)
        if "nostock" in args:
            viewer.setProperty("showStockVisible", False)
        backend.loadFinished.connect(on_load_finished)
        if model_only and os.path.isfile(STEP):
            backend.loadModel(STEP)
            on_load_finished()              # loadModel эмитит сигнал сам
        elif no_program:
            on_load_finished()
        elif "async" in args and os.path.isfile(NGC):
            backend.load_gcode_async(NGC)   # реальный путь вьювера, сигнал сам
        elif os.path.isfile(NGC):
            backend.load_gcode(NGC)         # синхронный путь, сигнала нет
            on_load_finished()
        else:
            on_load_finished()

    engine.load(QUrl.fromLocalFile(os.path.join(HERE, "preview_3d.qml")))
    if not engine.rootObjects():
        print("FAIL: QML did not load")
        return 1
    QTimer.singleShot(500, run)
    QTimer.singleShot(WATCHDOG_MS, app.quit)
    app.exec()
    print("probe image:", OUT)
    return 0 if done["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
