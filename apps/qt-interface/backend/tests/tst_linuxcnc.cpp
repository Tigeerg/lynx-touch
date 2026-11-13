#include "linuxcnc_backend.h"
#include "linuxcnc_link.h"

#include <QSignalSpy>
#include <QTime>
#include <QtTest>

namespace {

// A stand-in for the controller: the same raw, LinuxCNC-shaped facts, in memory. Every command
// records what it was asked for, so the test can check the mapping in both directions without a
// LinuxCNC installation.
class FakeLink : public LinuxCncLink
{
public:
    bool connectedFlag = true;
    QString errorText;
    int taskStateCode = 1;        // done
    int interpreterStateCode = 1; // idle
    int taskModeCode = 2;         // auto
    bool estopFlag = false;

    double joints[4] = {10.0, 20.0, 30.0, 40.0};
    double g5x[6][4] = {{1.0, 2.0, 3.0, 4.0}, {0, 0, 0, 0}, {0, 0, 0, 0},
                        {0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
    int activeG5x = 0;
    int jointsCount = 4;

    int toolNumbers[4] = {1, 2, 3, 99};
    QString toolNames[4] = {"Фреза концевая 4z", "Сверло спиральное", "Метчик M8", "3D щуп"};
    double toolDia[4] = {12.0, 6.8, 8.0, 2.0};
    double toolLen[4] = {85.214, 112.06, 96.3, 98.45};
    int toolRows = 4;
    int spindleToolNumber = 1;

    bool spindleFlag = false;
    bool coolantFlag = false;
    int feedPercent = 60;
    int spindlePercent = 90;
    int rapidIndex = 0;
    bool singleBlockFlag = false;
    bool optStopFlag = true;
    bool dryRunFlag = false;

    QStringList messages;
    QString program = QStringLiteral("/nc/O1024.ngc");
    QStringList programText;
    int current = 0;
    int cursor = 0;
    QString lastMsg;

    // Command answers.
    bool commandOk = true;
    QStringList called;

    bool connected() const override { return connectedFlag; }
    QString lastError() const override { return errorText; }
    int taskState() const override { return taskStateCode; }
    int interpreterState() const override { return interpreterStateCode; }
    int taskMode() const override { return taskModeCode; }
    bool estop() const override { return estopFlag; }

    double jointPosition(int joint) const override { return joints[joint]; }
    double g5xOffset(int index, int joint) const override { return g5x[index][joint]; }
    int activeG5xIndex() const override { return activeG5x; }
    int jointCount() const override { return jointsCount; }

    int toolCount() const override { return toolRows; }
    int toolNumber(int row) const override { return toolNumbers[row]; }
    QString toolName(int row) const override { return toolNames[row]; }
    double toolDiameter(int row) const override { return toolDia[row]; }
    double toolLength(int row) const override { return toolLen[row]; }
    int toolInSpindle() const override { return spindleToolNumber; }

    bool spindle() const override { return spindleFlag; }
    bool coolant() const override { return coolantFlag; }
    int feedOverride() const override { return feedPercent; }
    int spindleOverride() const override { return spindlePercent; }
    int rapidOverrideIndex() const override { return rapidIndex; }
    bool singleBlock() const override { return singleBlockFlag; }
    bool optStop() const override { return optStopFlag; }
    bool dryRun() const override { return dryRunFlag; }

    QStringList operatorMessages() const override { return messages; }
    QString programFile() const override { return program; }
    QStringList programLines() const override { return programText; }
    int currentLine() const override { return current; }
    int cursorLine() const override { return cursor; }
    QString lastMessage() const override { return lastMsg; }

    bool cycleStart() override { return note(QStringLiteral("cycleStart")); }
    bool feedHold() override { return note(QStringLiteral("feedHold")); }
    bool reset() override { return note(QStringLiteral("reset")); }
    bool setEstop(bool on) override { return note(QStringLiteral("setEstop:") + (on ? "1" : "0")); }
    bool setTaskMode(int mode) override { return note(QStringLiteral("setTaskMode:") + QString::number(mode)); }
    bool jogJoint(int joint, double delta) override
    {
        return note(QStringLiteral("jog:%1:%2").arg(joint).arg(delta, 0, 'f', 3));
    }
    bool runMdiLine(const QString &line) override { return note(QStringLiteral("mdi:") + line); }
    bool openProgram(const QString &path) override { return note(QStringLiteral("openProgram:") + path); }
    bool setOverride(const QString &kind, int percent) override
    {
        return note(QStringLiteral("setOverride:%1:%2").arg(kind).arg(percent));
    }
    bool setToggle(const QString &key, bool on) override
    {
        return note(QStringLiteral("setToggle:%1:%2").arg(key, on ? "1" : "0"));
    }
    bool selectTool(int number) override { return note(QStringLiteral("selectTool:") + QString::number(number)); }
    bool setActiveG5x(int index) override { return note(QStringLiteral("setActiveG5x:") + QString::number(index)); }
    bool zeroG5x(int index) override { return note(QStringLiteral("zeroG5x:") + QString::number(index)); }
    bool touchOffG5x(int index, int joint) override
    {
        return note(QStringLiteral("touchOffG5x:%1:%2").arg(index).arg(joint));
    }
    bool setToolLength(int number, double length) override
    {
        return note(QStringLiteral("setToolLength:%1:%2").arg(number).arg(length, 0, 'f', 3));
    }

private:
    bool note(const QString &name)
    {
        called.append(name);
        return commandOk;
    }
};

} // namespace

class TestLinuxCnc : public QObject
{
    Q_OBJECT

private slots:
    void withoutLink();
    void runAndModeMapping();
    void workPositionAndOffsets();
    void toolTable();
    void messages();
    void programMapping();
    void overridesAndFlags();
    void commandsReachTheLink();
    void refusalsCarryTheMachineReason();
    void gapsAreHonest();
    void pollingSignalsAChange();
};

void TestLinuxCnc::withoutLink()
{
    LinuxCncBackend backend(nullptr);
    QCOMPARE(backend.backendName(), QStringLiteral("linuxcnc"));
    QVERIFY(!backend.connected());
    QVERIFY(backend.lastError().contains(QStringLiteral("не создана")));
    // A command without a link is refused with a reason, never silently dropped.
    const QVariantMap answer = backend.cycleStart();
    QVERIFY(!answer.value(QStringLiteral("ok")).toBool());
    QVERIFY(answer.value(QStringLiteral("reason")).toString().contains(QStringLiteral("не создана")));
    QCOMPARE(backend.position().value(QStringLiteral("x")).toDouble(), 0.0);
}

void TestLinuxCnc::runAndModeMapping()
{
    FakeLink link;
    LinuxCncBackend backend(&link);

    QVERIFY(backend.connected());
    QCOMPARE(backend.run(), QStringLiteral("IDLE"));

    link.taskStateCode = 2;
    QCOMPARE(backend.run(), QStringLiteral("RUN"));
    link.interpreterStateCode = 3;
    QCOMPARE(backend.run(), QStringLiteral("HOLD"));
    link.taskStateCode = 3;
    QCOMPARE(backend.run(), QStringLiteral("ALARM"));
    link.estopFlag = true;
    QCOMPARE(backend.run(), QStringLiteral("ESTOP"));

    link.estopFlag = false;
    link.taskStateCode = 1;
    link.interpreterStateCode = 1;
    link.taskModeCode = 1;
    QCOMPARE(backend.mode(), QStringLiteral("JOG"));
    link.taskModeCode = 2;
    QCOMPARE(backend.mode(), QStringLiteral("AUTO"));
    link.taskModeCode = 3;
    QCOMPARE(backend.mode(), QStringLiteral("MDI"));

    // SETUP/TEST/RUN is the panel's own navigation state: the adapter does not invent it.
    QCOMPARE(backend.workplace(), QString());
}

void TestLinuxCnc::workPositionAndOffsets()
{
    FakeLink link;
    LinuxCncBackend backend(&link);

    // Work position = joint position − the active G54 offset; machine position = the joint itself.
    QCOMPARE(backend.position().value(QStringLiteral("x")).toDouble(), 9.0);
    QCOMPARE(backend.position().value(QStringLiteral("y")).toDouble(), 18.0);
    QCOMPARE(backend.position().value(QStringLiteral("z")).toDouble(), 27.0);
    QCOMPARE(backend.position().value(QStringLiteral("a")).toDouble(), 36.0);
    QCOMPARE(backend.machinePosition().value(QStringLiteral("z")).toDouble(), 30.0);

    const QVariantList rows = backend.offsets();
    QCOMPARE(rows.size(), 6);
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("code")).toString(), QStringLiteral("G54"));
    QCOMPARE(rows.at(5).toMap().value(QStringLiteral("code")).toString(), QStringLiteral("G59"));
    QVERIFY(rows.at(0).toMap().value(QStringLiteral("active")).toBool());
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("values")).toMap()
                 .value(QStringLiteral("x")).toDouble(), 1.0);
    QCOMPARE(backend.activeOffset(), 0);

    // Activating another offset changes what the DRO shows.
    QVERIFY(backend.setActiveOffset(1).value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("setActiveG5x:1"));
    link.activeG5x = 1;
    link.g5x[1][2] = 5.0;
    QCOMPARE(backend.position().value(QStringLiteral("z")).toDouble(), 25.0);
    QVERIFY(backend.offsets().at(1).toMap().value(QStringLiteral("active")).toBool());
    QVERIFY(!backend.offsets().at(0).toMap().value(QStringLiteral("active")).toBool());
}

void TestLinuxCnc::toolTable()
{
    FakeLink link;
    LinuxCncBackend backend(&link);

    const QVariantList rows = backend.tools();
    QCOMPARE(rows.size(), 4);
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Фреза концевая 4z"));
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("diameter")).toDouble(), 12.0);
    QCOMPARE(rows.at(1).toMap().value(QStringLiteral("length")).toDouble(), 112.06);
    // LinuxCNC keeps no tool life: the adapter says 100 % instead of inventing a number.
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("life")).toInt(), 100);
    QVERIFY(rows.at(0).toMap().value(QStringLiteral("inSpindle")).toBool());
    QVERIFY(!rows.at(1).toMap().value(QStringLiteral("inSpindle")).toBool());
    QCOMPARE(backend.tool(), 1);
    // No per-tool cutting time in stat.
    QVERIFY(backend.toolTime().isEmpty());
}

void TestLinuxCnc::messages()
{
    FakeLink link;
    link.messages = {QStringLiteral("2051 Ресурс инструмента T04 менее 10%"),
                     QStringLiteral("Плановое ТО шпинделя")};
    LinuxCncBackend backend(&link);

    const QVariantList rows = backend.alarms();
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("code")).toString(), QStringLiteral("2051"));
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("text")).toString(),
             QStringLiteral("Ресурс инструмента T04 менее 10%"));
    // stat carries no level, so the adapter says "info" rather than guessing.
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("level")).toString(), QStringLiteral("info"));
    QCOMPARE(rows.at(1).toMap().value(QStringLiteral("code")).toString(), QString());
    QCOMPARE(rows.at(1).toMap().value(QStringLiteral("text")).toString(),
             QStringLiteral("Плановое ТО шпинделя"));
    QVERIFY(rows.at(1).toMap().value(QStringLiteral("active")).toBool());
}

void TestLinuxCnc::programMapping()
{
    FakeLink link;
    link.programText = {QStringLiteral("O1024 (КОРПУС ФЛАНЦА)"), QStringLiteral("N10 G21 G90"),
                        QStringLiteral("N20 T2 M06"), QStringLiteral("N30 M30")};
    link.current = 1;
    link.cursor = 2;
    LinuxCncBackend backend(&link);

    const QVariantMap program = backend.program();
    QCOMPARE(program.value(QStringLiteral("name")).toString(), QStringLiteral("/nc/O1024.ngc"));
    QCOMPARE(program.value(QStringLiteral("lines")).toStringList().size(), 4);
    QCOMPARE(program.value(QStringLiteral("activeFile")).toInt(), 0);
    QCOMPARE(program.value(QStringLiteral("line")).toInt(), 1);
    QCOMPARE(program.value(QStringLiteral("cursor")).toInt(), 2);
    QCOMPARE(program.value(QStringLiteral("nextLine")).toString(), QStringLiteral("N20 T2 M06"));
    QCOMPARE(backend.programs().size(), 1);
    QCOMPARE(backend.programs().at(0).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("/nc/O1024.ngc"));

    // The adapter carries the one program the controller has open.
    QVERIFY(backend.loadProgram(0).value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("openProgram:/nc/O1024.ngc"));
    QVERIFY(!backend.loadProgram(1).value(QStringLiteral("ok")).toBool());
}

void TestLinuxCnc::overridesAndFlags()
{
    FakeLink link;
    LinuxCncBackend backend(&link);

    QCOMPARE(backend.overrides().value(QStringLiteral("feed")).toInt(), 60);
    QCOMPARE(backend.overrides().value(QStringLiteral("spindle")).toInt(), 90);
    QCOMPARE(backend.overrides().value(QStringLiteral("rapid")).toInt(), 5);
    QCOMPARE(backend.rapidSteps().size(), 4);
    QCOMPARE(backend.rapidSteps().at(3).toInt(), 100);
    QVERIFY(backend.optStop());
    QVERIFY(!backend.singleBlock());
    QVERIFY(!backend.dryRun());
    QVERIFY(!backend.spindle());
    QVERIFY(!backend.coolant());

    // The clock is the operator's wall clock, not a stat field.
    QVERIFY(QTime::fromString(backend.clock(), QStringLiteral("HH:mm:ss")).isValid());
}

void TestLinuxCnc::commandsReachTheLink()
{
    FakeLink link;
    LinuxCncBackend backend(&link);

    QVERIFY(backend.cycleStart().value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("cycleStart"));
    QVERIFY(backend.feedHold().value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("feedHold"));
    QVERIFY(backend.reset().value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("reset"));

    QVERIFY(backend.emergencyStop().value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("setEstop:1"));

    QVERIFY(backend.setMode(QStringLiteral("AUTO")).value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("setTaskMode:2"));
    QVERIFY(backend.setMode(QStringLiteral("MPG")).value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("setTaskMode:1"));
    QVERIFY(!backend.setMode(QStringLiteral("NOPE")).value(QStringLiteral("ok")).toBool());

    // The panel step row stays the panel's: 0.01 mm on X, direction +1.
    QVERIFY(backend.jog(QStringLiteral("X"), 1, 1).value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("jog:0:0.010"));
    QVERIFY(backend.jog(QStringLiteral("A"), 2, -1).value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("jog:3:-0.100"));
    QVERIFY(!backend.jog(QStringLiteral("Q"), 0, 1).value(QStringLiteral("ok")).toBool());
    QVERIFY(!backend.jog(QStringLiteral("X"), 9, 1).value(QStringLiteral("ok")).toBool());

    QVERIFY(backend.mdi(QStringLiteral("G0 X0")).value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("mdi:G0 X0"));
    QCOMPARE(backend.mdiHistory().first(), QStringLiteral("G0 X0"));
    QVERIFY(!backend.mdi(QStringLiteral("   ")).value(QStringLiteral("ok")).toBool());

    QVERIFY(backend.setOverride(QStringLiteral("feed"), 1).value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("setOverride:feed:70"));
    QVERIFY(backend.setOverride(QStringLiteral("spindle"), -1).value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("setOverride:spindle:80"));
    QVERIFY(backend.setOverride(QStringLiteral("rapid"), 1).value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("setOverride:rapid:25"));
    QVERIFY(backend.setOverride(QStringLiteral("feed"), 9).value(QStringLiteral("ok")).toBool() == false);

    QVERIFY(backend.toggle(QStringLiteral("spindle")).value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("setToggle:spindle:1"));
    QVERIFY(backend.toggle(QStringLiteral("singleBlock")).value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("setToggle:singleBlock:1"));
    QVERIFY(!backend.toggle(QStringLiteral("nope")).value(QStringLiteral("ok")).toBool());

    QVERIFY(backend.selectTool(2).value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("selectTool:2"));
    QVERIFY(backend.setToolLength(2, 42.5).value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("setToolLength:2:42.500"));

    QVERIFY(backend.setActiveOffset(2).value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("setActiveG5x:2"));
    QVERIFY(!backend.setActiveOffset(9).value(QStringLiteral("ok")).toBool());
    QVERIFY(backend.zeroOffset(1).value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("zeroG5x:1"));
    QVERIFY(backend.touchOff(1, QStringLiteral("Z")).value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("touchOffG5x:1:2"));
    QVERIFY(!backend.touchOff(1, QStringLiteral("Q")).value(QStringLiteral("ok")).toBool());

    QVERIFY(backend.acknowledgeAlarm(1).value(QStringLiteral("ok")).toBool());
    QCOMPARE(link.called.last(), QStringLiteral("setToggle:acknowledgeAlarm:1:1"));
}

void TestLinuxCnc::refusalsCarryTheMachineReason()
{
    FakeLink link;
    LinuxCncBackend backend(&link);

    link.commandOk = false;
    link.errorText = QStringLiteral("Станок занят");
    QVERIFY(!backend.cycleStart().value(QStringLiteral("ok")).toBool());
    QCOMPARE(backend.cycleStart().value(QStringLiteral("reason")).toString(), QStringLiteral("Станок занят"));

    // A refusal without a link error still gets a readable default.
    link.errorText.clear();
    QVERIFY(!backend.cycleStart().value(QStringLiteral("ok")).toBool());
    QCOMPARE(backend.cycleStart().value(QStringLiteral("reason")).toString(),
             QStringLiteral("Станок не принял ПУСК"));
}

void TestLinuxCnc::gapsAreHonest()
{
    FakeLink link;
    LinuxCncBackend backend(&link);

    // Everything the contract asks for but stat cannot answer is refused or left empty, and the
    // gap list lives in docs/LINUXCNC-BACKEND.md — nothing is invented.
    const QVariantMap life = backend.setToolLife(1, 50);
    QVERIFY(!life.value(QStringLiteral("ok")).toBool());
    QVERIFY(life.value(QStringLiteral("reason")).toString().contains(QStringLiteral("не хранится")));
    const QVariantMap counted = backend.countPart();
    QVERIFY(!counted.value(QStringLiteral("ok")).toBool());
    QVERIFY(counted.value(QStringLiteral("reason")).toString().contains(QStringLiteral("не хранится")));

    QCOMPARE(backend.load(), 0);
    QCOMPARE(backend.axisLoad().value(QStringLiteral("X")).toInt(), 0);
    QCOMPARE(backend.cycleTime(), 0);
    QCOMPARE(backend.lastCycle(), 0);
    QCOMPARE(backend.partCount(), 0);
    QVERIFY(!backend.probe().value(QStringLiteral("result")).toBool());

    // The workplace is a panel concept: the machine accepts the call and keeps its own mode.
    QVERIFY(backend.setWorkplace(QStringLiteral("SETUP")).value(QStringLiteral("ok")).toBool());
}

void TestLinuxCnc::pollingSignalsAChange()
{
    FakeLink link;
    LinuxCncBackend backend(&link);
    QSignalSpy positionSpy(&backend, &LinuxCncBackend::positionChanged);
    QSignalSpy runSpy(&backend, &LinuxCncBackend::runChanged);
    QSignalSpy connectionSpy(&backend, &LinuxCncBackend::connectionChanged);
    QSignalSpy alarmsSpy(&backend, &LinuxCncBackend::alarmsChanged);

    // The adapter polls the link; a real change reaches the panel as a signal, and a quiet link
    // stays quiet.
    link.joints[0] = 15.0;
    link.taskStateCode = 2;
    link.messages = {QStringLiteral("2051 Ресурс инструмента")};
    QTRY_VERIFY_WITH_TIMEOUT(positionSpy.count() > 0, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(runSpy.count() > 0, 1000);
    QTRY_VERIFY_WITH_TIMEOUT(alarmsSpy.count() > 0, 1000);

    // The connection is reported once, not on every poll; a swallowed third alarm is not invented.
    QTRY_COMPARE_WITH_TIMEOUT(connectionSpy.count(), 1, 1000);
    QTest::qWait(250);
    QCOMPARE(connectionSpy.count(), 1);
    QCOMPARE(backend.alarms().size(), 1);
}

QTEST_GUILESS_MAIN(TestLinuxCnc)

#include "tst_linuxcnc.moc"
