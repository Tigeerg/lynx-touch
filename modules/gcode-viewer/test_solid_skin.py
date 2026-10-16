#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Рендер-проверка сплошной кожи: скрытые рёбра не должны просвечивать.

Сплошной режим (X выключен) обязан писать глубину, иначе кожа не закрывает
скрытые рёбра и они рисуются поверх тела («просвечивающие грани серым»).
Проверка снимает кадр вьювера на реальном УП с включёнными и выключенными
слоями обводки и считает пиксели, где линии лежат поверх кожи. Сплошная кожа
их закрывает: замер сплошного режима с силуэтом по мешу - ~9.2k таких пикселей,
прежней Blend-кожи с opacity 1.0 - ~13.6k. Порог 10.5k разводит два состояния.
Порог поднят с 9.5k под меш-силуэт: горизонт поверхности добавляет линии, но
порядок величины тот же (сплошной режим по-прежнему закрывает скрытое).
Грабится только после готовности модели (`loading` false), иначе появление
кожи между кадрами «вкл»/«выкл» попадает в разницу как тысячи лишних пикселей.

Заявленное поведение: в сплошном режиме линий поверх кожи <= 10500 пикселей
(и не меньше 2000 - кадр не пустой). Выход 0 - проверка пройдена.

Qt Quick 3D на Windows в offscreen отдаёт чёрный кадр, поэтому тест открывает
обычное окно (живой дисплей обязателен).

Запуск:  python modules/gcode-viewer/test_solid_skin.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import numpy as np  # noqa: E402
from PySide6.QtCore import QObject, QTimer, QUrl  # noqa: E402
from PySide6.QtGui import QGuiApplication  # noqa: E402
from PySide6.QtQml import QQmlApplicationEngine  # noqa: E402

import mock_backend  # noqa: E402,F401
import gv_qtgeom  # noqa: E402

# Каталог QML-модуля CNC/Edges и DLL OCCT - в PATH до создания движка (см.
# preview_3d.py): без сборки helper ничего не меняет.
gv_qtgeom.prepare_dll_path()

NGC = os.path.abspath(os.path.join(HERE, "..", "..", "data", "cad-testdata", "Test_model.ngc"))
MAX_LINE_PX = 10500
MIN_LINE_PX = 2000


def _find_viewer(win):
    stack = [win]
    while stack:
        o = stack.pop()
        for c in o.findChildren(object):
            try:
                if c.property("viewerVersion") is not None:
                    return c
            except Exception:
                pass
            stack.append(c)
    return None


def _frame(win):
    img = win.grabWindow()
    rgba = img.convertToFormat(img.Format.Format_RGBA8888)
    w, h = rgba.width(), rgba.height()
    a = np.frombuffer(bytes(rgba.constBits()), dtype=np.uint8)
    return a.reshape(h, rgba.bytesPerLine() // 4, 4)[:, :w, :3].astype(int)


def main() -> int:
    if not os.path.isfile(NGC):
        print("УП не найдена (пропущено):", NGC)
        return 0
    app = QGuiApplication(sys.argv)
    engine = QQmlApplicationEngine()
    engine.addImportPath(HERE)
    engine.load(QUrl.fromLocalFile(os.path.join(HERE, "preview_3d.qml")))
    roots = engine.rootObjects()
    if not roots:
        print("FAIL: QML не загрузился")
        return 1
    win = roots[0]
    backend = win.findChild(QObject, "gcodeToolpath")
    viewer = _find_viewer(win)
    if viewer is None or backend is None:
        print("FAIL: вьювер/бэкенд не найдены")
        return 1
    backend.load_gcode_async(NGC)
    result = {"line_px": None, "err": None}

    def base(lines):
        for k, v in (("showToolpath", False), ("showCollisions", False),
                     ("showStockVisible", False), ("showPartVisible", True),
                     ("showBrepEdges", lines), ("showTangentLines", lines),
                     ("xray", False), ("pitch", -32.0), ("yaw", 90.0)):
            viewer.setProperty(k, v)

    def start():
        base(True)
        QTimer.singleShot(400, grab_on)

    def grab_on():
        result["on"] = _frame(win)
        base(False)
        QTimer.singleShot(400, grab_off)

    def grab_off():
        off = _frame(win)
        d = np.abs(result["on"] - off).sum(axis=2)
        result["line_px"] = int((d > 25).sum())
        app.quit()

    def wait_ready():
        # ждём готовности модели: иначе кожа появляется между кадрами
        # «вкл»/«выкл» и её пиксели уходят в разницу линий
        if backend.property("loading"):
            QTimer.singleShot(250, wait_ready)
        else:
            QTimer.singleShot(600, start)

    QTimer.singleShot(500, wait_ready)
    QTimer.singleShot(60000, app.quit)
    app.exec()

    if result["line_px"] is None:
        print("FAIL: кадр не получен")
        return 1
    n = result["line_px"]
    print("линий поверх кожи в сплошном режиме:", n, "px (порог", MAX_LINE_PX, ")")
    if n < MIN_LINE_PX:
        print("FAIL: кадр пуст, обводка не отрисована")
        return 1
    if n > MAX_LINE_PX:
        print("FAIL: скрытые рёбра просвечивают через сплошную кожу")
        return 1
    print("PASSED: сплошная кожа закрывает скрытые рёбра")
    return 0


if __name__ == "__main__":
    sys.exit(main())
