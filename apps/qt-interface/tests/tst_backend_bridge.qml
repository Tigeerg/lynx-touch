import QtQuick
import QtTest
import ".."

// The backend seam: the panel reads live values and sends `{ ok, reason }` commands through one
// object, whichever backend is behind it. `StubBackend` stands in for `MachineBackend`; a plain QML
// test has no `machine` context property, so the bridge starts disconnected and the test wires the
// stub in explicitly.
TestCase {
    name: "BackendBridge"
    width: 320
    height: 200

    BackendBridge { id: bridge }
    StubBackend { id: stub }

    function init() {
        stub.resetState()
        bridge.backend = null
        bridge.lastReason = ""
        bridge.lastCommand = ""
    }

    function test_without_backend() {
        verify(!bridge.connected)
        compare(bridge.run, "")
        compare(bridge.runLabel, "ГОТОВ")
        compare(bridge.clock, "--:--:--")
        compare(bridge.toolLabel, "")
        compare(bridge.activeAlarmCount, 0)
        compare(bridge.position.x, 0)
        // A command without a backend is refused with a reason, not silently dropped.
        verify(!bridge.cycleStart())
        compare(bridge.lastCommand, "cycleStart")
        compare(bridge.lastReason, "Backend не подключён")
    }

    function test_reads_live_values() {
        bridge.backend = stub
        verify(bridge.connected)
        compare(bridge.run, "IDLE")
        compare(bridge.mode, "AUTO")
        compare(bridge.workplace, "RUN")
        compare(bridge.position.x, -120.5)
        compare(bridge.machinePosition.z, -210.4)
        compare(bridge.activeOffsetCode, "G54")
        compare(bridge.tool, 1)
        compare(bridge.programName, "O1024")
        compare(bridge.programTitle, "КОРПУС ФЛАНЦА")
        compare(bridge.overrides.feed, 60)
        compare(bridge.cycleTime, 42000)
        compare(bridge.partCount, 7)
        compare(bridge.load, 38)
        compare(bridge.clock, "10:45:00")
        // Only switched-on messages are counted.
        compare(bridge.activeAlarmCount, 2)
        compare(bridge.toolInSpindle, 1)
    }

    function test_position_follows_backend() {
        bridge.backend = stub
        compare(bridge.position.y, 35.25)
        stub.position = ({ x: 10, y: 20, z: -30, a: 90 })
        compare(bridge.position.x, 10)
        compare(bridge.position.a, 90)
    }

    function test_run_labels() {
        bridge.backend = stub
        var cases = [ ["IDLE", "ГОТОВ"], ["RUN", "ВЫПОЛНЕНИЕ"], ["HOLD", "ПАУЗА"],
                      ["ALARM", "АВАРИЯ"], ["ESTOP", "СТОП"] ]
        for (var i = 0; i < cases.length; i++) {
            stub.run = cases[i][0]
            compare(bridge.runLabel, cases[i][1])
        }
        stub.run = "RUN"
        verify(bridge.running)
        verify(!bridge.holding)
        stub.run = "HOLD"
        verify(bridge.holding)
        stub.run = "ESTOP"
        verify(bridge.stopped)
    }

    function test_tool_label_padding() {
        bridge.backend = stub
        stub.tool = 1
        compare(bridge.toolLabel, "T01")
        stub.tool = 12
        compare(bridge.toolLabel, "T12")
        stub.tool = 0
        compare(bridge.toolLabel, "")
    }

    function test_commands_route_to_backend() {
        bridge.backend = stub

        verify(bridge.cycleStart())
        compare(stub.lastCommandName, "cycleStart")
        compare(bridge.lastReason, "")

        verify(bridge.jog("X", 2, -1))
        compare(stub.lastCommandName, "jog:X:2:-1")

        verify(bridge.setWorkplace("SETUP"))
        compare(stub.lastCommandName, "setWorkplace:SETUP")

        verify(bridge.mdi("G0 X0"))
        compare(stub.lastCommandName, "mdi:G0 X0")

        verify(bridge.setOverride("feed", 1))
        compare(stub.lastCommandName, "setOverride:feed:1")

        verify(bridge.toggle("spindle"))
        compare(stub.lastCommandName, "toggle:spindle")

        verify(bridge.loadProgram(1))
        compare(stub.lastCommandName, "loadProgram:1")

        verify(bridge.startFromCursor())
        compare(stub.lastCommandName, "startFromCursor")

        verify(bridge.emergencyStop())
        compare(stub.lastCommandName, "emergencyStop")
    }

    function test_refusal_carries_reason() {
        bridge.backend = stub
        stub.commandOk = false
        stub.commandReason = "Станок занят"

        verify(!bridge.cycleStart())
        compare(bridge.lastReason, "Станок занят")
        compare(bridge.lastCommand, "cycleStart")

        // A silent refusal without a reason still gets a readable default.
        stub.commandReason = ""
        verify(!bridge.feedHold())
        compare(bridge.lastReason, "Команда отклонена")
        compare(bridge.lastCommand, "feedHold")

        // A later success clears the stored reason.
        stub.commandOk = true
        verify(bridge.reset())
        compare(bridge.lastReason, "")
    }

    function test_rapid_steps_and_history() {
        bridge.backend = stub
        compare(bridge.rapidSteps.length, 4)
        compare(bridge.rapidSteps[3], 100)
        compare(bridge.mdiHistory.length, 0)
        stub.mdiHistory = [ "G91 X5", "G0 X10" ]
        compare(bridge.mdiHistory[0], "G91 X5")
        // Without a backend the panel keeps its documented rapid row.
        bridge.backend = null
        compare(bridge.rapidSteps.length, 4)
        compare(bridge.rapidSteps[0], 5)
        compare(bridge.mdiHistory.length, 0)
    }

    // The screens did not have to be rewritten: the seam turns machine numbers into the row shape
    // they already read (signs and decimals match the React panel).
    function test_presentation_rows() {
        bridge.backend = stub
        compare(bridge.offsetRows.length, 2)
        compare(bridge.offsetRows[0].code, "G54")
        // Negative values carry the typographic minus, as in React ("−120.500").
        compare(bridge.offsetRows[0].x.charCodeAt(0), 0x2212)
        compare(bridge.offsetRows[0].x.substring(1), "120.500")
        compare(bridge.offsetRows[0].y, "+35.250")
        compare(bridge.offsetRows[0].active, true)
        compare(bridge.offsetRows[1].a, "+90.000")
        compare(bridge.offsetRows[1].active, false)

        compare(bridge.toolRows.length, 4)
        compare(bridge.toolRows[0].n, "T01")
        compare(bridge.toolRows[0].d, "12.0")
        compare(bridge.toolRows[0].l, "85.214")
        compare(bridge.toolRows[0].inSpindle, true)
        compare(bridge.toolRows[3].n, "T99")

        // The machine speaks "warning"; the panel row keeps "warn" and its Russian label.
        compare(bridge.alarmRows.length, 3)
        compare(bridge.alarmRows[0].level, "warn")
        compare(bridge.alarmRows[0].lvl, "Предупр.")
        compare(bridge.alarmRows[2].level, "error")
        compare(bridge.alarmRows[2].lvl, "Авария")
        compare(bridge.alarmRows[2].active, false)

        compare(bridge.programRows.length, 4)
        compare(bridge.programRows[0].name, "O1024")
        compare(bridge.programRows[0].modified, "08.02.2026 14:20")
        compare(bridge.programName, "O1024")
        compare(bridge.activeFile, 0)
        compare(bridge.programLine, 0)

        // No backend: the rows are empty and the formatting helpers still answer.
        bridge.backend = null
        compare(bridge.offsetRows.length, 0)
        compare(bridge.toolRows.length, 0)
        compare(bridge.programRows.length, 0)
        compare(bridge.signed3(-1).charCodeAt(0), 0x2212)
        compare(bridge.signed3(-1).substring(1), "1.000")
        compare(bridge.fixed1(2), "2.0")
        compare(bridge.fixed3(2), "2.000")
    }

    function test_table_commands() {
        bridge.backend = stub
        verify(bridge.setActiveOffset(1))
        compare(stub.lastCommandName, "setActiveOffset:1")
        compare(bridge.offsetRows[1].active, true)
        compare(bridge.activeOffsetCode, "G55")

        verify(bridge.zeroOffset(1))
        compare(bridge.offsetRows[1].x, "+0.000")

        verify(bridge.touchOff(1, "Z"))
        compare(bridge.offsetRows[1].z, bridge.signed3(stub.position.z))

        verify(bridge.setToolLife(3, 0))
        compare(bridge.toolRows[2].life, 0)
        verify(bridge.setToolLength(1, 42.5))
        compare(bridge.toolRows[0].l, "42.500")
        verify(bridge.selectTool(3))
        compare(bridge.toolRows[2].inSpindle, true)
        compare(bridge.toolRows[0].inSpindle, false)
        compare(bridge.tool, 3)
        compare(bridge.toolNumberAt(3), 99)

        verify(bridge.acknowledgeAlarm(0))
        compare(bridge.alarmRows[0].active, false)
        compare(bridge.activeAlarmCount, 1)
        verify(bridge.clearAcknowledgedAlarms())
        compare(bridge.alarmRows.length, 1)

        verify(bridge.loadProgram(1))
        compare(bridge.programName, "O2048")
        compare(bridge.activeFile, 1)
        compare(bridge.toolLabel, "T03")

        // A refusal is a normal answer with the machine's reason.
        stub.commandOk = false
        stub.commandReason = "Станок занят"
        verify(!bridge.setActiveOffset(0))
        compare(bridge.lastReason, "Станок занят")
    }

    function test_messages_and_counters() {
        bridge.backend = stub
        compare(bridge.activeAlarmCount, 2)
        stub.alarms = [ { active: false }, { active: false } ]
        compare(bridge.activeAlarmCount, 0)
        stub.alarms = [ { active: true } ]
        compare(bridge.activeAlarmCount, 1)

        stub.partCount = 8
        stub.cycleTime = 1000
        stub.clock = "11:00:00"
        compare(bridge.partCount, 8)
        compare(bridge.cycleTime, 1000)
        compare(bridge.clock, "11:00:00")
    }
}
