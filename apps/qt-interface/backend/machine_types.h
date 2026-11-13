#pragma once

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

// The data shared by every backend and the QML panel. The structs are the typed core; `toMap()`
// is only the QML boundary, so QML never needs to know the transport behind the values.
struct Position {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double a = 0.0;

    QVariantMap toMap() const;
};

struct Tool {
    int number = 0;
    QString name;
    double diameter = 0.0;
    double length = 0.0;
    int life = 100;
    bool inSpindle = false;

    QVariantMap toMap() const;
};

struct WorkOffset {
    QString code;
    Position values;
    bool active = false;

    QVariantMap toMap() const;
};

struct Alarm {
    QString id;
    QString code;
    QString text;
    // One of the panel levels: "error", "warning", "info".
    QString level = QStringLiteral("info");
    QString time;
    bool active = true;

    QVariantMap toMap() const;
};

struct ProgramFile {
    QString name;
    QString title;
    QStringList lines;

    QVariantMap toMap() const;
};

struct Overrides {
    int feed = 100;
    int spindle = 100;
    int rapid = 100;

    QVariantMap toMap() const;
};

// Every command returns `{ ok, reason }`. A refused command is a normal result, not a silent
// no-op: the QML panel prints `reason` in the message line.
QVariantMap commandOk();
QVariantMap commandFailed(const QString &reason);
