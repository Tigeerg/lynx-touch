import QtQuick
import QtTest

// Offscreen checks of the Qt port: the panel loads at 1024x600, the public surface is alive,
// and wave B keeps the Work screen in components of its own (coordinate strip, toolpath
// preview, status column) instead of growing another axis table inside main.qml.
TestCase {
    name: "QtPanel"
    width: 1024
    height: 600

    Loader {
        id: panel
        anchors.fill: parent
        source: "../main.qml"
    }

    function findByName(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        var kids = item.children
        for (var i = 0; i < kids.length; i++) {
            var hit = findByName(kids[i], name)
            if (hit) return hit
        }
        return null
    }

    function test_panel_loads() {
        if (panel.status !== Loader.Ready) {
            var component = Qt.createComponent("../main.qml")
            console.log("panel component status=" + component.status + " error=" + component.errorString())
        }
        tryCompare(panel, "status", Loader.Ready)
        verify(panel.item !== null)
        compare(panel.item.width, 1024)
        compare(panel.item.height, 600)
        verify(panel.item.activeScreen.length > 0)
    }

    function test_softkeys_are_slots() {
        var keys = panel.item.padSoft(panel.item.softkeysFor("pos"))

        compare(keys.length, 8)
    }

    function test_work_screen_parts() {
        tryCompare(panel, "status", Loader.Ready)
        verify(findByName(panel.item, "workScreen") !== null)
        verify(findByName(panel.item, "workCoordStrip") !== null)
        verify(findByName(panel.item, "workPreview") !== null)
        verify(findByName(panel.item, "workStatusColumn") !== null)
    }
}
