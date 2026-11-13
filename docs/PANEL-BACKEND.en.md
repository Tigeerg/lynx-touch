# PANEL-BACKEND.md — panel data contract and simulator

Record of stage 0 of the Qt panel transfer: the UI gets a single data contract
(`MachineBackend`), a simulator for tests and demos, and a screenshot comparison stand. The screens
still read their values from `main.qml`; stage 0 does not switch them to the contract, it prepares
the ground so that switch is mechanical and testable.

Terms: the human dictionary is [`ROLES.md`](../ROLES.md), section 9; every term is explained at its
first use (`AI_WORKFLOW.md`, section 0.2).

## 1. Why a contract

Today the machine numbers and texts are hard-coded in `apps/qt-interface/main.qml`. While the data
source is the panel file itself, LinuxCNC cannot be connected: there is no place to read HAL pins,
and a test cannot tell a live value from a literal.

The contract solves three problems:

* **One seam.** QML knows only the properties and commands of `MachineBackend`; where the values
  came from — simulator or machine — the panel neither asks nor branches on.
* **Honest refusal.** A command returns `{ ok, reason }`; a refusal is shown to the operator as a
  line, not as silence. There is no `if (simulator)` inside the screens.
* **Repeatability.** The simulator reads a JSON fixture and can freeze the clock, so a panel
  screenshot is the same from run to run.

## 2. Where it lives

| File | Role |
|---|---|
| `apps/qt-interface/backend/machine_types.h/.cpp` | Data types: position, tool, offset, message, program, overrides; `toMap()` is only the QML boundary |
| `apps/qt-interface/backend/machine_backend.h/.cpp` | Public contract: properties, commands, signals; the default refusal is "Backend не подключён" |
| `apps/qt-interface/backend/sim_state.h` | Full simulator state |
| `apps/qt-interface/backend/fixture_reader.h/.cpp` | Reads and validates a JSON fixture; a broken fixture does not overwrite the previous state |
| `apps/qt-interface/backend/sim_backend.h/.cpp` | Simulator: the same guards (busy, alarm, mode), JOG steps, overrides, MDI |
| `apps/qt-interface/backend/backend_factory.h/.cpp` | Backend selection by name: `sim` or `linuxcnc` |
| `apps/qt-interface/backend/fixtures/*.json` | States for tests and screenshots |
| `apps/qt-interface/backend/tests/tst_backend.cpp` | 12 checks of the contract and the simulator |
| `apps/qt-interface/BackendBridge.qml` | The panel seam: values in, commands back, `lastReason` (§4) |
| `apps/qt-interface/tests/StubBackend.qml` | Backend stub for the QML tests: the same properties and commands |

`lynx-backend` is a static library without Qt Quick: it can be tested without a window. `lynx-panel`
and `lynx-backend-test` link against it.

## 3. Contract

Read-only properties (signal in brackets):

* state: `backendName`, `connected`, `lastError`, `run` (`IDLE`/`RUN`/`HOLD`/`ALARM`/`ESTOP`),
  `mode` (`AUTO`/`MDI`/`JOG`/`MPG`), `workplace` (`SETUP`/`TEST`/`RUN`);
* geometry: `position`, `machinePosition`, `offsets`, `activeOffset`;
* tool: `tools`, `tool`, `toolTime`, `probe`;
* program: `programs`, `program` (active file, line, cursor, next line);
* machine: `overrides`, `spindle`, `coolant`, `load`, `axisLoad`, `rapidSteps`;
* counters: `cycleTime`, `lastCycle`, `partCount`;
* other: `alarms`, `singleBlock`, `optStop`, `dryRun`, `message`, `clock`, `focused`, `mdiHistory`.

Commands (`Q_INVOKABLE`, all return `{ ok, reason }`): `cycleStart`, `feedHold`, `reset`,
`emergencyStop`, `setWorkplace`, `setMode`, `jog(axis, step, direction)`, `mdi(text)`,
`loadProgram(index)`, `startFromCursor`, `setOverride(kind, direction)`, `toggle(key)`,
`acknowledgeAlarms`, `clearAcknowledgedAlarms`.

The simulator repeats the React reducer instead of inventing its own rules:

* changing the workplace or mode is refused while a program runs or is paused;
* JOG is available only in `JOG`/`MPG`, steps are 0.001 / 0.01 / 0.1 mm;
* feed and spindle override are 0…150 % in steps of 10, rapid is the 5/25/50/100 row;
* an emergency stop and `ALARM` require a reset before any command;
* MDI understands `G90`/`G91`, `M3`/`M4`/`M5`, `M8`/`M9` and `X/Y/Z/A` coordinates; history keeps
  the last three commands.

## 4. The panel seam (`BackendBridge.qml`)

The screens do not know which backend is behind the contract. One object sits between them and
`MachineBackend`:

* **Values on read.** `backend.run`, `position`, `tool`, `programName`, `overrides`,
  `activeAlarmCount`, `clock` and the rest — the panel and the touch UI take numbers and labels only
  from here; the seam composes the labels (`runLabel` — "ГОТОВ"/"ВЫПОЛНЕНИЕ"/…, `toolLabel` — `T01`,
  `activeOffsetCode`) instead of every screen doing it.
* **Commands back.** `backend.cycleStart()`, `feedHold()`, `reset()`, `emergencyStop()`,
  `setWorkplace()`, `setMode()`, `jog()`, `mdi()`, `setOverride()`, `toggle()` and the others call
  the machine and return `true`/`false`.
* **A refusal is visible.** The reason lands in `backend.lastReason`; the screen shows it in the
  message line. Without a backend the reason is "Backend не подключён".
* **No "simulator or machine" branch.** The panel never asks who is behind the seam; `--backend` in
  `main.cpp` makes that choice.

Connected at this step: the run state, program name and title, message count and clock in the header
(panel and touch); the cycle, spindle and coolant commands; workplace and mode switching; the
position (DRO of the panel and the touch UI), jog, the MDI line, "go to part zero", "retract to the
safe point"; the feed/spindle/rapid overrides; the offset, tool and message tables (`offsetRows`,
`toolRows`, `alarmRows`) and the program catalogue (`programRows`); the block, the program progress
and the tools the program calls (`programToolRows`, taken from its `T`-words), the part counter and
the cycle clocks (`countPart`, `cycleTimeText`, `lastCycleText`), the load (`load`, `axisLoad`) and
the state sentence (`runStateText`). The screens did not have to be rewritten: the seam builds rows
of the shape they already read (sign and three decimals, `T01`, "Предупр.").

Still local (moving in the next slices, see §8): the program editor and its search, tool measuring as
a scenario, the probe wizard, the ladder and the service parameters (they are panel settings, a
separate store, not machine state).

## 5. Backend selection and launch

```
lynx-panel.exe --backend=sim --fixture=apps/qt-interface/backend/fixtures/auto-idle.json
lynx-panel.exe --backend=linuxcnc   # error: the adapter is not written yet
```

* `--backend` defaults to `sim`; `linuxcnc` stops the launch with a clear message. There is no
  silent fallback to the simulator: a panel on a machine must not show invented numbers. The panel
  is a GUI process, so when started from a console it reattaches to it and prints the reason; the
  exit code is 1.
* `--fixture` is a JSON path (a relative one is resolved from the repository root).
* `--freeze-clock=HH:mm:ss` freezes the clock for a repeatable screenshot.
* `--screen`, `--workplace`, `--theme` set the start state for debugging and screenshots.
* `--shot=file.png` and `--shot-delay=ms` grab the panel to PNG and exit. Screenshot mode loads
  `main.qml` directly (without `panel-host.qml`) so the state is applied before the frame; the window
  follows the panel canvas (1024×600 or 864×1536), and the canvas itself is grabbed. The shot also
  seeds the header clock from the frozen backend state, otherwise the frame would catch a running
  second.

Fixtures: `auto-idle.json` — AUTO with a full set of tools, offsets and messages;
`setup-probe.json` — SETUP with the probe; `test-dry.json` — TEST with dry run.

## 6. Comparison stand

| Tool | What it does |
|---|---|
| `tools/panel-shot.ps1` | Offscreen panel screenshot: fixture, screen, workplace, theme; writes a PNG to `build/artifacts/screenshots/` |
| `tools/panel-diff.ps1` | Compares a reference and a shot with a per-channel tolerance, writes a red mask of differences and fails when the budget is exceeded |

The comparison is not "byte equal": Chromium and Qt rasterise text differently. The channel
tolerance (`-Tolerance`, default 8) and the share of differing pixels (`-MaxDiff`, default 0.5 %)
define "pixel parity": geometry and colours match, font smoothing may differ.

```
powershell -NoProfile -ExecutionPolicy Bypass -File tools/panel-shot.ps1 `
    -Out build/artifacts/screenshots/auto-idle.png -Fixture auto-idle -Screen prog -Theme 0
powershell -NoProfile -ExecutionPolicy Bypass -File tools/panel-diff.ps1 `
    -Reference img/react/auto-idle.png -Actual build/artifacts/screenshots/auto-idle.png `
    -Diff build/artifacts/screenshots/auto-idle.diff.png
```

The scripts are written in UTF-8 with a BOM: Windows PowerShell 5.1 without a BOM reads Cyrillic as
ANSI and parses a line with a dash as an unclosed quote.

## 7. Checks

| Check | What it proves |
|---|---|
| `lynx-backend-test.exe` | 16 cases: fixture loading, a broken fixture keeps the state, workplace and JOG guards, START/PAUSE/RESET cycle, overrides, messages, fixture switching, clock freeze, program selection, offset commands (activate, clear, touch off), tool commands (life, length, select), single-message acknowledgement and the finished-part counter |
| `scripts/check-qt-panel.ps1` | QML panel and seam tests (37) + `lynx-backend-test.exe` (16) |
| `tools/panel-shot.ps1` + `panel-diff.ps1` | Panel screenshot and comparison with the reference within the difference budget |
| `scripts/build-qt-interface.ps1` | `/W4 /WX` build without warnings |

Environment variables for build and run: `CNC_VCVARS`, `CNC_NINJA`, `CNC_CMAKE`, `CNC_QT_DIR`,
`CNC_OCC_DIR` (defaults are in `scripts/build-qt-interface.ps1`).

## 8. What is not done yet

* The screens read `machine` through the seam: the header (run state, program, message count,
  clock), the position (DRO), jog, the MDI line, the safe point, part zero, the overrides, the
  offset/tool/message/program tables, the program block and progress, the program's tools, the part
  counter, the cycle clocks and the load are live; edits are machine commands (`setActiveOffset`,
  `touchOff`, `setToolLife`, `selectTool`, `acknowledgeAlarm`, `countPart`). The rest — the program
  editor, measuring, the probe, the ladder and the service parameters (a separate settings store) —
  is still local.
* There is no `LinuxCncBackend`: the HAL/`linuxcnc` adapter comes after stage 1; until then
  `--backend=linuxcnc` fails on purpose.
* Qt has no "Minimal" theme: `--theme` accepts 0…5, but only four palettes exist.
* The simulator does not play the viewer trajectory: it moves coordinates by program blocks but does
  not synchronise them with the 3D scene.
