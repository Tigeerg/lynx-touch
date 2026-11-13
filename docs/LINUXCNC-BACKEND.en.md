# LINUXCNC-BACKEND.md — the LinuxCNC adapter

Record of the work that connects the controller to the panel: the machine gets a second backend
next to the simulator. The same file holds the honest gap list: what the contract asks for that
`stat`/HAL cannot answer, and what was decided about each.

Terms: the human dictionary is [`ROLES.md`](../ROLES.md), section 9; every term is explained at its
first use (`AI_WORKFLOW.md`, section 0.2).

## 1. Layers and the border

```
LinuxCNC (stat/command, HAL)
        │  raw facts: joint indexes, task/interp codes, tool table, operator message list
        ▼
LinuxCncLink        — the interface (backend/linuxcnc_link.h), no contract types
        │
LinuxCncBackend     — the mapping onto the contract (backend/linuxcnc_backend.cpp), tested
        │
BackendBridge.qml   — the panel seam
        │
the screens (main.qml and the components)
```

Why this shape: the library call is the risky part and there is no LinuxCNC on a development machine.
The mapping is therefore split from the library call: **the mapping is written and checked here** (13
tests against a fake link), while **the library call is written on the controller** from the task card
in `backend/linuxcnc_nml_link.cpp`, which lists which `stat` field feeds which link method.

## 2. Build

* LinuxCNC is enabled by a CMake option: `-DLYNX_WITH_LINUXCNC=ON` (default `OFF`). With it the real
  link is built and the controller's libraries are linked; without it the adapter and its tests are
  built.
* `--backend=linuxcnc` without the link stops the launch with a readable message and exit code 1,
  **with no silent fallback to the simulator**: a panel on a machine must not show invented numbers.
* The adapter check is part of `scripts/check-qt-panel.ps1`: `lynx-linuxcnc-test` (13 cases) and
  `lynx-backend-test` (16 cases) run next to the QML tests (37).

## 3. The mapping (already written and checked)

| Panel (contract) | LinuxCNC | Rule |
|---|---|---|
| `run` | `stat.estop`, `stat.task_state`, `stat.interp_state` | `ESTOP` → `ALARM` (`task_state = ERROR`) → `HOLD` (`interp_state = PAUSED`) → `RUN` (`task_state = EXEC`) → else `IDLE` |
| `mode` | `stat.task_mode` | `auto` → `AUTO`, `mdi` → `MDI`, `manual` → `JOG` (the machine has no `MPG` mode — that is a panel step) |
| `position` | `stat.joint_position` minus the active `stat.g5x_offset` | the work position (the DRO) |
| `machinePosition` | `stat.joint_position` | the machine position |
| `offsets` | `stat.g5x_offset[0..5]` | G54…G59 rows, the active one from `stat.g5x_index` |
| `tools` | `stat.tool_table[]`, `stat.tool_in_spindle` | number, name, diameter, length; life — see gap 3 |
| `overrides` | `stat.feedrate`, `stat.spindle_scale`, `stat.rapid_scale` | percent; the rapid row is 5/25/50/100 |
| `singleBlock`, `optStop`, `dryRun` | `stat.single_block`, `stat.optional_stop`, the dry-run flag | to confirm on the stand |
| `program` | `stat.file`, the file lines, `stat.motion_line`, `stat.current_line` | the active program and block |
| `alarms` | the operator message channel | "code text" is split; the level is gap 4 |
| `clock` | the system clock | the shift clock, as in React |
| commands | `command.task_*`, `task_mode`, `jog`, `mdi`, `program_open`, `override`, the tool table, `g5x` | a machine refusal comes back as a line |

## 4. Gaps: what the contract asks for that the machine cannot answer

The React panel keeps these concepts itself; they entered the contract from the panel, not from the
machine. Each has a decision about where it will come from; until then the adapter has an honest stub.

1. **The workplace `SETUP`/`TEST`/`RUN`.** LinuxCNC has no such state: it is panel navigation.
   *Decision:* it stays panel state; the adapter returns empty (`workplace()` → `""`) and
   `setWorkplace()` accepts the call without touching the machine. On the stand a workplace may expand
   into a mode (`manual`/`auto`) and the dry run.
2. **The program catalogue: the file list and the date.** `stat` knows one open program.
   *Decision:* a separate store (like the settings), not `stat`; the adapter returns one row — the
   controller's file. `loadProgram(1..n)` refuses.
3. **Tool life and per-tool cutting time.** Not in `stat`.
   *Decision:* `setToolLife` refuses ("not stored in LinuxCNC"), `tools[].life` = 100, `toolTime`
   empty. On the stand, a separate account (a file or HAL) fills the field.
4. **The message level (`error`/`warning`/`info`).** The message channel carries no level.
   *Decision:* the adapter says `info`; a code-range mapping comes when the stand confirms the table.
5. **The probe result.** `stat` has none; the program or the panel wizard drives the probe.
   *Decision:* the result comes from the wizard or a HAL pin; empty until then.
6. **Spindle and per-axis load.** These are HAL pins (`spindle.N.load` and the like), not `stat`.
   *Decision:* the link reads the pins through HAL access; until then `load()` = 0 and `axisLoad` is
   zeroed.
7. **The cycle clock and the finished-part counter.** Not in `stat`.
   *Decision:* the panel can count the cycle from the `RUN` state; the part counter is the panel's or
   the program's. `countPart()` refuses today; later either a HAL timer or the panel's own count.
8. **"Start from cursor" (run from line).** It needs the interpreter's run-from-line command.
   *Decision:* today the adapter maps it to an ordinary cycle start and that is marked; on the stand
   it becomes a real run from line.
9. **`toolTime`, `probe.step`, the message archive.** Small items of the same kind: from the panel or
   from HAL.

The shared rule: **the adapter invents nothing.** Where the machine has no number, the answer is a
refusal with a reason or an empty value, never a plausible-looking figure.

## 5. Task card for the stand

1. Build with `-DLYNX_WITH_LINUXCNC=ON`, wiring the LinuxCNC headers and libraries (stat/command,
   NML) into `CMakeLists.txt`.
2. Write `LinuxCncNmlLink` in `backend/linuxcnc_nml_link.cpp` from the list in that file's comment:
   `stat` fields → link methods, commands → `command`. The mapping needs no change — it is covered by
   tests, and it should only change together with a test.
3. Run `scripts/check-qt-panel.ps1` (the adapter and the panel) and `--backend=linuxcnc` on the stand.
4. Acceptance: the panel shows **real** coordinates, mode, program, overrides and messages;
   START/PAUSE/RESET/ESTOP and the overrides reach the machine; a machine refusal is visible as a line.
5. Fill gaps 1–9 one at a time, each with a fake-link test and a `CHANGELOG.md` entry.
