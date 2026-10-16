#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Launch the QtQuick3D G-code viewer (preview_3d.qml + GcodeViewer3D.qml).

Usage:
    python preview_3d.py [path/to/file.ngc]

Registers the MockBackend QML module from mock_backend.py, loads the window,
optionally overrides the toolpath from a .ngc file and shows it.

Headless check:
    QT_QPA_PLATFORM=offscreen python preview_3d.py
"""
import logging
import os
import sys

from PySide6.QtCore import QObject, QUrl
from PySide6.QtGui import QGuiApplication
from PySide6.QtQml import QQmlApplicationEngine

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import mock_backend  # noqa: E402,F401  (registers "MockBackend" via @QmlElement)
import gv_qtgeom  # noqa: E402

# Каталог QML-модуля CNC/Edges и DLL OCCT - в PATH до создания движка: точка
# входа Qt-плагина ищет свои зависимости по PATH, а не через add_dll_directory
# (разбор - test_qt_bridge.py, cpp/exp/exp_qml_diag.py). Каталогов сборки нет -
# PATH не трогаем: на машине без тулчейна поведение прежнее.
gv_qtgeom.prepare_dll_path()


def main() -> int:
    # Диагностика вьювера идёт через logging; уровень задаёт окружение
    # (CNC_LOG_LEVEL=DEBUG покажет и источник контура), по умолчанию INFO.
    logging.basicConfig(level=os.environ.get("CNC_LOG_LEVEL", "INFO").upper(),
                        format="%(levelname)s %(name)s: %(message)s")
    app = QGuiApplication(sys.argv)

    engine = QQmlApplicationEngine()
    engine.addImportPath(HERE)
    engine.warnings.connect(lambda m: print("[QML warning]", m, file=sys.stderr))

    engine.load(QUrl.fromLocalFile(os.path.join(HERE, "preview_3d.qml")))

    roots = engine.rootObjects()
    print("[preview_3d] root objects:", len(roots))
    if not roots:
        print("[preview_3d] FAILED to load QML", file=sys.stderr)
        return 1

    # Optional: load a G-code file through the backend (also auto-loads <same name>.stl/.obj).
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
        # Parse + geometry + STL run on a worker thread; the UI stays responsive.
        backend.load_gcode_async(filepath)

    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
