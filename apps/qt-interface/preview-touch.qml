import QtQuick
import QtQuick.Window

// Desktop preview of the portrait touch panel: the same main.qml, forced into touch mode.
Window {
    width: 864
    height: 1536
    minimumWidth: 864
    minimumHeight: 1536
    maximumWidth: 864
    maximumHeight: 1536
    visible: true
    title: "Lynx Touch — сенсорная панель (Qt)"

    Loader {
        anchors.fill: parent
        source: "main.qml"
        onLoaded: item.svcValues = ({ "system.9": 2 })
    }
}
