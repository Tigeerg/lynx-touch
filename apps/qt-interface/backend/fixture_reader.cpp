#include "fixture_reader.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

namespace {

bool fail(QString *error, const QString &text)
{
    if (error)
        *error = text;
    return false;
}

bool readPosition(const QJsonObject &object, const QString &key, Position *position, QString *error)
{
    const QJsonValue value = object.value(key);
    if (!value.isObject())
        return fail(error, QStringLiteral("fixture: missing object \"%1\"").arg(key));
    const QJsonObject point = value.toObject();
    const QStringList axes{QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("z"),
                           QStringLiteral("a")};
    for (const QString &axis : axes) {
        if (!point.value(axis).isDouble())
            return fail(error, QStringLiteral("fixture: \"%1.%2\" must be a number").arg(key, axis));
    }
    position->x = point.value(QStringLiteral("x")).toDouble();
    position->y = point.value(QStringLiteral("y")).toDouble();
    position->z = point.value(QStringLiteral("z")).toDouble();
    position->a = point.value(QStringLiteral("a")).toDouble();
    return true;
}

bool readStateName(const QJsonObject &root, const QString &key, const QSet<QString> &allowed,
                   QString *value, QString *error)
{
    const QString text = root.value(key).toString();
    if (!allowed.contains(text))
        return fail(error, QStringLiteral("fixture: \"%1\" has unknown value \"%2\"").arg(key, text));
    *value = text;
    return true;
}

bool readTools(const QJsonArray &array, SimState *state, QString *error)
{
    if (array.isEmpty())
        return fail(error, QStringLiteral("fixture: \"tools\" must not be empty"));
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            return fail(error, QStringLiteral("fixture: every tool must be an object"));
        const QJsonObject object = value.toObject();
        Tool tool;
        tool.number = object.value(QStringLiteral("number")).toInt();
        tool.name = object.value(QStringLiteral("name")).toString();
        tool.diameter = object.value(QStringLiteral("diameter")).toDouble();
        tool.length = object.value(QStringLiteral("length")).toDouble();
        tool.life = object.value(QStringLiteral("life")).toInt(100);
        tool.inSpindle = object.value(QStringLiteral("inSpindle")).toBool();
        if (tool.number <= 0 || tool.name.isEmpty())
            return fail(error, QStringLiteral("fixture: tool needs number and name"));
        state->tools.append(tool);
        if (tool.inSpindle)
            state->tool = tool.number;
    }
    if (state->tool == 0)
        state->tool = state->tools.first().number;
    return true;
}

bool readOffsets(const QJsonArray &array, SimState *state, QString *error)
{
    if (array.isEmpty())
        return fail(error, QStringLiteral("fixture: \"offsets\" must not be empty"));
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            return fail(error, QStringLiteral("fixture: every offset must be an object"));
        const QJsonObject object = value.toObject();
        WorkOffset offset;
        offset.code = object.value(QStringLiteral("code")).toString();
        if (offset.code.isEmpty())
            return fail(error, QStringLiteral("fixture: offset needs a code"));
        if (!readPosition(object, QStringLiteral("values"), &offset.values, error))
            return false;
        offset.active = object.value(QStringLiteral("active")).toBool();
        state->offsets.append(offset);
        if (offset.active)
            state->activeOffset = state->offsets.size() - 1;
    }
    return true;
}

bool readFiles(const QJsonArray &array, SimState *state, QString *error)
{
    if (array.isEmpty())
        return fail(error, QStringLiteral("fixture: \"files\" must not be empty"));
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            return fail(error, QStringLiteral("fixture: every program must be an object"));
        const QJsonObject object = value.toObject();
        ProgramFile file;
        file.name = object.value(QStringLiteral("name")).toString();
        file.title = object.value(QStringLiteral("title")).toString();
        const QJsonArray lines = object.value(QStringLiteral("lines")).toArray();
        for (const QJsonValue &line : lines)
            file.lines.append(line.toString());
        if (file.name.isEmpty() || file.lines.isEmpty())
            return fail(error, QStringLiteral("fixture: program needs name and lines"));
        state->files.append(file);
    }
    return true;
}

bool readAlarms(const QJsonArray &array, SimState *state, QString *error)
{
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            return fail(error, QStringLiteral("fixture: every alarm must be an object"));
        const QJsonObject object = value.toObject();
        Alarm alarm;
        alarm.id = object.value(QStringLiteral("id")).toString();
        alarm.code = object.value(QStringLiteral("code")).toString();
        alarm.text = object.value(QStringLiteral("text")).toString();
        alarm.level = object.value(QStringLiteral("level")).toString(QStringLiteral("info"));
        alarm.time = object.value(QStringLiteral("time")).toString();
        alarm.active = object.value(QStringLiteral("active")).toBool(true);
        if (alarm.id.isEmpty())
            return fail(error, QStringLiteral("fixture: alarm needs an id"));
        state->alarms.append(alarm);
    }
    return true;
}

bool readToolTime(const QJsonObject &object, SimState *state, QString *error)
{
    for (auto it = object.begin(); it != object.end(); ++it) {
        bool ok = false;
        const int number = it.key().toInt(&ok);
        if (!ok || !it.value().isDouble())
            return fail(error, QStringLiteral("fixture: toolTime keys must be tool numbers"));
        state->toolTime.insert(number, it.value().toInt());
    }
    return true;
}

bool readAxisLoad(const QJsonObject &object, SimState *state, QString *error)
{
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (!it.value().isDouble())
            return fail(error, QStringLiteral("fixture: axisLoad.%1 must be a number").arg(it.key()));
        state->axisLoad.insert(it.key(), it.value().toInt());
    }
    return true;
}

} // namespace

bool readFixture(const QString &path, SimState &state, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("fixture: cannot open %1").arg(path));

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return fail(error, QStringLiteral("fixture: %1: %2 at offset %3")
                               .arg(path, parseError.errorString())
                               .arg(parseError.offset));
    if (!document.isObject())
        return fail(error, QStringLiteral("fixture: %1: root must be an object").arg(path));

    SimState next;
    const QJsonObject root = document.object();

    if (!readStateName(root, QStringLiteral("run"),
                       {QStringLiteral("IDLE"), QStringLiteral("RUN"), QStringLiteral("HOLD"),
                        QStringLiteral("ALARM"), QStringLiteral("ESTOP")},
                       &next.run, error))
        return false;
    if (!readStateName(root, QStringLiteral("mode"),
                       {QStringLiteral("AUTO"), QStringLiteral("MDI"), QStringLiteral("JOG"),
                        QStringLiteral("MPG")},
                       &next.mode, error))
        return false;
    if (!readStateName(root, QStringLiteral("workplace"),
                       {QStringLiteral("SETUP"), QStringLiteral("TEST"), QStringLiteral("RUN")},
                       &next.workplace, error))
        return false;
    if (!readPosition(root, QStringLiteral("position"), &next.position, error))
        return false;
    if (root.contains(QStringLiteral("machine"))) {
        if (!readPosition(root, QStringLiteral("machine"), &next.machinePosition, error))
            return false;
    } else {
        next.machinePosition = next.position;
    }
    if (!readOffsets(root.value(QStringLiteral("offsets")).toArray(), &next, error))
        return false;
    if (!readTools(root.value(QStringLiteral("tools")).toArray(), &next, error))
        return false;
    if (!readFiles(root.value(QStringLiteral("files")).toArray(), &next, error))
        return false;
    if (!readAlarms(root.value(QStringLiteral("alarms")).toArray(), &next, error))
        return false;
    const QJsonObject probe = root.value(QStringLiteral("probe")).toObject();
    next.probeTool = probe.value(QStringLiteral("tool")).toInt(99);
    next.probeRunning = probe.value(QStringLiteral("running")).toBool(false);
    next.probeResult = probe.value(QStringLiteral("result")).toBool(false);
    next.probeStep = probe.value(QStringLiteral("step")).toInt(0);
    if (!readToolTime(root.value(QStringLiteral("toolTime")).toObject(), &next, error))
        return false;
    if (!readAxisLoad(root.value(QStringLiteral("axisLoad")).toObject(), &next, error))
        return false;

    next.activeFile = root.value(QStringLiteral("activeFile")).toInt(0);
    if (next.activeFile < 0 || next.activeFile >= next.files.size())
        return fail(error, QStringLiteral("fixture: activeFile is outside the files list"));
    next.line = root.value(QStringLiteral("line")).toInt(0);
    next.cursor = root.value(QStringLiteral("cursor")).toInt(next.line);
    if (next.line < 0 || next.line >= next.files.at(next.activeFile).lines.size())
        return fail(error, QStringLiteral("fixture: line is outside the active program"));
    next.cycleTime = root.value(QStringLiteral("cycleTime")).toInt(0);
    next.lastCycle = root.value(QStringLiteral("lastCycle")).toInt(0);
    next.partCount = root.value(QStringLiteral("partCount")).toInt(0);
    next.load = root.value(QStringLiteral("load")).toInt(0);

    const QJsonObject overrides = root.value(QStringLiteral("overrides")).toObject();
    next.overrides.feed = overrides.value(QStringLiteral("feed")).toInt(100);
    next.overrides.spindle = overrides.value(QStringLiteral("spindle")).toInt(100);
    next.overrides.rapid = overrides.value(QStringLiteral("rapid")).toInt(100);

    next.spindle = root.value(QStringLiteral("spindle")).toBool(false);
    next.coolant = root.value(QStringLiteral("coolant")).toBool(false);
    next.singleBlock = root.value(QStringLiteral("singleBlock")).toBool(false);
    next.optStop = root.value(QStringLiteral("optStop")).toBool(false);
    next.dryRun = root.value(QStringLiteral("dryRun")).toBool(false);
    next.focused = root.value(QStringLiteral("focused")).toBool(true);
    next.message = root.value(QStringLiteral("message")).toString();
    next.clock = root.value(QStringLiteral("clock")).toString(QStringLiteral("10:45:00"));
    next.clockFrozen = root.value(QStringLiteral("clockFrozen")).toBool(true);
    if (!QTime::fromString(next.clock, QStringLiteral("HH:mm:ss")).isValid())
        return fail(error, QStringLiteral("fixture: clock must be HH:mm:ss"));
    for (const QJsonValue &value : root.value(QStringLiteral("mdiHistory")).toArray())
        next.mdiHistory.append(value.toString());

    if (next.offsets.value(next.activeOffset).code.isEmpty())
        return fail(error, QStringLiteral("fixture: no active work offset"));
    if (next.tools.isEmpty())
        return fail(error, QStringLiteral("fixture: no tools"));

    state = next;
    if (error)
        error->clear();
    return true;
}
