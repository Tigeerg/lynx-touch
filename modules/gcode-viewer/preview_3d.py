#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Запуск вьювера УП (preview_3d.qml + GcodeViewer3D.qml) - инструмент разработки.

Использование:
    python preview_3d.py [путь/к/файлу.ngc]

Бэкенд вьювера - QML-плагин MockBackend (C++, cpp/qmlbackend): он единственный,
и питоновские модули того же URI здесь не импортируются никогда. Плагина нет -
ошибка с указанием, что собрать, и ненулевой код возврата: отката на Python нет.

Каталог модуля плагина обязан попасть в PATH до создания движка: точка входа
Qt-плагина ищет зависимости по PATH, а не через os.add_dll_directory (разбор -
test_qt_bridge.py, cpp/exp/exp_qml_diag.py).

Проверка без экрана:
    QT_QPA_PLATFORM=offscreen python preview_3d.py data/cad-testdata/Test_model.ngc
"""
import logging
import os
import sys

from PySide6.QtCore import QObject, QUrl
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import gv_cpp  # noqa: E402


def main() -> int:
    # Диагностика вьювера идёт через logging; уровень задаёт окружение
    # (CNC_LOG_LEVEL=DEBUG покажет и источник контура), по умолчанию INFO.
    logging.basicConfig(level=os.environ.get("CNC_LOG_LEVEL", "INFO").upper(),
                        format="%(levelname)s %(name)s: %(message)s")
    try:
        backend_import = gv_cpp.require_qml_backend()
    except gv_cpp.GvCppError as exc:
        print("[preview_3d] FAIL", exc, file=sys.stderr)
        return 2
    gv_cpp.prepare_qml_backend_path()

    app = QGuiApplication(sys.argv)

    engine = QQmlApplicationEngine()
    engine.addImportPath(HERE)
    engine.addImportPath(backend_import)
    engine.warnings.connect(lambda m: print("[QML warning]", m, file=sys.stderr))

    engine.load(QUrl.fromLocalFile(os.path.join(HERE, "preview_3d.qml")))

    roots = engine.rootObjects()
    print("[preview_3d] root objects:", len(roots))
    if not roots:
        print("[preview_3d] FAILED to load QML", file=sys.stderr)
        return 1

    # Необязательно: загрузить УП через бэкенд (модель <тем же именем>.stl/.obj
    # подхватывается сама).
    filepath = sys.argv[1] if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]) else None
    backend = roots[0].findChild(QObject, "gcodeToolpath")

    if os.environ.get("QT_QPA_PLATFORM") == "offscreen":
        if backend is not None and filepath:
            count = backend.load_gcode(filepath)
            print("[preview_3d] loaded g-code from:", filepath, "segments:", count)
            print("[preview_3d] modelPath:", backend.property("modelPath"))
        print("[preview_3d] offscreen mode — loaded OK.")
        return 0

    if backend is not None and filepath:
        win = roots[0]
        win.setProperty("loading", True)     # show the loading overlay

        def on_toolpath(seg):
            print("[preview_3d] loaded g-code from:", filepath, "segments:", seg)

        def on_finished():
            print("[preview_3d] modelPath:", backend.property("modelPath"))
            win.setProperty("loading", False)

        backend.toolpathLoaded.connect(on_toolpath)
        backend.loadFinished.connect(on_finished)
        # Разбор УП, геометрия и модель идут на рабочем потоке: окно не замирает.
        backend.load_gcode_async(filepath)

    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
