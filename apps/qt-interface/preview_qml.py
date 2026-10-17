#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Desktop preview for QtQuick UI 2.15 (main.qml layout) using PySide6 (Qt 6).

Opens a 1024x600 window loading ./preview.qml (self-contained copy of the
main.qml layout wrapped in a Window). Registers the bundled Lucide icon
font so QML Text with font.family "lucide" renders icons.

Usage:
    python preview_qml.py [path/to/file.qml] [width] [height]

Бэкенд вьювера - C++-плагин MockBackend (см. prepare_backend): питоновской
регистрации этого URI нет, поэтому QML с `import MockBackend 1.0` (main.qml) без
собранного плагина не грузится, и об этом сообщается кодом возврата 2.

Requires:
    pip install PySide6
"""
import os
import sys

from PySide6.QtCore import QTimer, QUrl
from PySide6.QtGui import QFontDatabase
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtWidgets import QApplication

HERE = os.path.dirname(os.path.abspath(__file__))

# Проводка бэкенда вьювера: main.qml импортирует MockBackend 1.0, а питоновской
# регистрации этого URI больше нет - бэкенд только C++ (QML-плагин проекта
# gcode-viewer, modules/gcode-viewer/cpp/qmlbackend). Пути считаются здесь, а не
# импортом из другого проекта: этот файл - инструмент разработки qt-interface.
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
BACKEND_IMPORT = os.path.join(REPO, "build", "qmlbackend")
BACKEND_MODULE = os.path.join(BACKEND_IMPORT, "MockBackend")


def prepare_backend(qml_file: str) -> bool:
    """PATH и путь импорта для C++-плагина MockBackend; False - плагина нет.

    QML без импорта MockBackend (preview.qml) бэкенда не требует - тогда ничего не
    делается. QML с этим импортом (main.qml) без собранного плагина не грузится, и
    об этом сообщается: отката на питоновскую регистрацию нет.

    Каталог модуля идёт в PATH до создания движка: точка входа Qt-плагина ищет
    зависимости по PATH, а не через os.add_dll_directory (разбор - PORT-CPP.md,
    «Ограничения рантайма Qt»).
    """
    with open(qml_file, "r", encoding="utf-8") as handle:
        needs_backend = "MockBackend" in handle.read()
    if not needs_backend:
        return True
    if not os.path.isfile(os.path.join(BACKEND_MODULE, "qmldir")):
        print("[preview] FAIL QML-модуль MockBackend не собран: нет %s\\qmldir; соберите "
              "плагин: powershell -NoProfile -ExecutionPolicy Bypass -File "
              "scripts/build-viewer.ps1" % BACKEND_MODULE, file=sys.stderr)
        return False
    dirs = [BACKEND_MODULE]
    # Умолчание то же, что у сборки (scripts/build-viewer.ps1) и у шима
    # gv_cpp.py: каталог OCCT из окружения conda, иначе известный путь машины
    # разработки. Без DLL OCCT точка входа плагина не грузится.
    occ = os.environ.get("CNC_OCC_DIR")
    if not occ and os.environ.get("CONDA_PREFIX"):
        occ = os.path.join(os.environ["CONDA_PREFIX"], "Library")
    if not occ:
        occ = r"C:\Users\Tiger\anaconda3\envs\gcode-cpp\Library"
    dirs.append(os.path.join(occ, "bin"))
    have = [p.lower() for p in os.environ.get("PATH", "").split(os.pathsep) if p]
    add = [d for d in dirs if os.path.isdir(d) and d.lower() not in have]
    if add:
        os.environ["PATH"] = os.pathsep.join(add + [os.environ.get("PATH", "")])
    return True


def main() -> int:
    qml_file = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "preview.qml")
    width = int(sys.argv[2]) if len(sys.argv) > 2 else 1024
    height = int(sys.argv[3]) if len(sys.argv) > 3 else 600

    if not os.path.isfile(qml_file):
        print(f"QML file not found: {qml_file}", file=sys.stderr)
        return 2

    if not prepare_backend(qml_file):
        return 2

    app = QApplication(sys.argv)

    # Register bundled fonts (Inter, JetBrains Mono, Lucide) so QML Text
    # with those family names renders correctly.
    for fname, label in [
        ("Inter-Regular.ttf", "Inter"),
        ("JetBrainsMono-Regular.ttf", "JetBrains Mono"),
        ("lucide.ttf", "lucide"),
    ]:
        path = os.path.join(HERE, "fonts", fname)
        if os.path.isfile(path):
            fid = QFontDatabase.addApplicationFont(path)
            fams = QFontDatabase.applicationFontFamilies(fid)
            print(f"[preview] Font {label}: {fams if fams else '?'}")
        else:
            print(f"[preview] WARNING: fonts/{fname} not found", file=sys.stderr)

    engine = QQmlApplicationEngine()
    engine.addImportPath(os.path.join(HERE, "qml"))
    engine.addImportPath(HERE)
    engine.addImportPath(BACKEND_IMPORT)      # C++-бэкенд MockBackend (см. prepare_backend)

    def _warn(msg: str) -> None:
        print("[QML warning]", msg, file=sys.stderr)

    engine.warnings.connect(_warn)

    url = QUrl.fromLocalFile(os.path.abspath(qml_file))
    engine.load(url)

    # Optional 4th arg: active screen ("pos", "prog", ...).
    _roots = engine.rootObjects()
    if _roots and len(sys.argv) > 4:
        _roots[0].setProperty("activeScreen", sys.argv[4])
        print(f"[preview] activeScreen = {sys.argv[4]}")
    # Optional 5th arg: machine mode ("AUTO", "MDI", "JOG", "MPG").
    if _roots and len(sys.argv) > 5:
        _roots[0].setProperty("machineMode", sys.argv[5])
        print(f"[preview] machineMode = {sys.argv[5]}")

    # In headless/offscreen environments there is no real display, so a
    # visible window cannot appear. Just confirm the engine loaded the QML
    # and exit cleanly (useful for CI / smoke checks).
    if os.environ.get("QT_QPA_PLATFORM") == "offscreen":
        roots = engine.rootObjects()
        print(f"[preview] offscreen mode — loaded {len(roots)} root object(s).")
        return 0 if roots else 1

    def _show() -> None:
        roots = engine.rootObjects()
        if not roots:
            QTimer.singleShot(50, _show)
            return
        win = roots[0]
        try:
            win.resize(width, height)
            win.show()
            win.raise_()
            win.requestActivate()
        except Exception as exc:  # noqa: BLE001
            print(f"Could not show window: {exc}", file=sys.stderr)

    QTimer.singleShot(200, _show)

    rc = app.exec()
    print("Preview window closed. Exiting cleanly.")
    return rc


if __name__ == "__main__":
    sys.exit(main())