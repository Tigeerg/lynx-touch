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
    property string programText: host ? host.programText() : ""
    onProgramTextChanged: if (viewerLoader.item) viewerLoader.item.gcode = programText
    // Viewer settings from the panel service menu (saved values). An empty object keeps the
    // viewer defaults; the debug gcodeviewer.exe does not use this property.
    property var settings: ({})
    readonly property bool playing: viewerLoader.item ? viewerLoader.item.playing : false

    function applySettings() {
        var it = viewerLoader.item
        if (!it || !settings) return
        if (settings.quality !== undefined) it.quality = settings.quality
        if (settings.stockVisible !== undefined) it.showStockVisible = settings.stockVisible
        if (settings.stockView !== undefined) it.stockView = settings.stockView
        if (settings.partMode !== undefined) it.partMode = settings.partMode
        if (settings.showPartSkin !== undefined) it.showPartSkin = settings.showPartSkin
        if (settings.showToolpath !== undefined) it.showToolpath = settings.showToolpath
        if (settings.stockPlainBox !== undefined) it.stockPlainBox = settings.stockPlainBox
        if (settings.stockGhost !== undefined) it.stockGhost = settings.stockGhost
        if (settings.showBrepEdges !== undefined) it.showBrepEdges = settings.showBrepEdges
        if (settings.showTangentLines !== undefined) it.showTangentLines = settings.showTangentLines
        if (settings.showFps !== undefined) it.showFps = settings.showFps
    }
    onSettingsChanged: applySettings()

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
            pane.applySettings()
            item.gcode = pane.programText
        }
    }

    Binding {
        target: viewerLoader.item
        property: "showChrome"
        value: pane.showChrome
        when: viewerLoader.item !== null
    }

    Binding {
        target: viewerLoader.item
        property: "gcode"
        value: pane.programText
        when: viewerLoader.item !== null
    }
}
