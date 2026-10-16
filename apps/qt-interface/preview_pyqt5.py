#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Preview QtQuick-like QML on Windows using PyQt5 (Qt 5.15).

NOTE: PyQt5 wheels exist only for Python <= 3.11 on Windows.
If you run a newer Python, install Python 3.11 (e.g. via winget/choco)
and then:  pip install PyQt5

Usage:
    python preview_pyqt5.py [path/to/qml] [width] [height]
"""
import os
import sys

from PyQt5.QtCore import QUrl
from PyQt5.QtQml import QQmlApplicationEngine
from PyQt5.QtWidgets import QApplication

HERE = os.path.dirname(os.path.abspath(__file__))


def main() -> int:
    qml_file = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "main.qml")
    width = int(sys.argv[2]) if len(sys.argv) > 2 else 1024
    height = int(sys.argv[3]) if len(sys.argv) > 3 else 600

    if not os.path.isfile(qml_file):
        print(f"QML file not found: {qml_file}", file=sys.stderr)
        return 2

    app = QApplication(sys.argv)
    engine = QQmlApplicationEngine()
    engine.addImportPath(os.path.join(HERE, "qml"))

    url = QUrl.fromLocalFile(os.path.abspath(qml_file))
    engine.load(url)

    roots = engine.rootObjects()
    if not roots:
        print("Failed to load QML — no root objects.", file=sys.stderr)
        return 1

    window = roots[0]
    window.setWindowTitle("CNC Operator Panel — VMC-850  (PyQt5 preview)")
    window.resize(width, height)
    window.show()
    return app.exec_()


if __name__ == "__main__":
    sys.exit(main())