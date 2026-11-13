#pragma once

#include "machine_backend.h"

#include <QPointer>
#include <QTimer>

class LinuxCncLink;

// The adapter between the panel contract and LinuxCNC. It owns no machine state: it reads the link
// (raw stat/command facts) and maps them onto the contract, and it sends every command back through
// the link. That mapping is the part worth testing, and it is tested with a fake link, so it can be
// written and checked on a machine that has no LinuxCNC installed.
//
// Deliberate gaps, recorded in docs/LINUXCNC-BACKEND.md: `workplace` (SETUP/TEST/RUN) is a panel
// concept, LinuxCNC has no such state; tool life, the finished-part counter, the cycle clocks, the
// per-axis load, the probe and the alarm levels have no counterpart in stat and stay empty until
// the stand tells us where to take them from.
class LinuxCncBackend : public MachineBackend
{
    Q_OBJECT

public:
    explicit LinuxCncBackend(LinuxCncLink *link, QObject *parent = nullptr);
    ~LinuxCncBackend() override;

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
    QVariantMap acknowledgeAlarm(int index) override;
    QVariantMap setActiveOffset(int index) override;
    QVariantMap zeroOffset(int index) override;
    QVariantMap touchOff(int index, const QString &axis) override;
    QVariantMap setToolLife(int number, int life) override;
    QVariantMap setToolLength(int number, double length) override;
    QVariantMap selectTool(int number) override;
    QVariantMap countPart() override;

private:
    void poll();
    QVariantMap resultOrReason(bool ok, const QString &reason);

    // Not owned: the factory parents the link to this backend, a test keeps its own instance.
    QPointer<LinuxCncLink> m_link;
    QTimer m_pollTimer;
    // The last values the panel was told about, so a poll emits a signal only on a real change.
    bool m_connected = false;
    QString m_run;
    QString m_mode;
    QVariantMap m_position;
    QVariantList m_offsets;
    QVariantList m_tools;
    QVariantList m_alarms;
    QVariantMap m_program;
    QVariantMap m_overrides;
    bool m_spindle = false;
    bool m_coolant = false;
    bool m_singleBlock = false;
    bool m_optStop = false;
    bool m_dryRun = false;
    QString m_message;
    QString m_clock;
    bool m_focused = false;
    QStringList m_mdiHistory;
};

// The real link, built only with -DLYNX_WITH_LINUXCNC=ON (CMake option documented in
// docs/LINUXCNC-BACKEND.md). Without the option there is no link to build and the factory says so
// instead of falling back to the simulator.
LinuxCncLink *createLinuxCncLink(QString *error);
