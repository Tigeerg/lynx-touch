#pragma once

#include "machine_backend.h"

#include <QString>

// Creates the backend named on the command line. `sim` is the debug and test source; `linuxcnc`
// is reserved for the machine adapter and fails loudly until it exists. There is deliberately no
// silent fallback: a panel on a real machine must not show simulated numbers.
MachineBackend *createBackend(const QString &kind, const QString &fixturePath,
                              const QString &frozenClock, QObject *parent, QString *error);
