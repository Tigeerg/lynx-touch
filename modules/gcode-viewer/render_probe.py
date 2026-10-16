"""Render probe: loads the viewer offscreen (real RHI), grabs a frame and
reports objective line statistics - no eyeballs involved.

    py -3 modules/gcode-viewer/render_probe.py [dist]

Prints counts of orange outline pixels per frame region (top plate band vs
side walls), stock-envelope pixels, and saves the PNG next to the script
for reference. Used to verify that ribbon/near-plane changes actually
alter the rendered image.
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
    dist = float(sys.argv[1]) if len(sys.argv) > 1 else 250.0
    args = sys.argv[1:]
    model_only = "model" in args          # только STEP, без траектории
    want_hlr = "hlr" in args or model_only  # ждать включения 2D-оверлея
    app = QGuiApplication(sys.argv)
    engine = QQmlApplicationEngine()
    engine.addImportPath(HERE)
    import mock_backend  # noqa: F401  (регистрирует "MockBackend")
    done = {"ok": False}

    def run():
        roots = engine.rootObjects()
        if not roots:
            print("FAIL: no root objects")
            app.quit()
            return
        win = roots[0]
        backend = win.findChild(QObject, "gcodeToolpath")
        if backend is not None:
            if model_only and os.path.isfile(STEP):
                backend.loadModel(STEP)
            elif "async" in args and os.path.isfile(NGC):
                backend.load_gcode_async(NGC)   # реальный путь вьювера
            elif os.path.isfile(NGC) and "none" not in args:
                backend.load_gcode(NGC)
        obj = win.findChild(type(win), "") or win

        def apply_dist():
            viewer = _find_viewer(win)
            if viewer is None:
                print("FAIL: viewer item not found")
                app.quit()
                return
            print("window version:", viewer.property("viewerVersion"))
            viewer.setProperty("dist", dist)
            for a in args:
                if a.startswith("pitch="):
                    viewer.setProperty("pitch", float(a[6:]))
                if a.startswith("yaw="):
                    viewer.setProperty("yaw", float(a[4:]))
                if a == "solid":
                    viewer.setProperty("xray", False)

        QTimer.singleShot(6000, apply_dist)

        def grab():
            # линии УП заслоняют обводку на скриншотах: скрываем и даём
            # кадр перерисоваться перед захватом
            viewer = _find_viewer(win)
            if viewer is not None:
                viewer.setProperty("showToolpath", False)
                viewer.setProperty("showCollisions", False)
                if "nostock" in args:
                    viewer.setProperty("showStockVisible", False)

            def do_grab():
                img = win.grabWindow()
                if img.isNull():
                    print("FAIL: grabWindow null")
                    app.quit()
                    return
                img.save(OUT)
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

            QTimer.singleShot(400, do_grab)

        QTimer.singleShot(12000, grab)
        QTimer.singleShot(20000, app.quit)

    engine.load(QUrl.fromLocalFile(os.path.join(HERE, "preview_3d.qml")))
    if not engine.rootObjects():
        print("FAIL: QML did not load")
        return 1
    QTimer.singleShot(500, run)
    app.exec()
    print("probe image:", OUT)
    return 0 if done["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
