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
