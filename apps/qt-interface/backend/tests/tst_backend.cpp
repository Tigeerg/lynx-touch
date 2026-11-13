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
    QCOMPARE(backend.tools().size(), 4);
    QCOMPARE(backend.alarms().size(), 3);
    QCOMPARE(backend.programs().size(), 2);
    QCOMPARE(backend.program().value(QStringLiteral("name")).toString(), QStringLiteral("O1024"));
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

QTEST_GUILESS_MAIN(TestBackend)

#include "tst_backend.moc"
