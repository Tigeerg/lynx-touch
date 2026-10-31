import QtQuick
import QtQuick.Window

/**
 * DESKTOP PREVIEW of the CNC Operator Panel: a window around main.qml, not a copy of it.
 *
 * main.qml is the single source of the panel. The preview only gives the Item root a visible window
 * for `qmlscene` and for the standalone `lynx-panel` executable. The import paths of MockBackend and
 * CNC.Edges are supplied by the launcher (scripts/run-qt-interface.ps1 or main.cpp).
 */
Window {
    id: root
    width: 1024
    height: 600
    visible: true
    title: "Lynx Touch — панель оператора (Qt-порт)"

    Loader {
        anchors.fill: parent
        source: "main.qml"
    }
}
