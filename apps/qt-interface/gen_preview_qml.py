#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Regenerate preview.qml from main.qml (wrap root Item into Window).

Keeps `id: root` intact so bindings like root.activeScreen keep working,
and only switches the root element type Item -> Window.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def main() -> None:
    src_path = os.path.join(HERE, "main.qml")
    out_path = os.path.join(HERE, "preview.qml")
    with open(src_path, "r", encoding="utf-8") as f:
        src = f.read()

    # 1. Drop the leading block comment of main.qml
    body = re.sub(r"(?s)^/\*\*.*?\*/", "", src, count=1)

    # 2. Drop the "import QtQuick ..." lines from body (we prepend our own)
    body = re.sub(r"(?m)^import QtQuick[^\n]*\n", "", body)

    # 3. Replace root "Item {" with "Window {"
    if not re.search(r"(?m)^Item \{", body):
        print("ERROR: no 'Item {' root found", file=sys.stderr)
        sys.exit(1)
    body = re.sub(r"(?m)^Item \{", "Window {", body, count=1)

    # 4. Keep `id: root`; add window title right after `height: 600` (root).
    body = re.sub(
        r"(?m)^(\s*)height: 600(\s*)\n",
        '\\1height: 600\\2\n\\1title: "CNC Operator Panel — VMC-850 (QtQuick preview)"\n',
        body,
        count=1,
    )

    header = (
        "import QtQuick 2.15\n"
        "import QtQuick.Controls 2.15\n"
        "import QtQuick.Layouts 1.15\n\n"
        "/**\n"
        " * DESKTOP PREVIEW of the CNC Operator Panel (auto-generated from main.qml).\n"
        " * Wraps the Item skeleton into a Window so PySide6/PyQt5 can display it.\n"
        " * Keep main.qml as the canonical source; regenerate via gen_preview_qml.py.\n"
        " */\n"
    )

    with open(out_path, "w", encoding="utf-8") as f:
        f.write(header + body.lstrip("\n"))

    print(f"Regenerated {out_path}")


if __name__ == "__main__":
    main()