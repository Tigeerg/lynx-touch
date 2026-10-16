#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Headless smoke-test: load the viewer QML (preview_3d.qml) off-screen and
report parse/warnings. Целевой файл по умолчанию - обёртка вьювера, не
main.qml: main.qml принадлежит qt-interface и в модуле MockBackend типа
`Toolpath` не имеет (класс называется GcodeToolpath) - дым-прогон вьювера
не может зависеть от чужого проекта.
"""
import os
import sys

from PySide6.QtCore import QTimer, QUrl
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtWidgets import QApplication

HERE = os.path.dirname(os.path.abspath(__file__))
QML = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "preview_3d.qml")

sys.path.insert(0, HERE)
# Модуль MockBackend регистрируется импортом mock_backend - тот же путь,
# что в preview_3d.py. Без него QML не резолвит типы (@QmlElement) и
# дым-тест падал на 'module "MockBackend" is not installed'. При
# CNC_CPP=controller тот же URI приходит из C++-плагина (cpp/qmlbackend), и
# питоновская регистрация тогда не делается вовсе: движок отказывается от двух
# регистраций одного имени (см. preview_3d.py).
import gv_cpp  # noqa: E402

CONTROLLER_CPP = gv_cpp.controller_enabled()
if CONTROLLER_CPP:
    gv_cpp.prepare_qml_backend_path()
else:
    import gv_qtgeom  # noqa: E402

    # Каталог QML-модуля CNC/Edges и DLL OCCT - в PATH до создания движка (см.
    # preview_3d.py): без сборки helper ничего не меняет.
    gv_qtgeom.prepare_dll_path()
    import mock_backend  # noqa: E402,F401


def main() -> int:
    app = QApplication(sys.argv)
    engine = QQmlApplicationEngine()
    engine.addImportPath(os.path.join(HERE, "qml"))
    if CONTROLLER_CPP:
        engine.addImportPath(gv_cpp.qml_backend_import_path())

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