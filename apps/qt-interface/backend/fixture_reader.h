#pragma once

#include "sim_state.h"

#include <QString>

// Reads one JSON fixture into `state`. On failure the old state stays untouched and `error`
// names the file and the missing or invalid key, so a broken fixture is visible before a demo.
bool readFixture(const QString &path, SimState &state, QString *error);
