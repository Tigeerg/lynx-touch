#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Рендер-проверка: толщина ленты контура не зависит от направления линии.

Материал контура (stepledges.vert/.frag) рисует отрезок лентой заданной
толщины lineWidthPx. Проверка строит сцену из одной вертикальной и одной
горизонтальной линии, рендерит её offscreen в окне 800x400 (неквадратный
вьюпорт: на нём расхождение осей NDC максимально) и измеряет толщину обеих
линий по яркости поперечного сечения.

Заявленное поведение: обе линии толщиной lineWidthPx (2.0 px) с допуском
0.4 px. Выход 0 - проверка пройдена.

Запуск:  python modules/gcode-viewer/test_ribbon_width.py

Qt Quick 3D на Windows в режиме offscreen отдаёт чёрный кадр, поэтому тест
открывает обычное окно на ~2 с (живой дисплей обязателен).
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import numpy as np  # noqa: E402
import shiboken6  # noqa: E402
from PySide6.QtCore import QObject, QTimer, QUrl  # noqa: E402
from PySide6.QtGui import QGuiApplication  # noqa: E402
from PySide6.QtQml import QQmlApplicationEngine  # noqa: E402
from PySide6.QtQuick import QQuickWindow  # noqa: E402

import mock_backend  # noqa: E402

WIDTH_PX = 2.0
TOLERANCE_PX = 0.4
VIEW_W, VIEW_H = 800, 400

# viewportW передаётся материалу так же, как viewportH: из размеров View3D.
# Материал без свойства viewportW (старый шейдер) тоже собирается: лишний
# uniform в QML шейдером просто не читается.
QML = """
import QtQuick
import QtQuick3D

Window {
    id: win
    width: %(w)d
    height: %(h)d
    visible: true
    color: "white"

    View3D {
        id: view
        anchors.fill: parent
        environment: SceneEnvironment {
            clearColor: "white"
            backgroundMode: SceneEnvironment.Color
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
        }
        PerspectiveCamera { position: Qt.vector3d(0, 0, 300); clipNear: 1; clipFar: 2000 }
        Model {
            objectName: "lineModel"
            materials: CustomMaterial {
                shadingMode: CustomMaterial.Unshaded
                property real lineWidthPx: %(lw)s
                property real viewportW: view.width
                property real viewportH: view.height
                property color edgeColor: "#000000"
                property real polyOffset: 0.00003
                cullMode: Material.NoCulling
                vertexShader: "stepledges.vert"
                fragmentShader: "stepledges.frag"
            }
        }
    }
}
"""


def _section_width(img, axis, at):
    """Толщина линии в пикселях: сумма «почернения» (фон белый, линия
    тёмная) поперечного сечения / максимум почернения."""
    gray = 255.0 - img[:, :, 0].astype(float)
    full = gray.max()
    if full <= 0:
        return 0.0
    cut = gray[at, :] if axis == "row" else gray[:, at]
    return float(cut.sum() / full)


def main() -> int:
    app = QGuiApplication(sys.argv)
    geom = mock_backend.HLRLineGeometry()
    # вертикальная линия слева от центра, горизонтальная - выше центра; обе
    # короче видимой области, чтобы концы не выходили из кадра
    # 62.13 мм * 1.6095 px/мм (fov 45, dist 300, h=400) = ровно 100 px:
    # линии попадают на центры пикселей, иначе AA размазывает покрытие
    # и сечение мерится на 0.5 px тоньше номинала
    segs = np.array([[[-62.13, -60.0, 0.0], [-62.13, 60.0, 0.0]],
                     [[-200.0, 62.13, 0.0], [200.0, 62.13, 0.0]]],
                    dtype=np.float32)
    geom.set_segments(segs)

    engine = QQmlApplicationEngine()
    engine.addImportPath(HERE)
    warnings = []
    engine.warnings.connect(lambda ws: warnings.extend(str(w) for w in ws))
    engine.loadData(
        (QML % {"w": VIEW_W, "h": VIEW_H, "lw": WIDTH_PX}).encode("utf-8"),
        QUrl.fromLocalFile(os.path.join(HERE, "ribbon_probe.qml")))
    roots = engine.rootObjects()
    if not roots:
        print("FAIL: QML не загрузился:", warnings)
        return 1
    win = shiboken6.wrapInstance(shiboken6.getCppPointer(roots[0])[0], QQuickWindow)
    # геометрию вешаем после старта движка (как в вьювере): привязка
    # Python-геометрии через context property до loadData подвешивала
    # весь тест на мёртвой защёлке в PySide6
    node = win.findChild(QObject, "lineModel")
    if node is None:
        print("FAIL: lineModel не найден")
        return 1
    node.setGeometry(geom)
    result = {}

    def grab():
        img = win.grabWindow()
        if img.isNull():
            result["err"] = "grabWindow вернул пустой кадр"
            app.quit()
            return
        rgba = img.convertToFormat(img.Format.Format_RGBA8888)
        w, h = rgba.width(), rgba.height()
        arr = np.frombuffer(bytes(rgba.constBits()), dtype=np.uint8)[:h * rgba.bytesPerLine()]
        result["img"] = arr.reshape(h, rgba.bytesPerLine() // 4, 4)[:, :w, :]
        app.quit()

    QTimer.singleShot(1500, grab)
    QTimer.singleShot(20000, app.quit)
    app.exec()

    if "img" not in result:
        print("FAIL:", result.get("err", "кадр не получен"), warnings)
        return 1
    img = result["img"]
    h, w = img.shape[:2]
    lit = img[:, :, 0] > 40
    if not lit.any():
        print("FAIL: на кадре нет линий (материал не собрался?)", warnings)
        return 1
    # поперечные сечения: вертикальная линия - строка в середине её длины,
    # горизонтальная - столбец в середине её длины
    cols = np.nonzero(lit.sum(axis=0) > h * 0.25)[0]
    rows = np.nonzero(lit.sum(axis=1) > w * 0.25)[0]
    if cols.size == 0 or rows.size == 0:
        print("FAIL: не найдены вертикальная/горизонтальная линии",
              "cols", cols.size, "rows", rows.size)
        return 1
    vert_w = _section_width(img, "row", h // 2)
    hor_w = _section_width(img, "col", w // 2)
    print(f"вертикальная линия: {vert_w:.2f} px, горизонтальная: {hor_w:.2f} px "
          f"(задано {WIDTH_PX})")
    fails = []
    for name, val in (("вертикальная", vert_w), ("горизонтальная", hor_w)):
        if abs(val - WIDTH_PX) > TOLERANCE_PX:
            fails.append(f"{name} линия {val:.2f} px, ожидалось {WIDTH_PX} +- {TOLERANCE_PX}")
    if fails:
        for f in fails:
            print("FAIL:", f)
        return 1
    print("PASSED: толщина ленты не зависит от направления")
    return 0


if __name__ == "__main__":
    sys.exit(main())

