#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// The raw, LinuxCNC-shaped facts the adapter reads. Nothing here is a contract type: joints and
// g5x numbers, task and interpreter state codes, tool-table fields and the operator message list
// are exactly what `linuxcnc.stat` exposes, so the real link talks to the library without a second
// translation layer and a test can feed the same shape from a fake.
//
// The real implementation (`LinuxCncNmlLink`, built only with -DLYNX_WITH_LINUXCNC=ON on the
// controller) is written against the stand's headers; the mapping from these facts to the panel
// contract lives in `LinuxCncBackend` and is covered by tests on any platform.
// It is a QObject so the link can be parented to the backend that uses it: the factory creates the
// link and hands its lifetime to the backend, while a test may keep a plain stack instance.
class LinuxCncLink : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~LinuxCncLink() override = default;

    // --- connection and errors ---------------------------------------------------------------
    virtual bool connected() const = 0;
    virtual QString lastError() const = 0;

    // --- task and interpreter state (raw codes, see the mapping in LinuxCncBackend) -----------
    // taskState: 1 done, 2 exec, 3 error. interpreterState: 1 idle, 2 reading, 3 paused, 4 waiting.
    // taskMode: 1 manual, 2 auto, 3 mdi.
    virtual int taskState() const = 0;
    virtual int interpreterState() const = 0;
    virtual int taskMode() const = 0;
    virtual bool estop() const = 0;

    // --- geometry ---------------------------------------------------------------------------
    // Joints: 0..2 are X, Y, Z, joint 3 is A. Position is the work position, machinePosition the
    // raw joint position; the adapter subtracts the active g5x offset for the panel DRO.
    virtual double jointPosition(int joint) const = 0;
    virtual double g5xOffset(int index, int joint) const = 0;   // index 0..5 for G54..G59
    virtual int activeG5xIndex() const = 0;
    virtual int jointCount() const = 0;

    // --- tool table -------------------------------------------------------------------------
    virtual int toolCount() const = 0;
    virtual int toolNumber(int row) const = 0;
    virtual QString toolName(int row) const = 0;
    virtual double toolDiameter(int row) const = 0;
    virtual double toolLength(int row) const = 0;
    virtual int toolInSpindle() const = 0;

    // --- station ----------------------------------------------------------------------------
    virtual bool spindle() const = 0;
    virtual bool coolant() const = 0;
    virtual int feedOverride() const = 0;
    virtual int spindleOverride() const = 0;
    virtual int rapidOverrideIndex() const = 0;   // 0..3 for the 5/25/50/100 row
    virtual bool singleBlock() const = 0;
    virtual bool optStop() const = 0;
    virtual bool dryRun() const = 0;

    // --- messages and program ---------------------------------------------------------------
    virtual QStringList operatorMessages() const = 0;   // one line per message, "code text"
    virtual QString programFile() const = 0;
    virtual QStringList programLines() const = 0;
    virtual int currentLine() const = 0;
    virtual int cursorLine() const = 0;
    virtual QString lastMessage() const = 0;

    // --- commands (the machine answers `false` when it refuses) -----------------------------
    virtual bool cycleStart() = 0;
    virtual bool feedHold() = 0;
    virtual bool reset() = 0;
    virtual bool setEstop(bool on) = 0;
    virtual bool setTaskMode(int mode) = 0;
    virtual bool jogJoint(int joint, double delta) = 0;
    virtual bool runMdiLine(const QString &line) = 0;
    virtual bool openProgram(const QString &path) = 0;
    virtual bool setOverride(const QString &kind, int percent) = 0;
    virtual bool setToggle(const QString &key, bool on) = 0;
    virtual bool selectTool(int number) = 0;
    virtual bool setActiveG5x(int index) = 0;
    virtual bool zeroG5x(int index) = 0;
    virtual bool touchOffG5x(int index, int joint) = 0;
    virtual bool setToolLength(int number, double length) = 0;
};
