#include "sim_backend.h"

#include "fixture_reader.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace {

constexpr int TICK_MS = 100;
constexpr int TICKS_PER_SECOND = 10;
constexpr int TICKS_PER_PROGRAM_LINE = 4;
constexpr int OVERRIDE_STEP = 10;

const double JOG_STEPS[] = {0.001, 0.01, 0.1};
const int JOG_STEP_COUNT = 3;
const int RAPID_STEPS[] = {5, 25, 50, 100};
const int RAPID_STEP_COUNT = 4;

QString formattedCoordinate(double value)
{
    return QString::number(value, 'f', 3);
}

} // namespace

SimBackend::SimBackend(QObject *parent)
    : MachineBackend(parent)
{
    m_clockTime = QTime::fromString(m_state.clock, QStringLiteral("HH:mm:ss"));
    m_tickTimer.setInterval(TICK_MS);
    connect(&m_tickTimer, &QTimer::timeout, this, &SimBackend::tick);
    m_tickTimer.start();
}

SimBackend::~SimBackend() = default;

QString SimBackend::backendName() const
{
    return QStringLiteral("sim");
}

bool SimBackend::connected() const
{
    return true;
}

QString SimBackend::lastError() const
{
    return m_lastError;
}

QString SimBackend::run() const
{
    return m_state.run;
}

QString SimBackend::mode() const
{
    return m_state.mode;
}

QString SimBackend::workplace() const
{
    return m_state.workplace;
}

QVariantMap SimBackend::position() const
{
    return m_state.position.toMap();
}

QVariantMap SimBackend::machinePosition() const
{
    return m_state.machinePosition.toMap();
}

QVariantList SimBackend::offsets() const
{
    QVariantList result;
    for (const WorkOffset &offset : m_state.offsets)
        result.append(offset.toMap());
    return result;
}

int SimBackend::activeOffset() const
{
    return m_state.activeOffset;
}

QVariantList SimBackend::tools() const
{
    QVariantList result;
    for (const Tool &tool : m_state.tools)
        result.append(tool.toMap());
    return result;
}

int SimBackend::tool() const
{
    return m_state.tool;
}

QVariantMap SimBackend::toolTime() const
{
    QVariantMap result;
    for (auto it = m_state.toolTime.cbegin(); it != m_state.toolTime.cend(); ++it)
        result.insert(QString::number(it.key()), it.value());
    return result;
}

QVariantList SimBackend::programs() const
{
    QVariantList result;
    for (const ProgramFile &file : m_state.files)
        result.append(file.toMap());
    return result;
}

QVariantMap SimBackend::program() const
{
    const ProgramFile *file = activeProgram(m_state);
    QVariantMap result = file ? file->toMap() : ProgramFile().toMap();
    result.insert(QStringLiteral("activeFile"), m_state.activeFile);
    result.insert(QStringLiteral("line"), m_state.line);
    result.insert(QStringLiteral("cursor"), m_state.cursor);
    result.insert(QStringLiteral("nextLine"),
                  file && m_state.line + 1 < file->lines.size() ? file->lines.at(m_state.line + 1) : QString());
    return result;
}

QVariantList SimBackend::alarms() const
{
    QVariantList result;
    for (const Alarm &alarm : m_state.alarms)
        result.append(alarm.toMap());
    return result;
}

QVariantMap SimBackend::probe() const
{
    return {{QStringLiteral("tool"), m_state.probeTool},
            {QStringLiteral("running"), m_state.probeRunning},
            {QStringLiteral("result"), m_state.probeResult},
            {QStringLiteral("step"), m_state.probeStep}};
}

QVariantMap SimBackend::overrides() const
{
    return m_state.overrides.toMap();
}

QVariantList SimBackend::rapidSteps() const
{
    return {RAPID_STEPS[0], RAPID_STEPS[1], RAPID_STEPS[2], RAPID_STEPS[3]};
}

bool SimBackend::spindle() const
{
    return m_state.spindle;
}

bool SimBackend::coolant() const
{
    return m_state.coolant;
}

int SimBackend::load() const
{
    return m_state.load;
}

QVariantMap SimBackend::axisLoad() const
{
    QVariantMap result;
    for (auto it = m_state.axisLoad.cbegin(); it != m_state.axisLoad.cend(); ++it)
        result.insert(it.key(), it.value());
    return result;
}

int SimBackend::cycleTime() const
{
    return m_state.cycleTime;
}

int SimBackend::lastCycle() const
{
    return m_state.lastCycle;
}

int SimBackend::partCount() const
{
    return m_state.partCount;
}

bool SimBackend::singleBlock() const
{
    return m_state.singleBlock;
}

bool SimBackend::optStop() const
{
    return m_state.optStop;
}

bool SimBackend::dryRun() const
{
    return m_state.dryRun;
}

QString SimBackend::message() const
{
    return m_state.message;
}

QString SimBackend::clock() const
{
    return m_clockTime.toString(QStringLiteral("HH:mm:ss"));
}

bool SimBackend::focused() const
{
    return m_state.focused;
}

QStringList SimBackend::mdiHistory() const
{
    return m_state.mdiHistory;
}

void SimBackend::setFocused(bool focused)
{
    if (m_state.focused == focused)
        return;
    m_state.focused = focused;
    emit focusedChanged();
}

bool SimBackend::loadFixture(const QString &path)
{
    SimState next;
    QString error;
    if (!readFixture(path, next, &error)) {
        m_lastError = error;
        emit errorChanged();
        return false;
    }

    m_state = next;
    m_clockTime = QTime::fromString(m_state.clock, QStringLiteral("HH:mm:ss"));
    m_clockTime = m_clockTime.isValid() ? m_clockTime : QTime(10, 45, 0);
    m_ticks = 0;
    m_ticksOnLine = 0;
    m_lastError.clear();
    emit errorChanged();
    emitAll();
    return true;
}

bool SimBackend::setFrozenClock(const QString &clock)
{
    const QTime parsed = QTime::fromString(clock, QStringLiteral("HH:mm:ss"));
    if (!parsed.isValid()) {
        m_lastError = QStringLiteral("clock: expected HH:mm:ss, got \"%1\"").arg(clock);
        emit errorChanged();
        return false;
    }
    m_clockTime = parsed;
    m_state.clockFrozen = true;
    emit clockChanged();
    return true;
}

void SimBackend::emitAll()
{
    emit connectionChanged();
    emit runChanged();
    emit modeChanged();
    emit workplaceChanged();
    emit positionChanged();
    emit offsetsChanged();
    emit toolsChanged();
    emit programsChanged();
    emit programChanged();
    emit alarmsChanged();
    emit probeChanged();
    emit overridesChanged();
    emit spindleChanged();
    emit coolantChanged();
    emit loadChanged();
    emit countersChanged();
    emit flagsChanged();
    emit messageChanged();
    emit clockChanged();
    emit focusedChanged();
    emit mdiHistoryChanged();
}

void SimBackend::setMessage(const QString &text)
{
    if (m_state.message == text)
        return;
    m_state.message = text;
    emit messageChanged();
}

void SimBackend::tick()
{
    ++m_ticks;
    if (!m_state.clockFrozen && m_ticks % TICKS_PER_SECOND == 0) {
        m_clockTime = m_clockTime.addSecs(1);
        emit clockChanged();
    }
    if (m_state.run != QStringLiteral("RUN"))
        return;

    m_state.cycleTime += TICK_MS;
    emit countersChanged();

    ++m_ticksOnLine;
    if (m_ticksOnLine < TICKS_PER_PROGRAM_LINE)
        return;
    m_ticksOnLine = 0;
    advanceLine();
}

void SimBackend::advanceLine()
{
    const ProgramFile *file = activeProgram(m_state);
    if (!file) {
        m_state.run = QStringLiteral("IDLE");
        emit runChanged();
        return;
    }
    if (m_state.line + 1 >= file->lines.size()) {
        m_state.run = QStringLiteral("IDLE");
        m_state.lastCycle = m_state.cycleTime;
        m_state.cycleTime = 0;
        ++m_state.partCount;
        m_state.line = 0;
        m_state.cursor = 0;
        setMessage(QStringLiteral("Программа завершена"));
        emit runChanged();
        emit countersChanged();
        emit programChanged();
        return;
    }

    ++m_state.line;
    m_state.cursor = m_state.line;
    const QString line = file->lines.at(m_state.line).toUpper();
    const QString clean = QString(line).remove(QRegularExpression(QStringLiteral("\\(.*?\\)")));
    const QStringList tokens = clean.split(QRegularExpression(QStringLiteral("\\s+")),
                                           Qt::SkipEmptyParts);
    for (const QString &token : tokens) {
        if (token.size() < 2)
            continue;
        bool ok = false;
        const double value = token.mid(1).toDouble(&ok);
        if (!ok)
            continue;
        if (token.startsWith(QLatin1Char('X')))
            m_state.position.x = value;
        else if (token.startsWith(QLatin1Char('Y')))
            m_state.position.y = value;
        else if (token.startsWith(QLatin1Char('Z')))
            m_state.position.z = value;
        else if (token.startsWith(QLatin1Char('A')))
            m_state.position.a = value;
    }
    m_state.machinePosition = m_state.position;
    emit programChanged();
    emit positionChanged();
}

QVariantMap SimBackend::cycleStart()
{
    if (m_state.run == QStringLiteral("ESTOP") || m_state.run == QStringLiteral("ALARM"))
        return commandFailed(QStringLiteral("Сначала сбросьте аварию"));
    if (m_state.run == QStringLiteral("RUN"))
        return commandFailed(QStringLiteral("Программа уже выполняется"));
    if (!activeProgram(m_state))
        return commandFailed(QStringLiteral("Нет активной программы"));
    m_state.run = QStringLiteral("RUN");
    setMessage(QStringLiteral("Выполнение"));
    emit runChanged();
    return commandOk();
}

QVariantMap SimBackend::feedHold()
{
    if (m_state.run != QStringLiteral("RUN"))
        return commandFailed(QStringLiteral("Программа не выполняется"));
    m_state.run = QStringLiteral("HOLD");
    setMessage(QStringLiteral("Подача остановлена"));
    emit runChanged();
    return commandOk();
}

QVariantMap SimBackend::reset()
{
    m_state.run = QStringLiteral("IDLE");
    m_state.line = 0;
    m_state.cursor = 0;
    m_state.cycleTime = 0;
    setMessage(QStringLiteral("Сброс"));
    emit runChanged();
    emit programChanged();
    emit countersChanged();
    return commandOk();
}

QVariantMap SimBackend::emergencyStop()
{
    m_state.run = QStringLiteral("ESTOP");
    setMessage(QStringLiteral("Аварийный стоп"));
    emit runChanged();
    return commandOk();
}

QVariantMap SimBackend::setWorkplace(const QString &workplace)
{
    if (m_state.run == QStringLiteral("RUN") || m_state.run == QStringLiteral("HOLD"))
        return commandFailed(QStringLiteral("Станок занят"));
    if (workplace != QStringLiteral("SETUP") && workplace != QStringLiteral("TEST")
        && workplace != QStringLiteral("RUN"))
        return commandFailed(QStringLiteral("Неизвестное рабочее место"));
    m_state.workplace = workplace;
    m_state.mode = workplace == QStringLiteral("SETUP") ? QStringLiteral("JOG")
                                                          : QStringLiteral("AUTO");
    m_state.line = 0;
    m_state.cursor = 0;
    setMessage(QStringLiteral("Режим: %1").arg(workplace));
    emit workplaceChanged();
    emit modeChanged();
    emit programChanged();
    return commandOk();
}

QVariantMap SimBackend::setMode(const QString &mode)
{
    if (m_state.run == QStringLiteral("RUN") || m_state.run == QStringLiteral("HOLD"))
        return commandFailed(QStringLiteral("Остановите программу, чтобы сменить режим"));
    if (mode != QStringLiteral("AUTO") && mode != QStringLiteral("MDI") && mode != QStringLiteral("JOG")
        && mode != QStringLiteral("MPG"))
        return commandFailed(QStringLiteral("Неизвестный режим"));
    m_state.mode = mode;
    setMessage(QStringLiteral("Режим: %1").arg(mode));
    emit modeChanged();
    return commandOk();
}

QVariantMap SimBackend::jog(const QString &axis, int step, int direction)
{
    if (m_state.run == QStringLiteral("RUN") || m_state.run == QStringLiteral("HOLD"))
        return commandFailed(QStringLiteral("Станок занят"));
    if (m_state.run == QStringLiteral("ESTOP") || m_state.run == QStringLiteral("ALARM"))
        return commandFailed(QStringLiteral("Сначала сбросьте аварию"));
    if (m_state.mode != QStringLiteral("JOG") && m_state.mode != QStringLiteral("MPG"))
        return commandFailed(QStringLiteral("Ручная подача доступна в JOG или MPG"));
    if (step < 0 || step >= JOG_STEP_COUNT)
        return commandFailed(QStringLiteral("Неизвестный шаг подачи"));
    if (direction != -1 && direction != 1)
        return commandFailed(QStringLiteral("Направление подачи: -1 или 1"));

    const QString upper = axis.toUpper();
    const double delta = JOG_STEPS[step] * direction;
    if (upper == QStringLiteral("X"))
        m_state.position.x += delta;
    else if (upper == QStringLiteral("Y"))
        m_state.position.y += delta;
    else if (upper == QStringLiteral("Z"))
        m_state.position.z += delta;
    else if (upper == QStringLiteral("A"))
        m_state.position.a += delta;
    else
        return commandFailed(QStringLiteral("Неизвестная ось"));
    m_state.machinePosition = m_state.position;
    setMessage(QStringLiteral("%1 %2 мм").arg(upper, formattedCoordinate(delta)));
    emit positionChanged();
    return commandOk();
}

QVariantMap SimBackend::mdi(const QString &text)
{
    if (m_state.run == QStringLiteral("RUN") || m_state.run == QStringLiteral("HOLD"))
        return commandFailed(QStringLiteral("Станок занят"));
    if (text.trimmed().isEmpty())
        return commandFailed(QStringLiteral("Пустая команда"));
    applyMdi(text);
    return commandOk();
}

void SimBackend::applyMdi(const QString &text)
{
    const QString upper = text.trimmed().toUpper();
    const QString clean = QString(upper).remove(QRegularExpression(QStringLiteral("\\(.*?\\)")));
    const QStringList tokens = clean.split(QRegularExpression(QStringLiteral("\\s+")),
                                           Qt::SkipEmptyParts);
    bool relative = false;
    bool recognized = false;
    for (const QString &token : tokens) {
        if (token == QStringLiteral("G90")) {
            relative = false;
            recognized = true;
        } else if (token == QStringLiteral("G91")) {
            relative = true;
            recognized = true;
        } else if (token == QStringLiteral("M03") || token == QStringLiteral("M3")) {
            m_state.spindle = true;
            recognized = true;
        } else if (token == QStringLiteral("M05") || token == QStringLiteral("M5")) {
            m_state.spindle = false;
            recognized = true;
        } else if (token == QStringLiteral("M08") || token == QStringLiteral("M8")) {
            m_state.coolant = true;
            recognized = true;
        } else if (token == QStringLiteral("M09") || token == QStringLiteral("M9")) {
            m_state.coolant = false;
            recognized = true;
        } else if (token.size() > 1 && token.at(0).isLetter()) {
            bool ok = false;
            const double value = token.mid(1).toDouble(&ok);
            if (!ok)
                continue;
            const QChar axis = token.at(0);
            double *target = nullptr;
            if (axis == QLatin1Char('X'))
                target = &m_state.position.x;
            else if (axis == QLatin1Char('Y'))
                target = &m_state.position.y;
            else if (axis == QLatin1Char('Z'))
                target = &m_state.position.z;
            else if (axis == QLatin1Char('A'))
                target = &m_state.position.a;
            if (target) {
                *target = relative ? *target + value : value;
                recognized = true;
            }
        }
    }

    if (!recognized) {
        setMessage(QStringLiteral("MDI: команда не распознана"));
        return;
    }

    m_state.machinePosition = m_state.position;
    m_state.mdiHistory.removeAll(upper);
    m_state.mdiHistory.prepend(upper);
    while (m_state.mdiHistory.size() > 3)
        m_state.mdiHistory.removeLast();
    setMessage(QStringLiteral("MDI: %1").arg(upper));
    emit positionChanged();
    emit spindleChanged();
    emit coolantChanged();
    emit mdiHistoryChanged();
}

QVariantMap SimBackend::loadProgram(int index)
{
    if (m_state.run == QStringLiteral("RUN") || m_state.run == QStringLiteral("HOLD"))
        return commandFailed(QStringLiteral("Станок занят"));
    if (index < 0 || index >= m_state.files.size())
        return commandFailed(QStringLiteral("Нет такой программы"));
    m_state.activeFile = index;
    m_state.line = 0;
    m_state.cursor = 0;
    setMessage(QStringLiteral("Программа: %1").arg(m_state.files.at(index).name));
    emit programsChanged();
    emit programChanged();
    return commandOk();
}

QVariantMap SimBackend::startFromCursor()
{
    if (m_state.run == QStringLiteral("RUN") || m_state.run == QStringLiteral("HOLD"))
        return commandFailed(QStringLiteral("Станок занят"));
    if (!activeProgram(m_state))
        return commandFailed(QStringLiteral("Нет активной программы"));
    m_state.line = m_state.cursor;
    m_state.run = QStringLiteral("RUN");
    setMessage(QStringLiteral("Старт с кадра %1").arg(m_state.line + 1));
    emit programChanged();
    emit runChanged();
    return commandOk();
}

QVariantMap SimBackend::setOverride(const QString &kind, int direction)
{
    if (direction != -1 && direction != 1)
        return commandFailed(QStringLiteral("Направление: -1 или 1"));
    if (kind == QStringLiteral("feed") || kind == QStringLiteral("spindle")) {
        int &value = kind == QStringLiteral("feed") ? m_state.overrides.feed : m_state.overrides.spindle;
        value = std::clamp(value + direction * OVERRIDE_STEP, 0, 150);
    } else if (kind == QStringLiteral("rapid")) {
        int index = 0;
        for (int i = 0; i < RAPID_STEP_COUNT; ++i) {
            if (RAPID_STEPS[i] == m_state.overrides.rapid) {
                index = i;
                break;
            }
        }
        index = std::clamp(index + direction, 0, RAPID_STEP_COUNT - 1);
        m_state.overrides.rapid = RAPID_STEPS[index];
    } else {
        return commandFailed(QStringLiteral("Неизвестная коррекция"));
    }
    emit overridesChanged();
    return commandOk();
}

QVariantMap SimBackend::toggle(const QString &key)
{
    if (key == QStringLiteral("singleBlock"))
        m_state.singleBlock = !m_state.singleBlock;
    else if (key == QStringLiteral("optStop"))
        m_state.optStop = !m_state.optStop;
    else if (key == QStringLiteral("dryRun"))
        m_state.dryRun = !m_state.dryRun;
    else if (key == QStringLiteral("spindle")) {
        m_state.spindle = !m_state.spindle;
        emit spindleChanged();
        setMessage(m_state.spindle ? QStringLiteral("Шпиндель M03") : QStringLiteral("Шпиндель M05"));
    } else if (key == QStringLiteral("coolant")) {
        m_state.coolant = !m_state.coolant;
        emit coolantChanged();
        setMessage(m_state.coolant ? QStringLiteral("СОЖ M08") : QStringLiteral("СОЖ M09"));
    } else {
        return commandFailed(QStringLiteral("Неизвестный переключатель"));
    }
    emit flagsChanged();
    return commandOk();
}

QVariantMap SimBackend::acknowledgeAlarms()
{
    for (Alarm &alarm : m_state.alarms)
        alarm.active = false;
    setMessage(QStringLiteral("Сообщения квитированы"));
    emit alarmsChanged();
    return commandOk();
}

QVariantMap SimBackend::clearAcknowledgedAlarms()
{
    const auto removed = std::remove_if(m_state.alarms.begin(), m_state.alarms.end(),
                                        [](const Alarm &alarm) { return !alarm.active; });
    m_state.alarms.erase(removed, m_state.alarms.end());
    setMessage(QStringLiteral("Квитированные сообщения удалены"));
    emit alarmsChanged();
    return commandOk();
}
