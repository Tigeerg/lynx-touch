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
        // Every screen returns the full bar; empty slots are null (React SOFTKEY_SLOTS).
        var screens = panel.item.screenIds
        for (var i = 0; i < screens.length; i++) {
            compare(panel.item.softkeysFor(screens[i]).length, 8)
        }
    }

    function test_workplace_sets() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item

        p.setWorkplace("SETUP")
        var setup = p.softkeysFor("pos")
        p.setWorkplace("TEST")
        var test = p.softkeysFor("prog")
        p.setWorkplace("RUN")
        var run = p.softkeysFor("prog")

        // The workplace chooses the set before the screen (React getSoftkeys): SETUP is the
        // preparation set, PROGON the test set, AUTO the machining set.
        compare(setup.length, 8)
        compare(test.length, 8)
        compare(run.length, 8)
        compare(setup[0].label, "Обмер инструмента")
        compare(test[0].label, "Каталог УП")
        compare(run[0].label, "Каталог УП")
        verify(setup[0].label !== test[0].label)
        // PROGON and AUTO share F1 but differ at F4: the run mode against a reserved slot.
        verify(test[3].label !== run[3].label)

        // A disabled key carries its reason and says it in the message line, never silently.
        verify(setup[3].disabled === true)
        verify(setup[3].reason.length > 0)
        p.message = ""
        p.pressSoftkey(setup[3])
        verify(p.message.length > 0)

        p.setWorkplace("RUN")
    }

    function test_work_screen_parts() {
        tryCompare(panel, "status", Loader.Ready)
        verify(findByName(panel.item, "workScreen") !== null)
        verify(findByName(panel.item, "workCoordStrip") !== null)
        verify(findByName(panel.item, "workPreview") !== null)
        verify(findByName(panel.item, "workStatusColumn") !== null)
    }

    function test_debug_windows_exist() {
        var pendant = Qt.createComponent("../PendantWindow.qml")
        var mpg = Qt.createComponent("../MpgWindow.qml")
        compare(pendant.status, Component.Ready)
        compare(mpg.status, Component.Ready)
        compare(panel.item.pendantOpen, false)
        compare(panel.item.mpgOpen, false)
        panel.item.pendantOpen = true
        panel.item.mpgOpen = true
        compare(panel.item.pendantOpen, true)
        compare(panel.item.mpgOpen, true)
    }
}
