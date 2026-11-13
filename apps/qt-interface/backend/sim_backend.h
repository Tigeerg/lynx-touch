#pragma once

#include "machine_backend.h"
#include "sim_state.h"

#include <QTime>
#include <QTimer>

// Deterministic stand-in for the machine: fixtures describe a state, commands mutate it through
// the same guards the real backend will use. It is a permanent demo/test backend, not a hidden
// branch inside the panel.
class SimBackend : public MachineBackend
{
    Q_OBJECT

public:
    explicit SimBackend(QObject *parent = nullptr);
    ~SimBackend() override;

    QString backendName() const override;
    bool connected() const override;
    QString lastError() const override;

    QString run() const override;
    QString mode() const override;
    QString workplace() const override;
    QVariantMap position() const override;
    QVariantMap machinePosition() const override;
    QVariantList offsets() const override;
    int activeOffset() const override;
    QVariantList tools() const override;
    int tool() const override;
    QVariantMap toolTime() const override;
    QVariantList programs() const override;
    QVariantMap program() const override;
    QVariantList alarms() const override;
    QVariantMap probe() const override;
    QVariantMap overrides() const override;
    QVariantList rapidSteps() const override;
    bool spindle() const override;
    bool coolant() const override;
    int load() const override;
    QVariantMap axisLoad() const override;
    int cycleTime() const override;
    int lastCycle() const override;
    int partCount() const override;
    bool singleBlock() const override;
    bool optStop() const override;
    bool dryRun() const override;
    QString message() const override;
    QString clock() const override;
    bool focused() const override;
    QStringList mdiHistory() const override;
    void setFocused(bool focused) override;

    QVariantMap cycleStart() override;
    QVariantMap feedHold() override;
    QVariantMap reset() override;
    QVariantMap emergencyStop() override;
    QVariantMap setWorkplace(const QString &workplace) override;
    QVariantMap setMode(const QString &mode) override;
    QVariantMap jog(const QString &axis, int step, int direction) override;
    QVariantMap mdi(const QString &text) override;
    QVariantMap loadProgram(int index) override;
    QVariantMap startFromCursor() override;
    QVariantMap setOverride(const QString &kind, int direction) override;
    QVariantMap toggle(const QString &key) override;
    QVariantMap acknowledgeAlarms() override;
    QVariantMap clearAcknowledgedAlarms() override;
    QVariantMap setActiveOffset(int index) override;
    QVariantMap zeroOffset(int index) override;
    QVariantMap touchOff(int index, const QString &axis) override;
    QVariantMap setToolLife(int number, int life) override;
    QVariantMap setToolLength(int number, double length) override;
    QVariantMap selectTool(int number) override;
    QVariantMap acknowledgeAlarm(int index) override;
    QVariantMap countPart() override;

    Q_INVOKABLE bool loadFixture(const QString &path);
    Q_INVOKABLE bool setFrozenClock(const QString &clock);

private:
    void tick();
    void emitAll();
    void setMessage(const QString &text);
    void advanceLine();
    void applyMdi(const QString &text);

    SimState m_state;
    QString m_lastError;
    QTimer m_tickTimer;
    QTime m_clockTime;
    int m_ticks = 0;
    int m_ticksOnLine = 0;
};
