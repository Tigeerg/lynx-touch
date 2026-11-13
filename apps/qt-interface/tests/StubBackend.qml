import QtQuick

// A stand-in for `MachineBackend` for the QML tests: the same properties and `{ ok, reason }`
// commands, held in one place so the bridge test and the panel test cannot drift apart. The real
// backend is C++ (`apps/qt-interface/backend`) and is checked by `lynx-backend-test`.
QtObject {
    id: stub

    property bool connected: true
    property string run: "IDLE"
    property string mode: "AUTO"
    property string workplace: "RUN"
    property var position: ({ x: -120.5, y: 35.25, z: -210.4, a: 0 })
    property var machinePosition: position
    property var offsets: [ { code: "G54", active: true }, { code: "G55", active: false } ]
    property int activeOffset: 0
    property var tools: [ { number: 1, inSpindle: true }, { number: 12, inSpindle: false } ]
    property int tool: 1
    property var toolTime: ({ "1": 0 })
    property var probe: ({ tool: 99, running: false, result: false, step: 0 })
    property var program: ({ name: "O1024", title: "КОРПУС ФЛАНЦА" })
    property var overrides: ({ feed: 60, spindle: 90, rapid: 100 })
    property bool spindle: false
    property bool coolant: false
    property int load: 38
    property var axisLoad: ({ X: 0, Y: 0, Z: 0, A: 0 })
    property int cycleTime: 42000
    property int lastCycle: 198000
    property int partCount: 7
    property bool singleBlock: false
    property bool optStop: true
    property bool dryRun: false
    property var alarms: [ { active: true }, { active: false }, { active: true } ]
    property string clock: "10:45:00"
    property string message: ""

    // Command answers are configurable so a refusal can be checked, not only a success.
    property bool commandOk: true
    property string commandReason: ""
    property string lastCommandName: ""

    // Back to the documented start state: a test that moved the position or switched a message off
    // must not leak into the next one.
    function resetState() {
        connected = true
        run = "IDLE"
        mode = "AUTO"
        workplace = "RUN"
        position = ({ x: -120.5, y: 35.25, z: -210.4, a: 0 })
        offsets = [ { code: "G54", active: true }, { code: "G55", active: false } ]
        activeOffset = 0
        tools = [ { number: 1, inSpindle: true }, { number: 12, inSpindle: false } ]
        tool = 1
        program = ({ name: "O1024", title: "КОРПУС ФЛАНЦА" })
        overrides = ({ feed: 60, spindle: 90, rapid: 100 })
        spindle = false
        coolant = false
        alarms = [ { active: true }, { active: false }, { active: true } ]
        partCount = 7
        cycleTime = 42000
        clock = "10:45:00"
        message = ""
        commandOk = true
        commandReason = ""
        lastCommandName = ""
    }

    function cycleStart() { lastCommandName = "cycleStart"; return { ok: commandOk, reason: commandReason } }
    function feedHold() { lastCommandName = "feedHold"; return { ok: commandOk, reason: commandReason } }
    function reset() { lastCommandName = "reset"; return { ok: commandOk, reason: commandReason } }
    function emergencyStop() { lastCommandName = "emergencyStop"; return { ok: commandOk, reason: commandReason } }
    function setWorkplace(workplace) { lastCommandName = "setWorkplace:" + workplace; return { ok: commandOk, reason: commandReason } }
    function setMode(mode) { lastCommandName = "setMode:" + mode; return { ok: commandOk, reason: commandReason } }
    function jog(axis, step, direction) { lastCommandName = "jog:" + axis + ":" + step + ":" + direction; return { ok: commandOk, reason: commandReason } }
    function mdi(text) { lastCommandName = "mdi:" + text; return { ok: commandOk, reason: commandReason } }
    function loadProgram(index) { lastCommandName = "loadProgram:" + index; return { ok: commandOk, reason: commandReason } }
    function startFromCursor() { lastCommandName = "startFromCursor"; return { ok: commandOk, reason: commandReason } }
    function setOverride(kind, direction) { lastCommandName = "setOverride:" + kind + ":" + direction; return { ok: commandOk, reason: commandReason } }
    function toggle(key) { lastCommandName = "toggle:" + key; return { ok: commandOk, reason: commandReason } }
    function acknowledgeAlarms() { lastCommandName = "acknowledgeAlarms"; return { ok: commandOk, reason: commandReason } }
    function clearAcknowledgedAlarms() { lastCommandName = "clearAcknowledgedAlarms"; return { ok: commandOk, reason: commandReason } }
}
