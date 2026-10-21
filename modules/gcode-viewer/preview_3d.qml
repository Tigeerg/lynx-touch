import QtQuick
import QtQuick.Window

/* Desktop window wrapper around GcodeViewer3D (Item -> Window). */
Window {
    id: win
    width: 1024
    height: 600
    visible: true
    title: "G-code 3D Viewer — QtQuick3D"

    // Default toolpath (the wrapper is loaded by the C++ host; the NC program is set
    // by the gcode property)
    property string gcode: "G0 Z10\nG0 X0 Y0\nG1 Z-2 F500\nG1 X50 Y0 F1500\nG1 X50 Y50\nG1 X0 Y50\nG1 X0 Y0\nG0 Z10\n"
    property bool loading: false

    GcodeViewer3D {
        anchors.fill: parent
        gcode: win.gcode
        loading: win.loading
    }
}
