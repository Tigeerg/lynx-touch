import QtQuick

// A stand-in for `MachineBackend` for the QML tests: the same properties and `{ ok, reason }`
// commands, held in one place so the bridge test and the panel test cannot drift apart. The simple
// state changes (a table row, the active program, the active offset) are mirrored here; the G-code
// and MDI semantics stay in the C++ simulator and are checked by `lynx-backend-test`.
QtObject {
    id: stub

    property bool connected: true
    property string run: "IDLE"
    property string mode: "AUTO"
    property string workplace: "RUN"
    property var position: ({ x: -120.5, y: 35.25, z: -210.4, a: 0 })
    property var machinePosition: position
    property var offsets: [
        { code: "G54", values: { x: -120.5, y: 35.25, z: -210.4, a: 0 }, active: true },
        { code: "G55", values: { x: -320.0, y: 35.25, z: -210.4, a: 90 }, active: false }
    ]
    property int activeOffset: 0
    property var tools: [
        { number: 1, name: "Фреза концевая 4z", diameter: 12.0, length: 85.214, life: 72, inSpindle: true },
        { number: 2, name: "Сверло спиральное", diameter: 6.8, length: 112.06, life: 54, inSpindle: false },
        { number: 3, name: "Метчик M8", diameter: 8.0, length: 96.3, life: 88, inSpindle: false },
        { number: 99, name: "3D щуп Renishaw OMP40-2", diameter: 2.0, length: 98.45, life: 100, inSpindle: false }
    ]
    property int tool: 1
    property var toolTime: ({ "1": 0 })
    property var probe: ({ tool: 99, running: false, result: false, step: 0 })
    property var overrides: ({ feed: 60, spindle: 90, rapid: 100 })
    property var rapidSteps: [ 5, 25, 50, 100 ]
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
    property var alarms: [
        { id: "a1", code: "2051", text: "Ресурс инструмента T04 менее 10%", level: "warning", time: "07:42:10", active: true },
        { id: "a2", code: "3012", text: "Плановое ТО шпинделя через 48 ч", level: "info", time: "07:30:02", active: true },
        { id: "a3", code: "1104", text: "Низкий уровень СОЖ в баке", level: "error", time: "06:58:44", active: false }
    ]
    property var programs: [
        { name: "O1024", title: "КОРПУС ФЛАНЦА", modified: "08.02.2026 14:20",
          lines: [ "O1024 (КОРПУС ФЛАНЦА)", "N10 G21 G90 G17 G40 G49 G80", "N20 T2 M06 (СВЕРЛО D6.8)",
                   "N30 G54 G00 X0 Y0", "N40 S2400 M03", "N50 G43 H02 Z50.0 M08",
                   "N60 G00 X-50.0 Y-30.0 Z3.0", "N70 G01 Z-15.0 F180", "N80 G00 Z3.0", "N90 X50.0",
                   "N100 G01 Z-15.0 F180", "N110 G00 Z3.0", "N120 X-50.0 Y30.0", "N130 G01 Z-15.0 F180",
                   "N140 G00 Z3.0", "N150 M30" ] },
        { name: "O2048", title: "ПЛИТА КРЕПЁЖНАЯ", modified: "05.02.2026 09:12",
          lines: [ "O2048 (ПЛИТА КРЕПЁЖНАЯ)", "N10 G21 G90 G17", "N20 T1 M06 (ФРЕЗА D12)",
                   "N30 S3200 M03", "N40 G00 X-80.0 Y-50.0 Z3.0", "N50 G01 Z-8.0 F240",
                   "N60 G01 X80.0", "N70 M30" ] },
        { name: "O3100", title: "КОРПУС ДЛЯ СБОРКИ", modified: "29.01.2026 16:47",
          lines: [ "O3100 (КОРПУС ДЛЯ СБОРКИ)", "N10 G21 G90 G17", "N20 M30" ] },
        { name: "O0007", title: "ПРОГРЕВ ШПИНДЕЛЯ", modified: "12.01.2026 07:05",
          lines: [ "O0007 (ПРОГРЕВ ШПИНДЕЛЯ)", "N10 S2000", "N20 G04 P5", "N30 S8000", "N40 M30" ] }
    ]
    property int activeFile: 0
    property int programLine: 0
    property int programCursor: 0
    readonly property var program: {
        var f = programs[activeFile] ? programs[activeFile] : ({});
        var next = "";
        if (f.lines && programLine + 1 < f.lines.length)
            next = f.lines[programLine + 1];
        return { name: f.name, title: f.title, modified: f.modified, lines: f.lines,
                 activeFile: activeFile, line: programLine, cursor: programCursor, nextLine: next };
    }
    property string clock: "10:45:00"
    property string message: ""
    property var mdiHistory: []

    // Command answers are configurable so a refusal can be checked, not only a success.
    property bool commandOk: true
    property string commandReason: ""
    property string lastCommandName: ""

    function answer(name) {
        lastCommandName = name;
        return { ok: commandOk, reason: commandReason };
    }

    // Back to the documented start state: a test that moved the position or switched a message off
    // must not leak into the next one.
    function resetState() {
        connected = true
        run = "IDLE"
        mode = "AUTO"
        workplace = "RUN"
        position = ({ x: -120.5, y: 35.25, z: -210.4, a: 0 })
        offsets = [
            { code: "G54", values: { x: -120.5, y: 35.25, z: -210.4, a: 0 }, active: true },
            { code: "G55", values: { x: -320.0, y: 35.25, z: -210.4, a: 90 }, active: false }
        ]
        activeOffset = 0
        tools = [
            { number: 1, name: "Фреза концевая 4z", diameter: 12.0, length: 85.214, life: 72, inSpindle: true },
            { number: 2, name: "Сверло спиральное", diameter: 6.8, length: 112.06, life: 54, inSpindle: false },
            { number: 3, name: "Метчик M8", diameter: 8.0, length: 96.3, life: 88, inSpindle: false },
            { number: 99, name: "3D щуп Renishaw OMP40-2", diameter: 2.0, length: 98.45, life: 100, inSpindle: false }
        ]
        tool = 1
        overrides = ({ feed: 60, spindle: 90, rapid: 100 })
        rapidSteps = [ 5, 25, 50, 100 ]
        spindle = false
        coolant = false
        alarms = [
            { id: "a1", code: "2051", text: "Ресурс инструмента T04 менее 10%", level: "warning", time: "07:42:10", active: true },
            { id: "a2", code: "3012", text: "Плановое ТО шпинделя через 48 ч", level: "info", time: "07:30:02", active: true },
            { id: "a3", code: "1104", text: "Низкий уровень СОЖ в баке", level: "error", time: "06:58:44", active: false }
        ]
        activeFile = 0
        programLine = 0
        programCursor = 0
        partCount = 7
        cycleTime = 42000
        clock = "10:45:00"
        message = ""
        mdiHistory = []
        commandOk = true
        commandReason = ""
        lastCommandName = ""
    }

    function cycleStart() { return answer("cycleStart") }
    function feedHold() { return answer("feedHold") }
    function reset() { return answer("reset") }
    function emergencyStop() { return answer("emergencyStop") }
    function setWorkplace(workplace) { return answer("setWorkplace:" + workplace) }
    function setMode(mode) { return answer("setMode:" + mode) }
    function jog(axis, step, direction) { return answer("jog:" + axis + ":" + step + ":" + direction) }
    function mdi(text) { return answer("mdi:" + text) }
    function startFromCursor() { return answer("startFromCursor") }
    function toggle(key) { return answer("toggle:" + key) }
    function acknowledgeAlarms() { return answer("acknowledgeAlarms") }
    function clearAcknowledgedAlarms() {
        var r = answer("clearAcknowledgedAlarms")
        if (r.ok) {
            var kept = [];
            for (var i = 0; i < alarms.length; i++)
                if (alarms[i].active) kept.push(alarms[i]);
            alarms = kept;
        }
        return r
    }
    function setOverride(kind, direction) { return answer("setOverride:" + kind + ":" + direction) }
    function loadProgram(index) {
        var r = answer("loadProgram:" + index)
        if (r.ok) {
            if (index < 0 || index >= programs.length)
                return { ok: false, reason: "Нет такой программы" };
            activeFile = index
            programLine = 0
            programCursor = 0
        }
        return r
    }
    function setActiveOffset(index) {
        var r = answer("setActiveOffset:" + index)
        if (r.ok) {
            if (index < 0 || index >= offsets.length)
                return { ok: false, reason: "Нет такого смещения" };
            var next = [];
            for (var i = 0; i < offsets.length; i++) {
                var row = { code: offsets[i].code, values: offsets[i].values, active: i === index };
                next.push(row);
            }
            offsets = next
            activeOffset = index
        }
        return r
    }
    function zeroOffset(index) {
        var r = answer("zeroOffset:" + index)
        if (r.ok) {
            var next = [];
            for (var i = 0; i < offsets.length; i++)
                next.push({ code: offsets[i].code, values: { x: 0, y: 0, z: 0, a: 0 }, active: offsets[i].active });
            offsets = next
        }
        return r
    }
    function touchOff(index, axis) {
        var r = answer("touchOff:" + index + ":" + axis)
        if (r.ok) {
            var next = [];
            for (var i = 0; i < offsets.length; i++) {
                var v = offsets[i].values;
                var values = { x: v.x, y: v.y, z: v.z, a: v.a };
                if (i === index)
                    values[axis.toLowerCase()] = position[axis.toLowerCase()];
                next.push({ code: offsets[i].code, values: values, active: offsets[i].active });
            }
            offsets = next
        }
        return r
    }
    function setToolLife(number, life) {
        var r = answer("setToolLife:" + number + ":" + life)
        if (r.ok) {
            var next = [];
            for (var i = 0; i < tools.length; i++) {
                var t = tools[i];
                next.push({ number: t.number, name: t.name, diameter: t.diameter, length: t.length,
                            life: t.number === number ? Math.max(0, Math.min(100, life)) : t.life,
                            inSpindle: t.inSpindle });
            }
            tools = next
        }
        return r
    }
    function setToolLength(number, length) {
        var r = answer("setToolLength:" + number)
        if (r.ok) {
            var next = [];
            for (var i = 0; i < tools.length; i++) {
                var t = tools[i];
                next.push({ number: t.number, name: t.name, diameter: t.diameter,
                            length: t.number === number ? length : t.length, life: t.life,
                            inSpindle: t.inSpindle });
            }
            tools = next
        }
        return r
    }
    function selectTool(number) {
        var r = answer("selectTool:" + number)
        if (r.ok) {
            var next = [];
            for (var i = 0; i < tools.length; i++) {
                var t = tools[i];
                next.push({ number: t.number, name: t.name, diameter: t.diameter, length: t.length,
                            life: t.life, inSpindle: t.number === number });
            }
            tools = next
            tool = number
        }
        return r
    }
    function acknowledgeAlarm(index) {
        var r = answer("acknowledgeAlarm:" + index)
        if (r.ok) {
            if (index < 0 || index >= alarms.length)
                return { ok: false, reason: "Нет такого сообщения" };
            var next = [];
            for (var i = 0; i < alarms.length; i++) {
                var a = alarms[i];
                next.push({ id: a.id, code: a.code, text: a.text, level: a.level, time: a.time,
                            active: i === index ? false : a.active });
            }
            alarms = next
        }
        return r
    }
}
