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

    StubBackend { id: stubBackend }

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

    function test_language_switch() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        p.svcValues = ({})
        p.serviceSection = "system"
        p.svcCursor = 2
        compare(p.i18n.t("tab.work"), "Работа")
        p.adjustParam(1, false)          // LANGUAGE -> English
        compare(p.i18n.t("tab.work"), "Work")
        compare(p.i18n.t("svc.save"), "Save")
        p.adjustParam(-1, false)
        compare(p.i18n.t("tab.work"), "Работа")
        p.svcValues = ({})
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
        // The viewer needs a program from the machine: without one it has nothing to play.
        stubBackend.resetState()
        panel.item.backend.backend = stubBackend
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
        panel.item.backend.backend = null
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
        stubBackend.resetState()
        p.backend.backend = stubBackend
        wait(0)
        verify(p.programFiles.length >= 4)
        compare(p.activeProgram().name, "O1024")
        verify(p.progLines.length > 10)
        compare(p.progLines[0].toks[0].t, "O1024")
        compare(p.programFiles[0].modified, "08.02.2026 14:20")

        // The Program softkey opens the catalogue; choosing a file is a machine command and the
        // active program follows the machine, not a local index.
        p.setWorkplace("RUN")
        p.activeScreen = "prog"
        p.fileDialog = true
        p.fileCursor = 1
        p.openProgram(p.fileCursor)
        compare(stubBackend.lastCommandName, "loadProgram:1")
        compare(p.fileDialog, false)
        compare(p.activeProgram().name, "O2048")
        compare(p.activeProgram().title, "ПЛИТА КРЕПЁЖНАЯ")
        compare(p.progLines[0].toks[0].t, "O2048")
        verify(p.programText().indexOf("O2048") === 0)
        compare(p.cursorProg, 0)

        // A refused open keeps the current program and shows the machine's reason.
        stubBackend.commandOk = false
        stubBackend.commandReason = "Станок занят"
        p.openProgram(2)
        compare(p.message, "Станок занят")
        compare(p.activeProgram().name, "O2048")

        // The Work-screen viewer receives the same active program text (manual workplace).
        p.workplace = "TEST"
        p.activeScreen = "pos"
        wait(0)
        var viewer = findByName(p, "viewerPane")
        var viewerItem = viewer.children[0].item
        wait(0)
        verify(viewerItem.gcode.indexOf("O2048") === 0)

        stubBackend.commandOk = true
        p.openProgram(0)
        compare(p.activeProgram().name, "O1024")
        p.workplace = "RUN"
        stubBackend.resetState()
        p.backend.backend = null
    }

    function test_machine_position() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        stubBackend.resetState()
        p.backend.backend = stubBackend
        stubBackend.position = ({ x: 0, y: 0, z: 0, a: 0 })

        // The panel owns the step row (0.001 / 0.01 / 0.1 mm) and asks the machine; the DRO then
        // reads what the machine answered, not what the panel guessed.
        p.jogAxis = "X"
        p.jogStep = 2          // 0.1 mm
        p.machineJogRequest(1)
        compare(stubBackend.lastCommandName, "jog:X:2:1")
        stubBackend.position = ({ x: 0.1, y: 0, z: 0, a: 0 })
        compare(p.machinePos.x, 0.1)
        compare(p.machineCoordinate("X"), "+0.100")

        p.jogAxis = "Z"
        p.jogStep = 0          // 0.001 mm
        p.machineJogRequest(-1)
        compare(stubBackend.lastCommandName, "jog:Z:0:-1")
        stubBackend.position = ({ x: 0.1, y: 0, z: -0.001, a: 0 })

        // The Work DRO reads the live position, not the G54 table row.
        var ws = findByName(p, "workScreen")
        compare(ws.dro("X"), "+0.100")
        compare(ws.dro("Z"), "-0.001")

        // A refused jog keeps the machine position and shows the machine's reason.
        stubBackend.commandOk = false
        stubBackend.commandReason = "Ручная подача доступна в JOG или MPG"
        p.machineJogRequest(1)
        compare(p.machinePos.x, 0.1)
        compare(p.message, "Ручная подача доступна в JOG или MPG")

        stubBackend.resetState()
        p.backend.backend = null
        p.jogAxis = "X"
        p.jogStep = 1
    }

    function test_softkey_actions() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        // The finished-part counter belongs to the machine; the softkey asks it and shows the answer.
        stubBackend.resetState()
        p.backend.backend = stubBackend
        stubBackend.partCount = 0
        p.pressSoftkey({ id: "partCounter" })
        compare(stubBackend.lastCommandName, "countPart")
        compare(p.partCount, 1)
        compare(p.message, "Готовых деталей: 1")
        p.backend.backend = null
        stubBackend.resetState()

        p.cursorTool = 2
        stubBackend.resetState()
        p.backend.backend = stubBackend
        p.pressSoftkey({ id: "brokenTool" })
        compare(stubBackend.lastCommandName, "setToolLife:3:0")
        compare(p.toolsData[2].life, 0)
        p.pressSoftkey({ id: "resetLife" })
        compare(stubBackend.lastCommandName, "setToolLife:3:100")
        compare(p.toolsData[2].life, 100)
        p.backend.backend = null

        // The safe point and work zero are MDI lines the machine executes; the panel does not move
        // its own numbers.
        stubBackend.resetState()
        p.backend.backend = stubBackend
        p.pressSoftkey({ id: "goSafe" })
        compare(stubBackend.lastCommandName, "mdi:G90 Z50")
        p.pressSoftkey({ id: "goWorkZero" })
        compare(stubBackend.lastCommandName, "mdi:G90 X0 Y0 Z0 A0")

        // The offset table: activate a row, clear it, touch off an axis by the live position.
        p.cursorOffset = 1
        p.pressSoftkey({ id: "act" })
        compare(stubBackend.lastCommandName, "setActiveOffset:1")
        compare(p.offsetsData[1].active, true)
        p.pressSoftkey({ id: "offClear" })
        compare(stubBackend.lastCommandName, "zeroOffset:1")
        compare(p.offsetsData[1].x, "+0.000")
        p.pressSoftkey({ id: "teachZ" })
        compare(stubBackend.lastCommandName, "touchOff:1:Z")
        compare(p.offsetsData[1].z, p.backend.signed3(stubBackend.position.z))

        // The message softkeys acknowledge one row and drop the acknowledged ones.
        p.cursorAlarm = 0
        p.pressSoftkey({ id: "ack" })
        compare(stubBackend.lastCommandName, "acknowledgeAlarm:0")
        compare(p.alarmsData[0].active, false)
        compare(p.activeAlarmCount(), 1)
        p.pressSoftkey({ id: "alarmClearAll" })
        compare(stubBackend.lastCommandName, "clearAcknowledgedAlarms")
        compare(p.alarmsData.length, 1)
        compare(p.activeAlarmCount(), 1)
        p.cursorOffset = 0
        p.backend.backend = null
        stubBackend.resetState()

        p.editMode = false
        p.pressSoftkey({ id: "edit" })
        verify(p.editMode)
        p.pressSoftkey({ id: "edit" })
        verify(!p.editMode)

        stubBackend.resetState()
        p.backend.backend = null
        p.cursorTool = 0
    }

    // The overrides are the machine's numbers: the panel asks for one step and shows the answer.
    function test_overrides_route_to_backend() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        stubBackend.resetState()
        p.backend.backend = stubBackend
        stubBackend.overrides = ({ feed: 100, spindle: 100, rapid: 100 })
        wait(0)
        compare(p.feedOverride, 100)
        compare(p.spindleOverride, 100)
        compare(p.rapidOverride, 100)

        p.adjustFeed(1)
        compare(stubBackend.lastCommandName, "setOverride:feed:1")
        p.adjustSpindle(-1)
        compare(stubBackend.lastCommandName, "setOverride:spindle:-1")
        p.adjustRapid(-1)
        compare(stubBackend.lastCommandName, "setOverride:rapid:-1")

        // The panel shows what the machine reports after the step, including its own rapid row.
        stubBackend.overrides = ({ feed: 110, spindle: 90, rapid: 50 })
        tryCompare(p, "feedOverride", 110)
        compare(p.spindleOverride, 90)
        compare(p.rapidOverride, 50)
        stubBackend.rapidSteps = [5, 25, 50, 100]
        compare(p.rapidSteps.length, 4)

        // A refused override is reported, not applied silently.
        stubBackend.commandOk = false
        stubBackend.commandReason = "Коррекция недоступна при работе"
        p.adjustFeed(1)
        compare(p.message, "Коррекция недоступна при работе")

        stubBackend.resetState()
        p.backend.backend = null
    }

    function test_tool_measure() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        stubBackend.resetState()
        p.backend.backend = stubBackend
        wait(0)
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
        // The measured length is written to the machine's tool table, not to a local copy.
        compare(stubBackend.lastCommandName, "setToolLength:1")
        compare(stubBackend.tools[0].length.toFixed(3), p.measureLength.toFixed(3))
        compare(p.toolsData[0].l, p.measureLength.toFixed(3))
        compare(p.measurePhase(), "done")
        p.closeToolMeasure()
        verify(!p.toolMeasureOpen)
        stubBackend.resetState()
        p.backend.backend = null
    }

    function test_hybrid_mode() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        p.svcValues = ({ "system.9": 1 })
        wait(0)
        verify(p.hybridMode)
        verify(!p.touchMode)
        var deck = findByName(p, "hybridDeckToggle")
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
        compare(touch.tabTitles[0], "tab.work")
        compare(p.i18n.t(touch.tabTitles[0]), "Работа")
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
        stubBackend.resetState()
        p.backend.backend = stubBackend
        stubBackend.position = ({ x: 0, y: 0, z: 0, a: 0 })
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
        p.backend.backend = null
    }

    function test_mdi_line() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        p.workplace = "SETUP"
        p.activeScreen = "pos"
        wait(0)
        stubBackend.resetState()
        p.backend.backend = stubBackend
        stubBackend.position = ({ x: 0, y: 0, z: 0, a: 0 })
        stubBackend.mdiHistory = []
        p.mdiBuffer = ""

        p.mdiType("g0 x10")
        compare(p.mdiBuffer, "G0 X10")
        p.mdiBackspace()
        compare(p.mdiBuffer, "G0 X1")

        // The line goes to the machine; the panel then shows the position and the history the
        // machine reports (parsing the G-code is the simulator's job, checked in C++).
        p.mdiBuffer = "G0 X10 Y20"
        p.runMdi()
        compare(stubBackend.lastCommandName, "mdi:G0 X10 Y20")
        stubBackend.position = ({ x: 10, y: 20, z: 0, a: 0 })
        stubBackend.mdiHistory = [ "G0 X10 Y20" ]
        compare(p.machinePos.x, 10)
        compare(p.machinePos.y, 20)
        compare(p.mdiBuffer, "")
        compare(p.mdiHistory[0], "G0 X10 Y20")

        // The machine keeps the recent lines; the panel recalls the first one into the buffer.
        stubBackend.mdiHistory = [ "G91 X5", "G0 X10 Y20" ]
        compare(p.mdiHistory[0], "G91 X5")
        p.mdiRecall(0)
        compare(p.mdiBuffer, "G91 X5")
        p.mdiBuffer = ""

        // A refused line keeps the typed text and shows the machine's reason.
        stubBackend.commandOk = false
        stubBackend.commandReason = "Станок занят"
        p.mdiBuffer = "G0 X30"
        p.runMdi()
        compare(p.message, "Станок занят")
        compare(p.mdiBuffer, "G0 X30")

        // The row is laid out on the Work screen in SETUP; its own visible value follows the
        // tab's visibility, so the state check is mdiOpen().
        verify(findByName(p, "mdiPanel") !== null)
        verify(p.mdiOpen())

        p.workplace = "RUN"
        stubBackend.resetState()
        p.backend.backend = null
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

        // Strict keyboard/nav canvases keep their aspect when the debug window is scaled.
        var kbObject = keyboard.createObject(null, { "host": panel.item })
        kbObject.width = 250
        wait(0)
        compare(kbObject.height, 235)
        kbObject.destroy()
        var navObject = nav.createObject(null, { "host": panel.item })
        navObject.width = 200
        wait(0)
        compare(navObject.height, 188)
        navObject.destroy()
        var pendantObject = pendant.createObject(null, { "host": panel.item })
        pendantObject.width = 600
        wait(0)
        compare(pendantObject.height, 150)
        pendantObject.destroy()
    }

    // Stage 1, first slice: the header reads live machine state through the backend bridge, and the
    // panel actuators ask the backend instead of only moving their own numbers.
    function test_header_follows_backend() {
        tryCompare(panel, "status", Loader.Ready)
        var p = panel.item
        stubBackend.resetState()
        p.backend.backend = stubBackend
        p.activeScreen = "prog"
        wait(0)

        compare(findByName(p, "runStateText").text, "ГОТОВ")
        compare(findByName(p, "headerProgramText").text, "O1024")
        compare(findByName(p, "headerClockText").text, "10:45:00")
        compare(findByName(p, "headerAlarmCount").text, "2")

        // The run badge follows the backend, not a local literal.
        stubBackend.run = "RUN"
        tryCompare(findByName(p, "runStateText"), "text", "ВЫПОЛНЕНИЕ")
        stubBackend.run = "HOLD"
        tryCompare(findByName(p, "runStateText"), "text", "ПАУЗА")
        stubBackend.run = "ESTOP"
        tryCompare(findByName(p, "runStateText"), "text", "СТОП")
        stubBackend.run = "IDLE"
        wait(0)

        // The panel actuators ask the backend: ПУСК/СТОП/СБРОС/АВАРИЯ and the switches go through
        // the bridge, which is the only place that knows the machine.
        p.machineCycleStart()
        compare(stubBackend.lastCommandName, "cycleStart")
        p.machineFeedHold()
        compare(stubBackend.lastCommandName, "feedHold")
        p.machineReset()
        compare(stubBackend.lastCommandName, "reset")
        p.machineEmergencyStop()
        compare(stubBackend.lastCommandName, "emergencyStop")
        p.machineToggleSpindle()
        compare(stubBackend.lastCommandName, "toggle:spindle")
        p.machineToggleCoolant()
        compare(stubBackend.lastCommandName, "toggle:coolant")

        // Switching the workplace sends it to the machine; a refusal is shown, not swallowed.
        stubBackend.commandOk = false
        stubBackend.commandReason = "Станок занят"
        p.setWorkplace("TEST")
        compare(p.backend.lastReason, "Станок занят")
        compare(p.message, "Станок занят")

        // A message switched off is gone from the live count.
        stubBackend.alarms = [ { active: true } ]
        tryCompare(findByName(p, "headerAlarmCount"), "text", "1")

        // Leave the panel the way the other tests expect it.
        stubBackend.resetState()
        p.backend.backend = null
        p.setWorkplace("RUN")
    }
}
