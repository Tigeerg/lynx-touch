#include "sim_backend.h"

#include <QCoreApplication>
#include <QDir>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QtTest>

namespace {

QString fixturePath(const QString &name)
{
    return QDir(QCoreApplication::applicationDirPath())
        .absoluteFilePath(QStringLiteral("../../apps/qt-interface/backend/fixtures/") + name);
}

} // namespace

class TestBackend : public QObject
{
    Q_OBJECT

private slots:
    void loadsAutoFixture();
    void badFixtureKeepsState();
    void workplaceGuard();
    void jogGuardAndStep();
    void cycleLifecycle();
    void overrides();
    void alarms();
    void fixturesSwitch();
    void clockFreeze();
    void programLoad();
    void offsetCommands();
    void toolCommands();
    void alarmAcknowledgeSingle();
    void counterCommands();
};

void TestBackend::loadsAutoFixture()
{
    SimBackend backend;
    QVERIFY2(backend.loadFixture(fixturePath(QStringLiteral("auto-idle.json"))),
             qPrintable(backend.lastError()));

    QCOMPARE(backend.backendName(), QStringLiteral("sim"));
    QVERIFY(backend.connected());
    QCOMPARE(backend.run(), QStringLiteral("IDLE"));
    QCOMPARE(backend.mode(), QStringLiteral("AUTO"));
    QCOMPARE(backend.workplace(), QStringLiteral("RUN"));
    QCOMPARE(backend.activeOffset(), 0);
    QCOMPARE(backend.tool(), 1);
    QCOMPARE(backend.position().value(QStringLiteral("x")).toDouble(), -120.5);
    QCOMPARE(backend.offsets().size(), 6);
    QCOMPARE(backend.tools().size(), 9);
    QCOMPARE(backend.alarms().size(), 3);
    QCOMPARE(backend.programs().size(), 4);
    QCOMPARE(backend.program().value(QStringLiteral("name")).toString(), QStringLiteral("O1024"));
    QCOMPARE(backend.programs().at(0).toMap().value(QStringLiteral("modified")).toString(),
             QStringLiteral("08.02.2026 14:20"));
    QCOMPARE(backend.program().value(QStringLiteral("line")).toInt(), 0);
    QCOMPARE(backend.overrides().value(QStringLiteral("feed")).toInt(), 60);
    QCOMPARE(backend.overrides().value(QStringLiteral("spindle")).toInt(), 90);
    QCOMPARE(backend.overrides().value(QStringLiteral("rapid")).toInt(), 100);
    QCOMPARE(backend.load(), 38);
    QCOMPARE(backend.partCount(), 7);
    QCOMPARE(backend.clock(), QStringLiteral("10:45:00"));
}

void TestBackend::badFixtureKeepsState()
{
    SimBackend backend;
    QVERIFY(backend.loadFixture(fixturePath(QStringLiteral("auto-idle.json"))));
    QVERIFY(!backend.loadFixture(fixturePath(QStringLiteral("missing.json"))));
    QVERIFY(backend.lastError().contains(QStringLiteral("cannot open")));
    QCOMPARE(backend.run(), QStringLiteral("IDLE"));
    QCOMPARE(backend.tool(), 1);

    QTemporaryFile broken;
    QVERIFY(broken.open());
    broken.write("{\"run\":\"IDLE\"}");
    broken.flush();
    QVERIFY(!backend.loadFixture(broken.fileName()));
    QVERIFY(!backend.lastError().isEmpty());
    QCOMPARE(backend.tool(), 1);
}

void TestBackend::workplaceGuard()
{
    SimBackend backend;
    QVERIFY(backend.loadFixture(fixturePath(QStringLiteral("auto-idle.json"))));

    const QVariantMap start = backend.cycleStart();
    QVERIFY(start.value(QStringLiteral("ok")).toBool());
    const QVariantMap refused = backend.setWorkplace(QStringLiteral("SETUP"));
    QVERIFY(!refused.value(QStringLiteral("ok")).toBool());
    QVERIFY(refused.value(QStringLiteral("reason")).toString().contains(QStringLiteral("занят")));

    QVERIFY(backend.reset().value(QStringLiteral("ok")).toBool());
    const QVariantMap accepted = backend.setWorkplace(QStringLiteral("SETUP"));
    QVERIFY(accepted.value(QStringLiteral("ok")).toBool());
    QCOMPARE(backend.workplace(), QStringLiteral("SETUP"));
    QCOMPARE(backend.mode(), QStringLiteral("JOG"));
}

void TestBackend::jogGuardAndStep()
{
    SimBackend backend;
    QVERIFY(backend.loadFixture(fixturePath(QStringLiteral("setup-probe.json"))));
    const double start = backend.position().value(QStringLiteral("x")).toDouble();

    QVERIFY(backend.jog(QStringLiteral("X"), 2, 1).value(QStringLiteral("ok")).toBool());
    QVERIFY(qAbs(backend.position().value(QStringLiteral("x")).toDouble() - (start + 0.1)) < 1e-9);
    QVERIFY(backend.jog(QStringLiteral("X"), 0, -1).value(QStringLiteral("ok")).toBool());
    QVERIFY(qAbs(backend.position().value(QStringLiteral("x")).toDouble() - (start + 0.099)) < 1e-9);
    QVERIFY(!backend.jog(QStringLiteral("Q"), 0, 1).value(QStringLiteral("ok")).toBool());

    QVERIFY(backend.loadFixture(fixturePath(QStringLiteral("auto-idle.json"))));
    const QVariantMap refused = backend.jog(QStringLiteral("X"), 0, 1);
    QVERIFY(!refused.value(QStringLiteral("ok")).toBool());
    QVERIFY(refused.value(QStringLiteral("reason")).toString().contains(QStringLiteral("JOG")));
}

void TestBackend::cycleLifecycle()
{
    SimBackend backend;
    QVERIFY(backend.loadFixture(fixturePath(QStringLiteral("test-dry.json"))));
    QSignalSpy runSpy(&backend, &SimBackend::runChanged);

    QVERIFY(backend.reset().value(QStringLiteral("ok")).toBool());
    QCOMPARE(backend.program().value(QStringLiteral("line")).toInt(), 0);
    QVERIFY(backend.cycleStart().value(QStringLiteral("ok")).toBool());
    QCOMPARE(backend.run(), QStringLiteral("RUN"));
    QVERIFY(backend.feedHold().value(QStringLiteral("ok")).toBool());
    QCOMPARE(backend.run(), QStringLiteral("HOLD"));
    QVERIFY(backend.cycleStart().value(QStringLiteral("ok")).toBool());
    QCOMPARE(backend.run(), QStringLiteral("RUN"));
    QVERIFY(backend.reset().value(QStringLiteral("ok")).toBool());
    QCOMPARE(backend.run(), QStringLiteral("IDLE"));
    QVERIFY(runSpy.count() >= 4);
}

void TestBackend::overrides()
{
    SimBackend backend;
    QVERIFY(backend.loadFixture(fixturePath(QStringLiteral("auto-idle.json"))));

    QVERIFY(backend.setOverride(QStringLiteral("feed"), 1).value(QStringLiteral("ok")).toBool());
    QCOMPARE(backend.overrides().value(QStringLiteral("feed")).toInt(), 70);
    for (int i = 0; i < 20; ++i)
        backend.setOverride(QStringLiteral("feed"), 1);
    QCOMPARE(backend.overrides().value(QStringLiteral("feed")).toInt(), 150);

    QCOMPARE(backend.overrides().value(QStringLiteral("rapid")).toInt(), 100);
    backend.setOverride(QStringLiteral("rapid"), 1);
    QCOMPARE(backend.overrides().value(QStringLiteral("rapid")).toInt(), 100);
    backend.setOverride(QStringLiteral("rapid"), -1);
    QCOMPARE(backend.overrides().value(QStringLiteral("rapid")).toInt(), 50);
    backend.setOverride(QStringLiteral("rapid"), -1);
    QCOMPARE(backend.overrides().value(QStringLiteral("rapid")).toInt(), 25);
    QVERIFY(!backend.setOverride(QStringLiteral("unknown"), 1).value(QStringLiteral("ok")).toBool());
}

void TestBackend::alarms()
{
    SimBackend backend;
    QVERIFY(backend.loadFixture(fixturePath(QStringLiteral("auto-idle.json"))));
    QCOMPARE(backend.alarms().size(), 3);

    QVERIFY(backend.acknowledgeAlarms().value(QStringLiteral("ok")).toBool());
    for (const QVariant &value : backend.alarms())
        QVERIFY(!value.toMap().value(QStringLiteral("active")).toBool());

    QVERIFY(backend.clearAcknowledgedAlarms().value(QStringLiteral("ok")).toBool());
    QCOMPARE(backend.alarms().size(), 0);
}

void TestBackend::fixturesSwitch()
{
    SimBackend backend;
    QVERIFY(backend.loadFixture(fixturePath(QStringLiteral("auto-idle.json"))));
    QCOMPARE(backend.tool(), 1);
    QVERIFY(backend.loadFixture(fixturePath(QStringLiteral("setup-probe.json"))));
    QCOMPARE(backend.workplace(), QStringLiteral("SETUP"));
    QCOMPARE(backend.tool(), 99);
    QVERIFY(backend.probe().value(QStringLiteral("result")).toBool());
    QCOMPARE(backend.alarms().size(), 0);
}

void TestBackend::clockFreeze()
{
    SimBackend backend;
    QVERIFY(backend.loadFixture(fixturePath(QStringLiteral("auto-idle.json"))));
    QCOMPARE(backend.clock(), QStringLiteral("10:45:00"));
    QVERIFY(backend.setFrozenClock(QStringLiteral("11:00:00")));
    QCOMPARE(backend.clock(), QStringLiteral("11:00:00"));
    QVERIFY(!backend.setFrozenClock(QStringLiteral("25:00:00")));
    QVERIFY(backend.lastError().contains(QStringLiteral("clock")));
    QCOMPARE(backend.clock(), QStringLiteral("11:00:00"));
}

void TestBackend::programLoad()
{
    SimBackend backend;
    QVERIFY(backend.loadFixture(fixturePath(QStringLiteral("auto-idle.json"))));
    QCOMPARE(backend.program().value(QStringLiteral("name")).toString(), QStringLiteral("O1024"));
    QVERIFY(backend.loadProgram(1).value(QStringLiteral("ok")).toBool());
    QCOMPARE(backend.program().value(QStringLiteral("name")).toString(), QStringLiteral("O2048"));
    QVERIFY(!backend.loadProgram(99).value(QStringLiteral("ok")).toBool());
}

void TestBackend::offsetCommands()
{
    SimBackend backend;
    QVERIFY(backend.loadFixture(fixturePath(QStringLiteral("auto-idle.json"))));
    QCOMPARE(backend.activeOffset(), 0);

    // Activating a work offset moves the active flag to it.
    QVERIFY(backend.setActiveOffset(1).value(QStringLiteral("ok")).toBool());
    QCOMPARE(backend.activeOffset(), 1);
    QVERIFY(backend.offsets().at(1).toMap().value(QStringLiteral("active")).toBool());
    QVERIFY(!backend.offsets().at(0).toMap().value(QStringLiteral("active")).toBool());

    // Clearing an offset keeps the list and the active index, but zeroes the values.
    QVERIFY(backend.zeroOffset(1).value(QStringLiteral("ok")).toBool());
    const QVariantMap zeroed = backend.offsets().at(1).toMap().value(QStringLiteral("values")).toMap();
    QCOMPARE(zeroed.value(QStringLiteral("x")).toDouble(), 0.0);
    QCOMPARE(zeroed.value(QStringLiteral("z")).toDouble(), 0.0);

    // Touch off writes the current position into the chosen axis only.
    const double z = backend.position().value(QStringLiteral("z")).toDouble();
    QVERIFY(backend.touchOff(1, QStringLiteral("Z")).value(QStringLiteral("ok")).toBool());
    const QVariantMap touched = backend.offsets().at(1).toMap().value(QStringLiteral("values")).toMap();
    QCOMPARE(touched.value(QStringLiteral("z")).toDouble(), z);
    QVERIFY(!backend.touchOff(1, QStringLiteral("Q")).value(QStringLiteral("ok")).toBool());
    QVERIFY(!backend.setActiveOffset(99).value(QStringLiteral("ok")).toBool());

    // A running program owns the offsets.
    QVERIFY(backend.cycleStart().value(QStringLiteral("ok")).toBool());
    QVERIFY(!backend.setActiveOffset(2).value(QStringLiteral("ok")).toBool());
    QVERIFY(!backend.touchOff(2, QStringLiteral("X")).value(QStringLiteral("ok")).toBool());
    QVERIFY(backend.reset().value(QStringLiteral("ok")).toBool());
    QVERIFY(backend.setActiveOffset(2).value(QStringLiteral("ok")).toBool());
}

void TestBackend::toolCommands()
{
    SimBackend backend;
    QVERIFY(backend.loadFixture(fixturePath(QStringLiteral("auto-idle.json"))));

    QVERIFY(backend.setToolLife(1, 0).value(QStringLiteral("ok")).toBool());
    QCOMPARE(backend.tools().at(0).toMap().value(QStringLiteral("life")).toInt(), 0);
    // The machine clamps the resource to 0…100.
    QVERIFY(backend.setToolLife(1, 500).value(QStringLiteral("ok")).toBool());
    QCOMPARE(backend.tools().at(0).toMap().value(QStringLiteral("life")).toInt(), 100);

    QVERIFY(backend.setToolLength(2, 42.5).value(QStringLiteral("ok")).toBool());
    QCOMPARE(backend.tools().at(1).toMap().value(QStringLiteral("length")).toDouble(), 42.5);

    // Selecting a tool puts it in the spindle and no other.
    QVERIFY(backend.selectTool(99).value(QStringLiteral("ok")).toBool());
    QCOMPARE(backend.tool(), 99);
    for (const QVariant &value : backend.tools()) {
        const QVariantMap tool = value.toMap();
        QCOMPARE(tool.value(QStringLiteral("inSpindle")).toBool(),
                 tool.value(QStringLiteral("number")).toInt() == 99);
    }

    QVERIFY(!backend.setToolLife(77, 10).value(QStringLiteral("ok")).toBool());
    QVERIFY(!backend.setToolLength(77, 10.0).value(QStringLiteral("ok")).toBool());
    QVERIFY(!backend.selectTool(77).value(QStringLiteral("ok")).toBool());
}

void TestBackend::alarmAcknowledgeSingle()
{
    SimBackend backend;
    QVERIFY(backend.loadFixture(fixturePath(QStringLiteral("auto-idle.json"))));

    // One message at a time: the panel acknowledges the row under the cursor, not the whole log.
    QVERIFY(backend.acknowledgeAlarm(0).value(QStringLiteral("ok")).toBool());
    QVERIFY(!backend.alarms().at(0).toMap().value(QStringLiteral("active")).toBool());
    QVERIFY(backend.alarms().at(1).toMap().value(QStringLiteral("active")).toBool());
    QVERIFY(!backend.acknowledgeAlarm(99).value(QStringLiteral("ok")).toBool());
}

void TestBackend::counterCommands()
{
    SimBackend backend;
    QVERIFY(backend.loadFixture(fixturePath(QStringLiteral("auto-idle.json"))));
    QCOMPARE(backend.partCount(), 7);

    // The counter belongs to the machine and counts even while a program runs.
    QVERIFY(backend.countPart().value(QStringLiteral("ok")).toBool());
    QCOMPARE(backend.partCount(), 8);
    QVERIFY(backend.cycleStart().value(QStringLiteral("ok")).toBool());
    QVERIFY(backend.countPart().value(QStringLiteral("ok")).toBool());
    QCOMPARE(backend.partCount(), 9);

    // The cycle clock is the machine's too: zero before the run, and the finished time lands in
    // the previous-cycle counter when the program reaches its end.
    QCOMPARE(backend.lastCycle(), 198000);
}

QTEST_GUILESS_MAIN(TestBackend)

#include "tst_backend.moc"
