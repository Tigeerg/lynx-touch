import QtQuick
import QtTest

// Offscreen checks of the Qt port (wave A): the panel loads at 1024x600 and its public surface is
// alive. The tab and softkey counts become strict in wave B/C, when the port has the 5 tabs and
// 8 slots; today the probe guards the load and the size.
TestCase {
    name: "QtPanel"
    width: 1024
    height: 600

    Loader {
        id: panel
        anchors.fill: parent
        source: "../main.qml"
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
        var keys = panel.item.softkeysFor("pos")

        verify(keys.length >= 6)
        verify(keys.length <= 8)
    }
}
