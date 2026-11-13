#include "machine_backend.h"

#include "machine_types.h"

MachineBackend::MachineBackend(QObject *parent)
    : QObject(parent)
{
}

MachineBackend::~MachineBackend() = default;

bool MachineBackend::connected() const
{
    return false;
}

QString MachineBackend::lastError() const
{
    return {};
}

QString MachineBackend::run() const
{
    return QStringLiteral("IDLE");
}

QString MachineBackend::mode() const
{
    return QStringLiteral("AUTO");
}

QString MachineBackend::workplace() const
{
    return QStringLiteral("RUN");
}

QVariantMap MachineBackend::position() const
{
    return Position().toMap();
}

QVariantMap MachineBackend::machinePosition() const
{
    return Position().toMap();
}

QVariantList MachineBackend::offsets() const
{
    return {};
}

int MachineBackend::activeOffset() const
{
    return 0;
}

QVariantList MachineBackend::tools() const
{
    return {};
}

int MachineBackend::tool() const
{
    return 0;
}

QVariantMap MachineBackend::toolTime() const
{
    return {};
}

QVariantList MachineBackend::programs() const
{
    return {};
}

QVariantMap MachineBackend::program() const
{
    return ProgramFile().toMap();
}

QVariantList MachineBackend::alarms() const
{
    return {};
}

QVariantMap MachineBackend::probe() const
{
    // No backend means no probe data: the simulator and the machine adapter fill this in.
    return {{QStringLiteral("tool"), 0},
            {QStringLiteral("running"), false},
            {QStringLiteral("result"), false},
            {QStringLiteral("step"), 0}};
}

QVariantMap MachineBackend::overrides() const
{
    return Overrides().toMap();
}

QVariantList MachineBackend::rapidSteps() const
{
    // The same four steps the React panel and the QML rapid row use.
    return {5, 25, 50, 100};
}

bool MachineBackend::spindle() const
{
    return false;
}

bool MachineBackend::coolant() const
{
    return false;
}

int MachineBackend::load() const
{
    return 0;
}

QVariantMap MachineBackend::axisLoad() const
{
    return {{QStringLiteral("X"), 0}, {QStringLiteral("Y"), 0},
            {QStringLiteral("Z"), 0}, {QStringLiteral("A"), 0}};
}

int MachineBackend::cycleTime() const
{
    return 0;
}

int MachineBackend::lastCycle() const
{
    return 0;
}

int MachineBackend::partCount() const
{
    return 0;
}

bool MachineBackend::singleBlock() const
{
    return false;
}

bool MachineBackend::optStop() const
{
    return false;
}

bool MachineBackend::dryRun() const
{
    return false;
}

QString MachineBackend::message() const
{
    return {};
}

QString MachineBackend::clock() const
{
    return QStringLiteral("--:--:--");
}

bool MachineBackend::focused() const
{
    return false;
}

QStringList MachineBackend::mdiHistory() const
{
    return {};
}

void MachineBackend::setFocused(bool focused)
{
    Q_UNUSED(focused)
}

QVariantMap MachineBackend::cycleStart()
{
    return commandFailed(QStringLiteral("Backend не подключён"));
}

QVariantMap MachineBackend::feedHold()
{
    return commandFailed(QStringLiteral("Backend не подключён"));
}

QVariantMap MachineBackend::reset()
{
    return commandFailed(QStringLiteral("Backend не подключён"));
}

QVariantMap MachineBackend::emergencyStop()
{
    return commandFailed(QStringLiteral("Backend не подключён"));
}

QVariantMap MachineBackend::setWorkplace(const QString &workplace)
{
    Q_UNUSED(workplace)
    return commandFailed(QStringLiteral("Backend не подключён"));
}

QVariantMap MachineBackend::setMode(const QString &mode)
{
    Q_UNUSED(mode)
    return commandFailed(QStringLiteral("Backend не подключён"));
}

QVariantMap MachineBackend::jog(const QString &axis, int step, int direction)
{
    Q_UNUSED(axis)
    Q_UNUSED(step)
    Q_UNUSED(direction)
    return commandFailed(QStringLiteral("Backend не подключён"));
}

QVariantMap MachineBackend::mdi(const QString &text)
{
    Q_UNUSED(text)
    return commandFailed(QStringLiteral("Backend не подключён"));
}

QVariantMap MachineBackend::loadProgram(int index)
{
    Q_UNUSED(index)
    return commandFailed(QStringLiteral("Backend не подключён"));
}

QVariantMap MachineBackend::startFromCursor()
{
    return commandFailed(QStringLiteral("Backend не подключён"));
}

QVariantMap MachineBackend::setOverride(const QString &kind, int direction)
{
    Q_UNUSED(kind)
    Q_UNUSED(direction)
    return commandFailed(QStringLiteral("Backend не подключён"));
}

QVariantMap MachineBackend::toggle(const QString &key)
{
    Q_UNUSED(key)
    return commandFailed(QStringLiteral("Backend не подключён"));
}

QVariantMap MachineBackend::acknowledgeAlarms()
{
    return commandFailed(QStringLiteral("Backend не подключён"));
}

QVariantMap MachineBackend::clearAcknowledgedAlarms()
{
    return commandFailed(QStringLiteral("Backend не подключён"));
}
