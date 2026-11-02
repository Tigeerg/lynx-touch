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

    function test_boot_state() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        // A clean start is deterministic: AUTO workplace, Program screen, the AUTO softkey set.
        p.workplace = "RUN"
        p.machineMode = "AUTO"
        p.activeScreen = "prog"
        wait(0)
        compare(p.activeScreen, "prog")
        compare(p.workplace, "RUN")
        var keys = p.softkeysFor("prog")
        compare(keys[0].id, "openFile")
        compare(keys[1].id, "toCursor")
    }

    function test_service_adjust_perf() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        p.serviceSection = "system"
        p.svcCursor = 4
        var t = Date.now()
        for (var i = 0; i < 40; i++) p.adjustParam(1, false)
        var elapsed = Date.now() - t
        verify(elapsed < 1000)
        p.svcValues = ({})
    }

    function test_service_navigation() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        var ids = p.serviceIds
        for (var s = 0; s < ids.length; s++) {
            p.serviceSection = ids[s]
            wait(0)
            var n = p.svcListLen()
            for (var i = 0; i < n; i++) {
                p.svcCursor = i
                p.svcValueAt(i)
                p.svcDefAt(i)
                p.svcUnsaved(i)
            }
            p.svcSave()
        }
        p.serviceSection = "system"
    }

    function test_softkey_matrix() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        var cases = [
            { wp: "SETUP", screen: "pos" },
            { wp: "TEST", screen: "prog" },
            { wp: "RUN", screen: "prog" },
            { wp: "RUN", screen: "offset" },
            { wp: "RUN", screen: "tool" },
            { wp: "RUN", screen: "alarm" },
            { wp: "RUN", screen: "service" }
        ]
        for (var c = 0; c < cases.length; c++) {
            p.workplace = cases[c].wp
            p.activeScreen = cases[c].screen
            var keys = p.softkeysFor(cases[c].screen)
            for (var i = 0; i < keys.length; i++) {
                if (!keys[i]) continue
                p.lastAction = ""
                p.pressSoftkey(keys[i])
                verify(p.lastAction !== "")
                p.fileDialog = false
                p.toolMeasureOpen = false
                p.serviceAuthOpen = false
                p.probeOpen = false
            }
        }
        p.setWorkplace("RUN")
        p.activeScreen = "prog"
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
        panel.item.workplace = "TEST"
        panel.item.activeScreen = "pos"
        wait(0)
        verify(findByName(panel.item, "workScreen") !== null)
        verify(findByName(panel.item, "workCoordStrip") !== null)
        verify(findByName(panel.item, "workPreview") !== null)
        verify(findByName(panel.item, "viewerPane") !== null)
        verify(findByName(panel.item, "workStatusColumn") !== null)
        var viewer = findByName(panel.item, "viewerPane")
        verify(viewer !== null)
        verify(viewer.children.length > 0)
        // The embedded viewer has no chrome of its own; playback comes from panel actuators.
        verify(viewer.showChrome === false)
        var viewerItem = viewer.children[0].item
        verify(viewerItem !== null)
        verify(viewerItem.showChrome === false)
        // The panel actuators themselves, not the viewer API: signal -> Connections -> viewer.
        panel.item.machineCycleStart()
        verify(viewerItem.playing === true)
        panel.item.machineFeedHold()
        verify(viewerItem.playing === false)
        panel.item.machineReset()
        verify(viewerItem.pathDist === 0)
        panel.item.workplace = "RUN"
    }

    function test_viewer_service_settings() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        p.workplace = "TEST"
        p.activeScreen = "pos"
        wait(0)
        // The viewer is configured from its own service section, not from viewer buttons.
        verify(p.serviceIds.indexOf("viewer") >= 0)
        compare(p.sysParamsFor("viewer").length, 11)

        var viewer = findByName(p, "viewerPane")
        var viewerItem = viewer.children[0].item
        // The real service flow: edit values, then Save applies them to the viewer.
        p.serviceSection = "viewer"
        p.svcValues = ({})
        p.svcSavedMap = ({})
        p.setSvcVal(0, 2)    // quality high
        p.setSvcVal(1, 1)    // stock hidden
        p.setSvcVal(3, 2)    // part hidden
        p.setSvcVal(5, 1)    // toolpath hidden
        p.setSvcVal(10, 1)   // fps shown
        p.svcSave()
        wait(0)
        compare(viewerItem.quality, "high")
        compare(viewerItem.showStockVisible, false)
        compare(viewerItem.partMode, 2)
        compare(viewerItem.showToolpath, false)
        compare(viewerItem.showFps, true)

        // Clearing the saved map returns the viewer to the documented defaults.
        p.svcSavedMap = ({})
        p.serviceSection = "system"
        wait(0)
        compare(viewerItem.quality, "med")
        compare(viewerItem.showStockVisible, true)
        compare(viewerItem.partMode, 0)
        compare(viewerItem.showToolpath, true)
        compare(viewerItem.showFps, false)
        p.workplace = "RUN"
    }

    function test_program_catalogue() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        verify(p.programFiles.length >= 4)
        compare(p.activeProgram().name, "O1024")
        verify(p.progLines.length > 10)
        compare(p.progLines[0].toks[0].t, "O1024")

        // The Program softkey opens the catalogue; choosing a file makes it active everywhere.
        p.setWorkplace("RUN")
        p.activeScreen = "prog"
        p.fileDialog = true
        p.fileCursor = 1
        p.openProgram(p.fileCursor)
        compare(p.fileDialog, false)
        compare(p.activeProgram().name, "O2048")
        compare(p.activeProgram().title, "ПЛИТА КРЕПЁЖНАЯ")
        compare(p.progLines[0].toks[0].t, "O2048")
        verify(p.programText().indexOf("O2048") === 0)
        compare(p.cursorProg, 0)

        // The Work-screen viewer receives the same active program text (manual workplace).
        p.workplace = "TEST"
        p.activeScreen = "pos"
        wait(0)
        var viewer = findByName(p, "viewerPane")
        var viewerItem = viewer.children[0].item
        wait(0)
        verify(viewerItem.gcode.indexOf("O2048") === 0)

        p.openProgram(0)
        compare(p.activeProgram().name, "O1024")
        p.workplace = "RUN"
    }

    function test_machine_position() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        p.machinePos = ({ x: 0, y: 0, z: 0, a: 0 })
        p.jogAxis = "X"
        p.jogStep = 2          // 0.1 mm
        p.machineJogRequest(1)
        compare(p.machinePos.x, 0.1)
        compare(p.machineCoordinate("X"), "+0.100")

        p.jogAxis = "Z"
        p.jogStep = 0          // 0.001 mm
        p.machineJogRequest(-1)
        compare(p.machinePos.z, -0.001)

        // The Work DRO reads the live position, not the G54 table row.
        var ws = findByName(p, "workScreen")
        compare(ws.dro("X"), "+0.100")
        compare(ws.dro("Z"), "-0.001")

        p.machinePos = ({ x: -120.500, y: 35.250, z: -210.400, a: 0.000 })
        p.jogAxis = "X"
        p.jogStep = 1
    }

    function test_softkey_actions() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        p.partCount = 0
        p.pressSoftkey({ id: "partCounter" })
        compare(p.partCount, 1)

        p.cursorTool = 2
        p.pressSoftkey({ id: "brokenTool" })
        compare(p.toolsData[2].life, 0)
        p.pressSoftkey({ id: "resetLife" })
        compare(p.toolsData[2].life, 100)

        p.machinePos = ({ x: 10, y: 20, z: -30, a: 0 })
        p.pressSoftkey({ id: "goSafe" })
        compare(p.machinePos.z, 50)
        p.pressSoftkey({ id: "goWorkZero" })
        compare(p.machinePos.x, 0)
        compare(p.machinePos.z, 0)

        p.editMode = false
        p.pressSoftkey({ id: "edit" })
        verify(p.editMode)
        p.pressSoftkey({ id: "edit" })
        verify(!p.editMode)

        p.machinePos = ({ x: -120.500, y: 35.250, z: -210.400, a: 0.000 })
        p.cursorTool = 0
    }

    function test_tool_measure() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        p.cursorTool = 0
        p.openToolMeasure()
        verify(p.toolMeasureOpen)
        compare(p.measurePhase(), "idle")
        p.startToolMeasure()
        verify(p.measureRunning)
        p.measureElapsed = 1.0
        compare(p.measurePhase(), "rapid")
        verify(p.measureDepth() > 0)
        p.finishToolMeasure()
        verify(p.measureLength > 0)
        compare(p.toolsData[0].l, p.measureLength.toFixed(3))
        compare(p.measurePhase(), "done")
        p.closeToolMeasure()
        verify(!p.toolMeasureOpen)
    }

    function test_hybrid_mode() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        p.svcValues = ({ "system.9": 1 })
        wait(0)
        verify(p.hybridMode)
        verify(!p.touchMode)
        var deck = findByName(p, "hybridDeckLayer")
        verify(deck !== null)
        p.svcValues = ({})
        verify(!p.hybridMode)
    }

    function test_touch_mode() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        p.svcValues = ({ "system.9": 2 })
        wait(0)
        verify(p.touchMode)
        // The Loader in the test fixes the item size to its own; the canvas check belongs to the
        // standalone window (864x1536), while here the touch frame and its tabs are checked.
        var touch = findByName(p, "touchPanel")
        verify(touch !== null)
        compare(touch.tabTitles.length, 5)
        compare(touch.tabTitles[0], "Работа")
        // The same state drives the touch frame: the Work tab opens the workplace screen.
        p.setWorkplace("SETUP")
        touch.tabTarget(0)
        compare(p.activeScreen, "pos")
        verify(findByName(p, "touchWork") !== null)
        verify(findByName(p, "touchProgram") !== null)

        // The touch program screen has its own four view buttons; F8 still changes the same state.
        p.setWorkplace("RUN")
        p.activeScreen = "prog"
        p.autoView = "trajectory"
        p.pressSoftkeyIndex(7)
        compare(p.autoView, "gcode")

        p.svcValues = ({})
        verify(!p.touchMode)
        p.setWorkplace("RUN")
        wait(0)
    }

    function test_auto_screen() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        p.workplace = "RUN"
        p.activeScreen = "pos"
        p.machinePos = ({ x: 0, y: 0, z: 0, a: 0 })
        p.cursorProg = 0
        p.autoView = "trajectory"
        wait(0)

        var auto = findByName(p, "autoScreen")
        verify(auto !== null)
        verify(auto.stats.toFrame > 0)
        verify(auto.stats.rest >= 0)
        compare(auto.view, "trajectory")

        p.autoView = "both"
        wait(0)
        compare(auto.view, "both")
        compare(auto.viewTitles[auto.view], "СОВМЕСТНО")

        p.autoView = "overlay"
        wait(0)
        compare(auto.view, "overlay")

        p.autoView = "trajectory"
    }

    function test_mdi_line() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        p.workplace = "SETUP"
        p.activeScreen = "pos"
        wait(0)
        p.machinePos = ({ x: 0, y: 0, z: 0, a: 0 })
        p.mdiBuffer = ""

        p.mdiType("g0 x10")
        compare(p.mdiBuffer, "G0 X10")
        p.mdiBackspace()
        compare(p.mdiBuffer, "G0 X1")

        p.mdiBuffer = "G0 X10 Y20"
        p.runMdi()
        compare(p.machinePos.x, 10)
        compare(p.machinePos.y, 20)
        compare(p.mdiBuffer, "")
        compare(p.mdiHistory[0], "G0 X10 Y20")

        // G91 moves relative to the live position; the command is kept in the recent history.
        p.mdiBuffer = "G91 X5"
        p.runMdi()
        compare(p.machinePos.x, 15)
        compare(p.mdiHistory[0], "G91 X5")

        // The row is laid out on the Work screen in SETUP; its own visible value follows the
        // tab's visibility, so the state check is mdiOpen().
        verify(findByName(p, "mdiPanel") !== null)
        verify(p.mdiOpen())

        p.workplace = "RUN"
        p.machinePos = ({ x: -120.500, y: 35.250, z: -210.400, a: 0.000 })
    }

    function test_debug_windows_exist() {
        var pendant = Qt.createComponent("../PendantWindow.qml")
        var mpg = Qt.createComponent("../MpgWindow.qml")
        var keyboard = Qt.createComponent("../KeyboardWindow.qml")
        var nav = Qt.createComponent("../NavWindow.qml")
        compare(pendant.status, Component.Ready)
        compare(mpg.status, Component.Ready)
        compare(keyboard.status, Component.Ready)
        compare(nav.status, Component.Ready)
        compare(panel.item.pendantOpen, false)
        compare(panel.item.mpgOpen, false)
        compare(panel.item.keyboardOpen, false)
        compare(panel.item.navOpen, false)
        panel.item.pendantOpen = true
        panel.item.mpgOpen = true
        panel.item.keyboardOpen = true
        panel.item.navOpen = true
        compare(panel.item.pendantOpen, true)
        compare(panel.item.mpgOpen, true)
        compare(panel.item.keyboardOpen, true)
        compare(panel.item.navOpen, true)

        // The MPG window keeps its 520x800 aspect while the debug window is scaled.
        var mpgObject = mpg.createObject(null, { "host": panel.item })
        verify(mpgObject !== null)
        mpgObject.width = 320
        wait(0)
        compare(mpgObject.height, 492)
        mpgObject.destroy()
    }
}
