#include "machine_types.h"

QVariantMap Position::toMap() const
{
    return {{QStringLiteral("x"), x},
            {QStringLiteral("y"), y},
            {QStringLiteral("z"), z},
            {QStringLiteral("a"), a}};
}

QVariantMap Tool::toMap() const
{
    return {{QStringLiteral("number"), number},
            {QStringLiteral("name"), name},
            {QStringLiteral("diameter"), diameter},
            {QStringLiteral("length"), length},
            {QStringLiteral("life"), life},
            {QStringLiteral("inSpindle"), inSpindle}};
}

QVariantMap WorkOffset::toMap() const
{
    return {{QStringLiteral("code"), code},
            {QStringLiteral("values"), values.toMap()},
            {QStringLiteral("active"), active}};
}

QVariantMap Alarm::toMap() const
{
    return {{QStringLiteral("id"), id},
            {QStringLiteral("code"), code},
            {QStringLiteral("text"), text},
            {QStringLiteral("level"), level},
            {QStringLiteral("time"), time},
            {QStringLiteral("active"), active}};
}

QVariantMap ProgramFile::toMap() const
{
    QVariantList lineList;
    lineList.reserve(lines.size());
    for (const QString &line : lines)
        lineList.append(line);
    return {{QStringLiteral("name"), name},
            {QStringLiteral("title"), title},
            {QStringLiteral("modified"), modified},
            {QStringLiteral("lines"), lineList}};
}

QVariantMap Overrides::toMap() const
{
    return {{QStringLiteral("feed"), feed},
            {QStringLiteral("spindle"), spindle},
            {QStringLiteral("rapid"), rapid}};
}

QVariantMap commandOk()
{
    return {{QStringLiteral("ok"), true}, {QStringLiteral("reason"), QString()}};
}

QVariantMap commandFailed(const QString &reason)
{
    return {{QStringLiteral("ok"), false}, {QStringLiteral("reason"), reason}};
}
