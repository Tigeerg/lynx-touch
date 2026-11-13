#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// The one surface the QML panel knows. `SimBackend` implements it for tests and the demo,
// `LinuxCncBackend` will implement it on the machine; the screens never branch on the source.
// A missing real backend is an error, not a silent fallback to the simulator.
class MachineBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString backendName READ backendName CONSTANT)
    Q_PROPERTY(bool connected READ connected NOTIFY connectionChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorChanged)

    Q_PROPERTY(QString run READ run NOTIFY runChanged)
    Q_PROPERTY(QString mode READ mode NOTIFY modeChanged)
    Q_PROPERTY(QString workplace READ workplace NOTIFY workplaceChanged)
    Q_PROPERTY(QVariantMap position READ position NOTIFY positionChanged)
    Q_PROPERTY(QVariantMap machinePosition READ machinePosition NOTIFY positionChanged)
    Q_PROPERTY(QVariantList offsets READ offsets NOTIFY offsetsChanged)
    Q_PROPERTY(int activeOffset READ activeOffset NOTIFY offsetsChanged)
    Q_PROPERTY(QVariantList tools READ tools NOTIFY toolsChanged)
    Q_PROPERTY(int tool READ tool NOTIFY toolsChanged)
    Q_PROPERTY(QVariantMap toolTime READ toolTime NOTIFY toolsChanged)
    Q_PROPERTY(QVariantList programs READ programs NOTIFY programsChanged)
    Q_PROPERTY(QVariantMap program READ program NOTIFY programChanged)
    Q_PROPERTY(QVariantList alarms READ alarms NOTIFY alarmsChanged)
    Q_PROPERTY(QVariantMap probe READ probe NOTIFY probeChanged)
    Q_PROPERTY(QVariantMap overrides READ overrides NOTIFY overridesChanged)
    Q_PROPERTY(QVariantList rapidSteps READ rapidSteps CONSTANT)
    Q_PROPERTY(bool spindle READ spindle NOTIFY spindleChanged)
    Q_PROPERTY(bool coolant READ coolant NOTIFY coolantChanged)
    Q_PROPERTY(int load READ load NOTIFY loadChanged)
    Q_PROPERTY(QVariantMap axisLoad READ axisLoad NOTIFY loadChanged)
    Q_PROPERTY(int cycleTime READ cycleTime NOTIFY countersChanged)
    Q_PROPERTY(int lastCycle READ lastCycle NOTIFY countersChanged)
    Q_PROPERTY(int partCount READ partCount NOTIFY countersChanged)
    Q_PROPERTY(bool singleBlock READ singleBlock NOTIFY flagsChanged)
    Q_PROPERTY(bool optStop READ optStop NOTIFY flagsChanged)
    Q_PROPERTY(bool dryRun READ dryRun NOTIFY flagsChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(QString clock READ clock NOTIFY clockChanged)
    Q_PROPERTY(bool focused READ focused WRITE setFocused NOTIFY focusedChanged)
    Q_PROPERTY(QStringList mdiHistory READ mdiHistory NOTIFY mdiHistoryChanged)

public:
    explicit MachineBackend(QObject *parent = nullptr);
    ~MachineBackend() override;

    virtual QString backendName() const = 0;
    virtual bool connected() const;
    virtual QString lastError() const;

    virtual QString run() const;
    virtual QString mode() const;
    virtual QString workplace() const;
    virtual QVariantMap position() const;
    virtual QVariantMap machinePosition() const;
    virtual QVariantList offsets() const;
    virtual int activeOffset() const;
    virtual QVariantList tools() const;
    virtual int tool() const;
    virtual QVariantMap toolTime() const;
    virtual QVariantList programs() const;
    virtual QVariantMap program() const;
    virtual QVariantList alarms() const;
    virtual QVariantMap probe() const;
    virtual QVariantMap overrides() const;
    virtual QVariantList rapidSteps() const;
    virtual bool spindle() const;
    virtual bool coolant() const;
    virtual int load() const;
    virtual QVariantMap axisLoad() const;
    virtual int cycleTime() const;
    virtual int lastCycle() const;
    virtual int partCount() const;
    virtual bool singleBlock() const;
    virtual bool optStop() const;
    virtual bool dryRun() const;
    virtual QString message() const;
    virtual QString clock() const;
    virtual bool focused() const;
    virtual QStringList mdiHistory() const;
    virtual void setFocused(bool focused);

    Q_INVOKABLE virtual QVariantMap cycleStart();
    Q_INVOKABLE virtual QVariantMap feedHold();
    Q_INVOKABLE virtual QVariantMap reset();
    Q_INVOKABLE virtual QVariantMap emergencyStop();
    Q_INVOKABLE virtual QVariantMap setWorkplace(const QString &workplace);
    Q_INVOKABLE virtual QVariantMap setMode(const QString &mode);
    Q_INVOKABLE virtual QVariantMap jog(const QString &axis, int step, int direction);
    Q_INVOKABLE virtual QVariantMap mdi(const QString &text);
    Q_INVOKABLE virtual QVariantMap loadProgram(int index);
    Q_INVOKABLE virtual QVariantMap startFromCursor();
    Q_INVOKABLE virtual QVariantMap setOverride(const QString &kind, int direction);
    Q_INVOKABLE virtual QVariantMap toggle(const QString &key);
    Q_INVOKABLE virtual QVariantMap acknowledgeAlarms();
    Q_INVOKABLE virtual QVariantMap clearAcknowledgedAlarms();
    Q_INVOKABLE virtual QVariantMap setActiveOffset(int index);
    Q_INVOKABLE virtual QVariantMap zeroOffset(int index);
    Q_INVOKABLE virtual QVariantMap touchOff(int index, const QString &axis);
    Q_INVOKABLE virtual QVariantMap setToolLife(int number, int life);
    Q_INVOKABLE virtual QVariantMap setToolLength(int number, double length);
    Q_INVOKABLE virtual QVariantMap selectTool(int number);
    Q_INVOKABLE virtual QVariantMap acknowledgeAlarm(int index);

signals:
    void connectionChanged();
    void errorChanged();
    void runChanged();
    void modeChanged();
    void workplaceChanged();
    void positionChanged();
    void offsetsChanged();
    void toolsChanged();
    void programsChanged();
    void programChanged();
    void alarmsChanged();
    void probeChanged();
    void overridesChanged();
    void spindleChanged();
    void coolantChanged();
    void loadChanged();
    void countersChanged();
    void flagsChanged();
    void messageChanged();
    void clockChanged();
    void focusedChanged();
    void mdiHistoryChanged();
};
