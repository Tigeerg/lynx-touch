import QtQuick

// The Work screen hosts the real viewer module (modules/gcode-viewer/GcodeViewer3D.qml), not a
// second simplified scene. The module keeps its own controller, stock/part/tool, camera and
// controls; the panel only gives it the program text and the space.
Item {
    id: pane
    objectName: "viewerPane"
    property Item host

    Loader {
        id: viewerLoader
        anchors.fill: parent
        source: "../../modules/gcode-viewer/GcodeViewer3D.qml"
        onLoaded: if (item) item.gcode = host ? host.viewSample : ""
    }
}
