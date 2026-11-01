import QtQuick

// The Work screen hosts the real viewer module (modules/gcode-viewer/GcodeViewer3D.qml), not a
// second simplified scene. The module keeps its own controller, stock/part/tool and camera; the
// panel gives it the program text and the space.
//
// The embedded copy runs without the viewer's own bottom toolbar and hotkey hints
// (`showChrome: false`): on the machine, view settings belong to the panel service menu and
// playback is driven by the panel actuators (cycle start / feed hold / reset). The debug
// gcodeviewer.exe still shows the full chrome, and set `showChrome: true` here when debugging
// the viewer inside the panel.
Item {
    id: pane
    objectName: "viewerPane"
    property Item host
    property bool showChrome: false
    readonly property bool playing: viewerLoader.item ? viewerLoader.item.playing : false

    // Panel actuators. The panel calls these from machineCycleStart/machineFeedHold/machineReset;
    // the viewer has no buttons of its own in the embedded copy.
    function cycleStart() { if (viewerLoader.item) viewerLoader.item.play() }
    function feedHold() { if (viewerLoader.item) viewerLoader.item.pause() }
    function reset() { if (viewerLoader.item) viewerLoader.item.resetPath() }

    Loader {
        id: viewerLoader
        anchors.fill: parent
        source: "../../modules/gcode-viewer/GcodeViewer3D.qml"
        onLoaded: {
            item.showChrome = pane.showChrome
            item.gcode = host ? host.viewSample : ""
        }
    }

    Binding {
        target: viewerLoader.item
        property: "showChrome"
        value: pane.showChrome
        when: viewerLoader.item !== null
    }
}
