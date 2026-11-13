#pragma once

#include "machine_types.h"

#include <QList>
#include <QMap>
#include <QStringList>
#include <QTime>

// The complete state the simulator can report. Fixtures fill it; the simulator changes it only
// through the same commands the real backend will implement.
struct SimState {
    QString run = QStringLiteral("IDLE");
    QString mode = QStringLiteral("AUTO");
    QString workplace = QStringLiteral("RUN");

    Position position;
    Position machinePosition;

    QList<WorkOffset> offsets;
    int activeOffset = 0;

    QList<Tool> tools;
    int tool = 0;
    QMap<int, int> toolTime;

    QList<ProgramFile> files;
    int activeFile = 0;
    int line = 0;
    int cursor = 0;

    QList<Alarm> alarms;
    Overrides overrides;

    int probeTool = 99;
    bool probeRunning = false;
    bool probeResult = false;
    int probeStep = 0;

    bool spindle = false;
    bool coolant = false;
    int load = 0;
    QMap<QString, int> axisLoad{{QStringLiteral("X"), 0}, {QStringLiteral("Y"), 0},
                                {QStringLiteral("Z"), 0}, {QStringLiteral("A"), 0}};

    int cycleTime = 0;
    int lastCycle = 0;
    int partCount = 0;

    bool singleBlock = false;
    bool optStop = false;
    bool dryRun = false;
    bool focused = true;

    QString message;
    QString clock = QStringLiteral("10:45:00");
    bool clockFrozen = true;
    QStringList mdiHistory;
};

inline const ProgramFile *activeProgram(const SimState &state)
{
    if (state.activeFile < 0 || state.activeFile >= state.files.size())
        return nullptr;
    return &state.files.at(state.activeFile);
}
