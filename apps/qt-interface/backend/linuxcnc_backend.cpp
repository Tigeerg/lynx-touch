#include "linuxcnc_backend.h"

#include "linuxcnc_link.h"
#include "machine_types.h"

#include <QTime>

#include <algorithm>

namespace {

constexpr int POLL_MS = 100;
const double JOG_STEPS[] = {0.001, 0.01, 0.1};
const int JOG_STEP_COUNT = 3;
const int RAPID_STEPS[] = {5, 25, 50, 100};
const int RAPID_STEP_COUNT = 4;
const int G5X_COUNT = 6;

const QString G5X_CODES[G5X_COUNT] = {QStringLiteral("G54"), QStringLiteral("G55"),
                                      QStringLiteral("G56"), QStringLiteral("G57"),
                                      QStringLiteral("G58"), QStringLiteral("G59")};

int jointOfAxis(const QString &axis)
{
    const QString upper = axis.toUpper();
    if (upper == QStringLiteral("X"))
        return 0;
    if (upper == QStringLiteral("Y"))
        return 1;
    if (upper == QStringLiteral("Z"))
        return 2;
    if (upper == QStringLiteral("A"))
        return 3;
    return -1;
}

// An operator message line is "code text" when it starts with a number; that is how LinuxCNC
// formats most of them, and it keeps the panel's message table readable.
void splitMessage(const QString &line, QString *code, QString *text)
{
    const QString trimmed = line.trimmed();
    const int space = trimmed.indexOf(QLatin1Char(' '));
    const QString first = space > 0 ? trimmed.left(space) : trimmed;
    bool numeric = !first.isEmpty();
    for (const QChar c : first)
        numeric = numeric && c.isDigit();
    if (numeric && space > 0) {
        *code = first;
        *text = trimmed.mid(space + 1);
    } else {
        code->clear();
        *text = trimmed;
    }
}

} // namespace

LinuxCncBackend::LinuxCncBackend(LinuxCncLink *link, QObject *parent)
    : MachineBackend(parent)
    , m_link(link)
{
    m_pollTimer.setInterval(POLL_MS);
    connect(&m_pollTimer, &QTimer::timeout, this, &LinuxCncBackend::poll);
    m_pollTimer.start();
}

LinuxCncBackend::~LinuxCncBackend() = default;

QString LinuxCncBackend::backendName() const
{
    return QStringLiteral("linuxcnc");
}

bool LinuxCncBackend::connected() const
{
    return m_link && m_link->connected();
}

QString LinuxCncBackend::lastError() const
{
    return m_link ? m_link->lastError() : QStringLiteral("Связь с LinuxCNC не создана");
}

QString LinuxCncBackend::run() const
{
    if (!m_link || !m_link->connected())
        return QStringLiteral("IDLE");
    // ESTOP is the machine's own switch, ERROR is the task falling over, PAUSED is the program
    // waiting, EXEC is a running program; anything else is idle.
    if (m_link->estop())
        return QStringLiteral("ESTOP");
    if (m_link->taskState() == 3)
        return QStringLiteral("ALARM");
    if (m_link->interpreterState() == 3)
        return QStringLiteral("HOLD");
    if (m_link->taskState() == 2)
        return QStringLiteral("RUN");
    return QStringLiteral("IDLE");
}

QString LinuxCncBackend::mode() const
{
    if (!m_link || !m_link->connected())
        return {};
    switch (m_link->taskMode()) {
    case 2: return QStringLiteral("AUTO");
    case 3: return QStringLiteral("MDI");
    default: return QStringLiteral("JOG");   // MANUAL; MPG is a panel mode, not a task mode
    }
}

QString LinuxCncBackend::workplace() const
{
    // SETUP/TEST/RUN is the panel's own navigation state; LinuxCNC has no counterpart, so the
    // adapter does not invent one (docs/LINUXCNC-BACKEND.md, gap 1).
    return {};
}

QVariantMap LinuxCncBackend::position() const
{
    Position work;
    if (m_link && m_link->connected()) {
        const int active = std::clamp(m_link->activeG5xIndex(), 0, G5X_COUNT - 1);
        const bool hasX = m_link->jointCount() > 0;
        const bool hasY = m_link->jointCount() > 1;
        const bool hasZ = m_link->jointCount() > 2;
        const bool hasA = m_link->jointCount() > 3;
        if (hasX)
            work.x = m_link->jointPosition(0) - m_link->g5xOffset(active, 0);
        if (hasY)
            work.y = m_link->jointPosition(1) - m_link->g5xOffset(active, 1);
        if (hasZ)
            work.z = m_link->jointPosition(2) - m_link->g5xOffset(active, 2);
        if (hasA)
            work.a = m_link->jointPosition(3) - m_link->g5xOffset(active, 3);
    }
    return work.toMap();
}

QVariantMap LinuxCncBackend::machinePosition() const
{
    Position machine;
    if (m_link && m_link->connected()) {
        const int joints = m_link->jointCount();
        if (joints > 0)
            machine.x = m_link->jointPosition(0);
        if (joints > 1)
            machine.y = m_link->jointPosition(1);
        if (joints > 2)
            machine.z = m_link->jointPosition(2);
        if (joints > 3)
            machine.a = m_link->jointPosition(3);
    }
    return machine.toMap();
}

QVariantList LinuxCncBackend::offsets() const
{
    QVariantList rows;
    if (!m_link || !m_link->connected())
        return rows;
    for (int i = 0; i < G5X_COUNT; ++i) {
        WorkOffset offset;
        offset.code = G5X_CODES[i];
        offset.active = (i == m_link->activeG5xIndex());
        offset.values.x = m_link->g5xOffset(i, 0);
        offset.values.y = m_link->g5xOffset(i, 1);
        offset.values.z = m_link->g5xOffset(i, 2);
        offset.values.a = m_link->g5xOffset(i, 3);
        rows.append(offset.toMap());
    }
    return rows;
}

int LinuxCncBackend::activeOffset() const
{
    return m_link ? std::clamp(m_link->activeG5xIndex(), 0, G5X_COUNT - 1) : 0;
}

QVariantList LinuxCncBackend::tools() const
{
    QVariantList rows;
    if (!m_link || !m_link->connected())
        return rows;
    const int count = m_link->toolCount();
    const int inSpindle = m_link->toolInSpindle();
    for (int row = 0; row < count; ++row) {
        Tool tool;
        tool.number = m_link->toolNumber(row);
        tool.name = m_link->toolName(row);
        tool.diameter = m_link->toolDiameter(row);
        tool.length = m_link->toolLength(row);
        // LinuxCNC keeps no tool life: the panel shows 100 % until the stand says where life comes
        // from (LinuxCNC-BACKEND.md, gap 3).
        tool.life = 100;
        tool.inSpindle = (tool.number == inSpindle && inSpindle > 0);
        rows.append(tool.toMap());
    }
    return rows;
}

int LinuxCncBackend::tool() const
{
    return m_link ? m_link->toolInSpindle() : 0;
}

QVariantMap LinuxCncBackend::toolTime() const
{
    // Gap 3 again: no per-tool cutting time in stat.
    return {};
}

QVariantList LinuxCncBackend::programs() const
{
    QVariantList rows;
    if (!m_link || !m_link->connected())
        return rows;
    ProgramFile file;
    file.name = m_link->programFile();
    file.lines = m_link->programLines();
    file.title = file.name;   // LinuxCNC does not read a title out of the file; the comment is a later step
    rows.append(file.toMap());
    return rows;
}

QVariantMap LinuxCncBackend::program() const
{
    QVariantMap result = ProgramFile().toMap();
    if (!m_link || !m_link->connected())
        return result;
    const QStringList lines = m_link->programLines();
    const int lastLine = qMax(0, static_cast<int>(lines.size()) - 1);
    result.insert(QStringLiteral("name"), m_link->programFile());
    result.insert(QStringLiteral("title"), m_link->programFile());
    result.insert(QStringLiteral("lines"), lines);
    result.insert(QStringLiteral("activeFile"), 0);
    const int line = std::clamp(m_link->currentLine(), 0, lastLine);
    result.insert(QStringLiteral("line"), line);
    result.insert(QStringLiteral("cursor"), std::clamp(m_link->cursorLine(), 0, lastLine));
    result.insert(QStringLiteral("nextLine"), line < lastLine ? lines.at(line + 1) : QString());
    return result;
}

QVariantList LinuxCncBackend::alarms() const
{
    QVariantList rows;
    if (!m_link || !m_link->connected())
        return rows;
    const QStringList lines = m_link->operatorMessages();
    for (int i = 0; i < lines.size(); ++i) {
        Alarm alarm;
        alarm.id = QString::number(i + 1);
        splitMessage(lines.at(i), &alarm.code, &alarm.text);
        // stat carries no level; a panel level appears only when the stand says where to take it
        // from (gap 4).
        alarm.level = QStringLiteral("info");
        alarm.active = true;
        rows.append(alarm.toMap());
    }
    return rows;
}

QVariantMap LinuxCncBackend::probe() const
{
    // Gap 5: the probe is driven by the program or by a panel wizard; stat has no probe result.
    return {{QStringLiteral("tool"), 0},
            {QStringLiteral("running"), false},
            {QStringLiteral("result"), false},
            {QStringLiteral("step"), 0}};
}

QVariantMap LinuxCncBackend::overrides() const
{
    Overrides values;
    if (m_link && m_link->connected()) {
        values.feed = m_link->feedOverride();
        values.spindle = m_link->spindleOverride();
        values.rapid = RAPID_STEPS[std::clamp(m_link->rapidOverrideIndex(), 0, RAPID_STEP_COUNT - 1)];
    }
    return values.toMap();
}

QVariantList LinuxCncBackend::rapidSteps() const
{
    return {RAPID_STEPS[0], RAPID_STEPS[1], RAPID_STEPS[2], RAPID_STEPS[3]};
}

bool LinuxCncBackend::spindle() const
{
    return m_link && m_link->connected() && m_link->spindle();
}

bool LinuxCncBackend::coolant() const
{
    return m_link && m_link->connected() && m_link->coolant();
}

int LinuxCncBackend::load() const
{
    // Gap 6: spindle load comes from a HAL pin (spindle.N.load), not from stat.
    return 0;
}

QVariantMap LinuxCncBackend::axisLoad() const
{
    // Gap 6 again: per-axis load is a HAL signal, not a stat field.
    return {{QStringLiteral("X"), 0}, {QStringLiteral("Y"), 0},
            {QStringLiteral("Z"), 0}, {QStringLiteral("A"), 0}};
}

int LinuxCncBackend::cycleTime() const
{
    // Gap 7: stat has no cycle clock; the panel can count it from the RUN state or take it from a
    // HAL timer once the stand names it.
    return 0;
}

int LinuxCncBackend::lastCycle() const
{
    return 0;
}

int LinuxCncBackend::partCount() const
{
    // Gap 7 again: the finished-part counter is the panel's or the program's, not stat's.
    return 0;
}

bool LinuxCncBackend::singleBlock() const
{
    return m_link && m_link->connected() && m_link->singleBlock();
}

bool LinuxCncBackend::optStop() const
{
    return m_link && m_link->connected() && m_link->optStop();
}

bool LinuxCncBackend::dryRun() const
{
    return m_link && m_link->connected() && m_link->dryRun();
}

QString LinuxCncBackend::message() const
{
    return m_link ? m_link->lastMessage() : QString();
}

QString LinuxCncBackend::clock() const
{
    // Wall clock: the panel header shows the time of the operator's shift, as the React panel does.
    return QTime::currentTime().toString(QStringLiteral("HH:mm:ss"));
}

bool LinuxCncBackend::focused() const
{
    return m_focused;
}

QStringList LinuxCncBackend::mdiHistory() const
{
    return m_mdiHistory;
}

void LinuxCncBackend::setFocused(bool focused)
{
    if (m_focused == focused)
        return;
    m_focused = focused;
    emit focusedChanged();
}

QVariantMap LinuxCncBackend::resultOrReason(bool ok, const QString &reason)
{
    if (ok)
        return commandOk();
    const QString error = m_link ? m_link->lastError() : QString();
    return commandFailed(!error.isEmpty() ? error : reason);
}

QVariantMap LinuxCncBackend::cycleStart()
{
    if (!m_link)
        return commandFailed(QStringLiteral("Связь с LinuxCNC не создана"));
    return resultOrReason(m_link->cycleStart(), QStringLiteral("Станок не принял ПУСК"));
}

QVariantMap LinuxCncBackend::feedHold()
{
    if (!m_link)
        return commandFailed(QStringLiteral("Связь с LinuxCNC не создана"));
    return resultOrReason(m_link->feedHold(), QStringLiteral("Станок не принял ПАУЗУ"));
}

QVariantMap LinuxCncBackend::reset()
{
    if (!m_link)
        return commandFailed(QStringLiteral("Связь с LinuxCNC не создана"));
    return resultOrReason(m_link->reset(), QStringLiteral("Станок не принял СБРОС"));
}

QVariantMap LinuxCncBackend::emergencyStop()
{
    if (!m_link)
        return commandFailed(QStringLiteral("Связь с LinuxCNC не создана"));
    return resultOrReason(m_link->setEstop(true), QStringLiteral("Станок не принял АВАРИЮ"));
}

QVariantMap LinuxCncBackend::setWorkplace(const QString &workplace)
{
    // The workplace is the panel's own state; the machine only sees the mode and the dry run that
    // the workplace implies (gap 1).
    Q_UNUSED(workplace)
    return commandOk();
}

QVariantMap LinuxCncBackend::setMode(const QString &mode)
{
    if (!m_link)
        return commandFailed(QStringLiteral("Связь с LinuxCNC не создана"));
    int code = 0;
    if (mode == QStringLiteral("AUTO"))
        code = 2;
    else if (mode == QStringLiteral("MDI"))
        code = 3;
    else if (mode == QStringLiteral("JOG") || mode == QStringLiteral("MPG"))
        code = 1;   // LinuxCNC has one manual mode; the MPG row is the panel's step table
    else
        return commandFailed(QStringLiteral("Неизвестный режим"));
    return resultOrReason(m_link->setTaskMode(code), QStringLiteral("Станок не принял режим"));
}

QVariantMap LinuxCncBackend::jog(const QString &axis, int step, int direction)
{
    if (!m_link)
        return commandFailed(QStringLiteral("Связь с LinuxCNC не создана"));
    if (step < 0 || step >= JOG_STEP_COUNT)
        return commandFailed(QStringLiteral("Неизвестный шаг подачи"));
    if (direction != -1 && direction != 1)
        return commandFailed(QStringLiteral("Направление подачи: -1 или 1"));
    const int joint = jointOfAxis(axis);
    if (joint < 0)
        return commandFailed(QStringLiteral("Неизвестная ось"));
    const double delta = JOG_STEPS[step] * direction;
    return resultOrReason(m_link->jogJoint(joint, delta), QStringLiteral("Станок не принял подачу"));
}

QVariantMap LinuxCncBackend::mdi(const QString &text)
{
    if (!m_link)
        return commandFailed(QStringLiteral("Связь с LinuxCNC не создана"));
    const QString line = text.trimmed();
    if (line.isEmpty())
        return commandFailed(QStringLiteral("Пустая команда"));
    if (!m_link->runMdiLine(line))
        return resultOrReason(false, QStringLiteral("Станок не принял MDI"));
    m_mdiHistory.removeAll(line.toUpper());
    m_mdiHistory.prepend(line.toUpper());
    while (m_mdiHistory.size() > 3)
        m_mdiHistory.removeLast();
    emit mdiHistoryChanged();
    return commandOk();
}

QVariantMap LinuxCncBackend::loadProgram(int index)
{
    if (!m_link)
        return commandFailed(QStringLiteral("Связь с LinuxCNC не создана"));
    // The adapter carries one open program (the file the controller has loaded); the panel
    // catalogue is the file list of the controller, which is a later step (gap 2).
    if (index != 0)
        return commandFailed(QStringLiteral("LinuxCNC держит одну открытую УП"));
    return resultOrReason(m_link->openProgram(m_link->programFile()),
                          QStringLiteral("Станок не открыл программу"));
}

QVariantMap LinuxCncBackend::startFromCursor()
{
    if (!m_link)
        return commandFailed(QStringLiteral("Связь с LinuxCNC не создана"));
    // Starting from a line is "run from line" in LinuxCNC; the mapping is fixed here and the real
    // call belongs to the link (gap 8).
    return resultOrReason(m_link->cycleStart(), QStringLiteral("Станок не принял старт с кадра"));
}

QVariantMap LinuxCncBackend::setOverride(const QString &kind, int direction)
{
    if (!m_link)
        return commandFailed(QStringLiteral("Связь с LinuxCNC не создана"));
    if (direction != -1 && direction != 1)
        return commandFailed(QStringLiteral("Направление: -1 или 1"));
    constexpr int STEP = 10;
    if (kind == QStringLiteral("feed")) {
        const int value = std::clamp(m_link->feedOverride() + direction * STEP, 0, 150);
        return resultOrReason(m_link->setOverride(kind, value), QStringLiteral("Станок не принял коррекцию подачи"));
    }
    if (kind == QStringLiteral("spindle")) {
        const int value = std::clamp(m_link->spindleOverride() + direction * STEP, 0, 150);
        return resultOrReason(m_link->setOverride(kind, value), QStringLiteral("Станок не принял коррекцию шпинделя"));
    }
    if (kind == QStringLiteral("rapid")) {
        const int index = std::clamp(m_link->rapidOverrideIndex() + direction, 0, RAPID_STEP_COUNT - 1);
        return resultOrReason(m_link->setOverride(kind, RAPID_STEPS[index]),
                              QStringLiteral("Станок не принял коррекцию быстрых"));
    }
    return commandFailed(QStringLiteral("Неизвестная коррекция"));
}

QVariantMap LinuxCncBackend::toggle(const QString &key)
{
    if (!m_link)
        return commandFailed(QStringLiteral("Связь с LinuxCNC не создана"));
    if (key == QStringLiteral("singleBlock"))
        return resultOrReason(m_link->setToggle(key, !m_link->singleBlock()), QStringLiteral("Станок не принял переключатель"));
    if (key == QStringLiteral("optStop"))
        return resultOrReason(m_link->setToggle(key, !m_link->optStop()), QStringLiteral("Станок не принял переключатель"));
    if (key == QStringLiteral("dryRun"))
        return resultOrReason(m_link->setToggle(key, !m_link->dryRun()), QStringLiteral("Станок не принял переключатель"));
    if (key == QStringLiteral("spindle"))
        return resultOrReason(m_link->setToggle(key, !m_link->spindle()), QStringLiteral("Станок не принял шпиндель"));
    if (key == QStringLiteral("coolant"))
        return resultOrReason(m_link->setToggle(key, !m_link->coolant()), QStringLiteral("Станок не принял СОЖ"));
    return commandFailed(QStringLiteral("Неизвестный переключатель"));
}

QVariantMap LinuxCncBackend::acknowledgeAlarms()
{
    if (!m_link)
        return commandFailed(QStringLiteral("Связь с LinuxCNC не создана"));
    // Operator messages are acknowledged through the message API, which is a HAL/`error` call on
    // the stand; the link owns it (gap 4).
    return resultOrReason(m_link->setToggle(QStringLiteral("acknowledgeAlarms"), true),
                          QStringLiteral("Станок не принял квитирование"));
}

QVariantMap LinuxCncBackend::clearAcknowledgedAlarms()
{
    if (!m_link)
        return commandFailed(QStringLiteral("Связь с LinuxCNC не создана"));
    return resultOrReason(m_link->setToggle(QStringLiteral("clearAcknowledgedAlarms"), true),
                          QStringLiteral("Станок не принял очистку сообщений"));
}

QVariantMap LinuxCncBackend::acknowledgeAlarm(int index)
{
    if (!m_link)
        return commandFailed(QStringLiteral("Связь с LinuxCNC не создана"));
    return resultOrReason(m_link->setToggle(QStringLiteral("acknowledgeAlarm:") + QString::number(index), true),
                          QStringLiteral("Станок не принял квитирование"));
}

QVariantMap LinuxCncBackend::setActiveOffset(int index)
{
    if (!m_link)
        return commandFailed(QStringLiteral("Связь с LinuxCNC не создана"));
    if (index < 0 || index >= G5X_COUNT)
        return commandFailed(QStringLiteral("Нет такого смещения"));
    return resultOrReason(m_link->setActiveG5x(index), QStringLiteral("Станок не принял смещение"));
}

QVariantMap LinuxCncBackend::zeroOffset(int index)
{
    if (!m_link)
        return commandFailed(QStringLiteral("Связь с LinuxCNC не создана"));
    if (index < 0 || index >= G5X_COUNT)
        return commandFailed(QStringLiteral("Нет такого смещения"));
    return resultOrReason(m_link->zeroG5x(index), QStringLiteral("Станок не принял очистку смещения"));
}

QVariantMap LinuxCncBackend::touchOff(int index, const QString &axis)
{
    if (!m_link)
        return commandFailed(QStringLiteral("Связь с LinuxCNC не создана"));
    const int joint = jointOfAxis(axis);
    if (joint < 0)
        return commandFailed(QStringLiteral("Неизвестная ось"));
    return resultOrReason(m_link->touchOffG5x(index, joint), QStringLiteral("Станок не принял привязку"));
}

QVariantMap LinuxCncBackend::setToolLife(int number, int life)
{
    Q_UNUSED(number)
    Q_UNUSED(life)
    // Gap 3: LinuxCNC keeps no tool life, so there is nothing to write. Refusing is honest: the
    // panel shows the machine's answer instead of pretending the life was stored.
    return commandFailed(QStringLiteral("Ресурс инструмента не хранится в LinuxCNC"));
}

QVariantMap LinuxCncBackend::setToolLength(int number, double length)
{
    if (!m_link)
        return commandFailed(QStringLiteral("Связь с LinuxCNC не создана"));
    return resultOrReason(m_link->setToolLength(number, length),
                          QStringLiteral("Станок не принял длину инструмента"));
}

QVariantMap LinuxCncBackend::selectTool(int number)
{
    if (!m_link)
        return commandFailed(QStringLiteral("Связь с LinuxCNC не создана"));
    return resultOrReason(m_link->selectTool(number), QStringLiteral("Станок не принял инструмент"));
}

QVariantMap LinuxCncBackend::countPart()
{
    // Gap 7: the counter is the panel's or the program's; there is no machine command for it.
    return commandFailed(QStringLiteral("Счётчик деталей не хранится в LinuxCNC"));
}

void LinuxCncBackend::poll()
{
    if (!m_link)
        return;

    if (connected() != m_connected) {
        m_connected = connected();
        emit connectionChanged();
    }
    const QString run = this->run();
    if (run != m_run) {
        m_run = run;
        emit runChanged();
    }
    const QString mode = this->mode();
    if (mode != m_mode) {
        m_mode = mode;
        emit modeChanged();
    }
    const QVariantMap position = this->position();
    if (position != m_position) {
        m_position = position;
        emit positionChanged();
    }
    const QVariantList offsets = this->offsets();
    if (offsets != m_offsets) {
        m_offsets = offsets;
        emit offsetsChanged();
    }
    const QVariantList tools = this->tools();
    if (tools != m_tools) {
        m_tools = tools;
        emit toolsChanged();
    }
    const QVariantList alarms = this->alarms();
    if (alarms != m_alarms) {
        m_alarms = alarms;
        emit alarmsChanged();
    }
    const QVariantMap program = this->program();
    if (program != m_program) {
        m_program = program;
        emit programsChanged();
        emit programChanged();
    }
    const QVariantMap overrides = this->overrides();
    if (overrides != m_overrides) {
        m_overrides = overrides;
        emit overridesChanged();
    }
    if (this->spindle() != m_spindle) {
        m_spindle = this->spindle();
        emit spindleChanged();
    }
    if (this->coolant() != m_coolant) {
        m_coolant = this->coolant();
        emit coolantChanged();
    }
    if (this->singleBlock() != m_singleBlock || this->optStop() != m_optStop
        || this->dryRun() != m_dryRun) {
        m_singleBlock = this->singleBlock();
        m_optStop = this->optStop();
        m_dryRun = this->dryRun();
        emit flagsChanged();
    }
    const QString message = this->message();
    if (message != m_message) {
        m_message = message;
        emit messageChanged();
    }
    const QString clock = this->clock();
    if (clock != m_clock) {
        m_clock = clock;
        emit clockChanged();
    }
}
