import QtQuick

// The single seam between the panel and the machine data. Values read one way, `{ ok, reason }`
// commands the other. Behind it is the simulator for tests and demos (`--backend=sim`) or, later,
// the LinuxCNC adapter; the panel never branches on which one it is.
//
// main.cpp injects the backend as the QML context property `machine`; a plain QML test has no such
// property and instead sets `bridge.backend` to a stub.
QtObject {
    id: bridge
    objectName: "backendBridge"

    property QtObject backend: (typeof machine !== "undefined" ? machine : null)

    readonly property bool connected: backend !== null && backend.connected === true

    // --- run state -------------------------------------------------------------------------
    readonly property string run: backend ? backend.run : ""
    readonly property string mode: backend ? backend.mode : ""
    readonly property string workplace: backend ? backend.workplace : ""
    readonly property bool running: run === "RUN"
    readonly property bool holding: run === "HOLD"
    readonly property bool stopped: run === "ALARM" || run === "ESTOP"
    readonly property string runLabel: run === "RUN" ? "ВЫПОЛНЕНИЕ"
                                        : run === "HOLD" ? "ПАУЗА"
                                        : run === "ALARM" ? "АВАРИЯ"
                                        : run === "ESTOP" ? "СТОП" : "ГОТОВ"

    // --- geometry --------------------------------------------------------------------------
    readonly property var position: backend ? backend.position : ({ x: 0, y: 0, z: 0, a: 0 })
    readonly property var machinePosition: backend ? backend.machinePosition : ({ x: 0, y: 0, z: 0, a: 0 })
    readonly property var offsets: backend ? backend.offsets : []
    readonly property int activeOffset: backend ? backend.activeOffset : -1
    readonly property string activeOffsetCode:
        (activeOffset >= 0 && activeOffset < offsets.length) ? offsets[activeOffset].code : ""

    // --- tool ------------------------------------------------------------------------------
    readonly property var tools: backend ? backend.tools : []
    readonly property int tool: backend ? backend.tool : 0
    readonly property string toolLabel: tool > 0 ? "T" + (tool < 10 ? "0" + tool : tool) : ""
    readonly property var toolTime: backend ? backend.toolTime : ({})
    readonly property var probe: backend ? backend.probe : ({})
    readonly property int toolInSpindle: {
        if (!backend) return 0;
        for (var i = 0; i < tools.length; i++)
            if (tools[i].inSpindle) return tools[i].number;
        return 0;
    }

    // --- program ---------------------------------------------------------------------------
    readonly property var program: backend ? backend.program : ({})
    readonly property string programName: program.name ? program.name : ""
    readonly property string programTitle: program.title ? program.title : ""

    // --- station ---------------------------------------------------------------------------
    readonly property var overrides: backend ? backend.overrides : ({ feed: 100, spindle: 100, rapid: 100 })
    readonly property bool spindle: backend ? backend.spindle : false
    readonly property bool coolant: backend ? backend.coolant : false
    readonly property int load: backend ? backend.load : 0
    readonly property var axisLoad: backend ? backend.axisLoad : ({})
    readonly property int cycleTime: backend ? backend.cycleTime : 0
    readonly property int lastCycle: backend ? backend.lastCycle : 0
    readonly property int partCount: backend ? backend.partCount : 0
    readonly property bool singleBlock: backend ? backend.singleBlock : false
    readonly property bool optStop: backend ? backend.optStop : false
    readonly property bool dryRun: backend ? backend.dryRun : false

    // --- messages --------------------------------------------------------------------------
    readonly property var alarms: backend ? backend.alarms : []
    readonly property int activeAlarmCount: {
        if (!backend) return 0;
        var n = 0;
        for (var i = 0; i < alarms.length; i++)
            if (alarms[i].active) n++;
        return n;
    }
    readonly property string clock: backend ? backend.clock : "--:--:--"
    readonly property string machineMessage: backend ? backend.message : ""

    // The verdict of the last command, so a screen can show why the machine said no instead of
    // staying silent.
    property string lastCommand: ""
    property string lastReason: ""

    function note(result, name) {
        lastCommand = name;
        if (!backend) {
            // There is nothing to talk to: say so instead of blaming the command.
            lastReason = "Backend не подключён";
            return false;
        }
        if (result && result.ok) {
            lastReason = "";
            return true;
        }
        lastReason = (result && result.reason) ? result.reason : "Команда отклонена";
        return false;
    }

    function cycleStart() { return note(backend ? backend.cycleStart() : null, "cycleStart") }
    function feedHold() { return note(backend ? backend.feedHold() : null, "feedHold") }
    function reset() { return note(backend ? backend.reset() : null, "reset") }
    function emergencyStop() { return note(backend ? backend.emergencyStop() : null, "emergencyStop") }
    function setWorkplace(workplace) { return note(backend ? backend.setWorkplace(workplace) : null, "setWorkplace") }
    function setMode(mode) { return note(backend ? backend.setMode(mode) : null, "setMode") }
    function jog(axis, step, direction) { return note(backend ? backend.jog(axis, step, direction) : null, "jog") }
    function mdi(text) { return note(backend ? backend.mdi(text) : null, "mdi") }
    function loadProgram(index) { return note(backend ? backend.loadProgram(index) : null, "loadProgram") }
    function startFromCursor() { return note(backend ? backend.startFromCursor() : null, "startFromCursor") }
    function setOverride(kind, direction) { return note(backend ? backend.setOverride(kind, direction) : null, "setOverride") }
    function toggle(key) { return note(backend ? backend.toggle(key) : null, "toggle") }
    function acknowledgeAlarms() { return note(backend ? backend.acknowledgeAlarms() : null, "acknowledgeAlarms") }
    function clearAcknowledgedAlarms() { return note(backend ? backend.clearAcknowledgedAlarms() : null, "clearAcknowledgedAlarms") }
}
