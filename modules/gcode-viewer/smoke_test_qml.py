#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Дым-прогон: загрузить QML вьювера (preview_3d.qml) без экрана и доложить разбор
и предупреждения. Инструмент разработки.

Целевой файл по умолчанию - обёртка вьювера, не main.qml: main.qml принадлежит
qt-interface и в модуле MockBackend типа `Toolpath` не имеет (класс называется
GcodeToolpath) - дым-прогон вьювера не может зависеть от чужого проекта.

Бэкенд - QML-плагин MockBackend (C++, cpp/qmlbackend), и он единственный:
питоновский бэкенд здесь не импортируется, плагина нет - ошибка и код возврата 2.
"""
import os
import sys

from PySide6.QtCore import QTimer, QUrl
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtWidgets import QApplication

HERE = os.path.dirname(os.path.abspath(__file__))
QML = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "preview_3d.qml")

sys.path.insert(0, HERE)

import gv_cpp  # noqa: E402


def main() -> int:
    try:
        backend_import = gv_cpp.require_qml_backend()
    except gv_cpp.GvCppError as exc:
        print("FAIL:", exc, file=sys.stderr)
        return 2
    # Каталог модуля плагина в PATH до создания движка (см. preview_3d.py).
    gv_cpp.prepare_qml_backend_path()

    app = QApplication(sys.argv)
    engine = QQmlApplicationEngine()
    engine.addImportPath(os.path.join(HERE, "qml"))
    engine.addImportPath(backend_import)

    seen = []

    def _warn(msg: str) -> None:
        seen.append(msg)

    engine.warnings.connect(_warn)

    engine.load(QUrl.fromLocalFile(os.path.abspath(QML)))

    # Give the engine a moment to finish loading before we inspect roots.
    QTimer.singleShot(300, app.quit)
    app.exec()

    roots = engine.rootObjects()
    print(f"Root object count: {len(roots)}")
    if roots:
        root = roots[0]
        print(f"Root type: {type(root).__name__}")
        try:
            print(f"Root size: {root.width()}x{root.height()}")
        except Exception as e:  # noqa: BLE001
            print(f"(no width/height readable: {e})")
    for w in seen:
        print("[QML warning]", w)
    if not roots:
        print("RESULT: FAILED")
        return 1
    print("RESULT: OK (parse-level)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
