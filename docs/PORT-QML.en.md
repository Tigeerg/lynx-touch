# PORT-QML.md — porting the operator panel from React to Qt (QML)

A document about **planned** work: bringing the Qt port of the panel (`apps/qt-interface/main.qml`)
into line with the React panel (`apps/react-interface`). The source of truth for behaviour is the
code and its run; this file holds the intent, anchors in both trees and acceptance.

This edition was merged on 31.10.2026: the inventory and anchors come from the previous edition,
the order of work, acceptance and decisions come from the lead. State at the day of the edition:
React `0.24.0` (product `0.34.0`), Qt `0.5.8`.

**Roles fixed (01.11.2026).** The real controller runs **Qt — it is the product**. React was a demo
stage: after the start and CAM-insert fixes it is **frozen** — no new features are developed there.
React stays the reference for texts, behaviour and tests; a defect found during the port is fixed in
React and moved to Qt at once.

The port exists and is not empty: `main.qml` is 3558 lines, `preview.qml` is a hand-maintained copy
of the same panel (3567 lines; edited together with `main.qml` until wave A), `assets/` and `fonts/`
sit next to them. Text parity is in place: `apps/react-interface/lib/cnc/qt-parity.test.ts` holds
19 shared labels and reads only `main.qml`. The rule is `CLAUDE.md`, section 5: **the port is
compared with the original word for word, not translated again**; a divergence must go red in a
test, not appear on the operator's screen.

**Why the plan was rewritten.** The previous edition described React `0.20.8`; since then the
0.21–0.23 waves (quick programming in the panel) and 0.28 (operator remarks: tool measure as an F1
window, the 6×5 keyboard, the tool block with the next tool, air blast/wash, the order of the side
column) have landed. The inventory in §1 was collected again with anchors in both trees so the
executing agent does not have to look for "where it lives".

**How to use the plan.** The waves in §4 go in order; each is a separate pass with its own
acceptance at gate G (`AI_WORKFLOW.md`, section 4). Steps refer to files and symbol names; line
numbers are a hint, not a contract: if a symbol is not found, stop and return to the lead. The
dispatcher's decisions are in §7.

## 1. Inventory: what Qt has and what it lacks

React anchors are a file and a symbol; line numbers are as of 31.10.2026. "W" is the wave in §4.

| Panel node | React `0.23.0` (source) | Qt `0.5.8` (`main.qml`) | Gap | W |
|---|---|---|---|---|
| Workplaces | `types.ts` (`SETUP`/`TEST`/`RUN`), `WORKPLACE_LABEL` (`data.ts`), `SET_WORKPLACE` (`nav.ts`) | none; only `machineMode` (`:24`) and `modeCycle` | no workplaces | C |
| Tabs | 5: `TABS`/`TAB_LABEL` (`types.ts`) Work, Offsets, Tool, Messages, Service; Work merges `pos` and `prog` (`tabOfScreen`) | 7: `tab0`..`tab6` (`:967-1256`), Position and Program separate, plus 3D | +2 tabs | B |
| Tab locking | removed in `0.16.0`; prohibitions live in actions (`guards.ts`) | `tabLocked` (`:100-104`), strike-through, `opacity: 0.4` | locking policy only in Qt | C |
| Softkeys: frame | 8 slots `SOFTKEY_SLOTS`, word cap `SOFTKEY_LABEL_MAX = 14` (`softkey-slots.ts`) | 7 slots: `padSoft` (`:275`) | count and cap | B, C |
| Softkeys: sets | window -> workplace (`setupPos`/`testWorkplace`/`runProg`) -> screen; `getSoftkeys` | `softkeysFor(screen)` (`:280-339`) — screen only | no workplace branch | C |
| Disabled key reason | `reason`, `FALLBACK_REASON`, a press of a disabled key sends `HINT` | `disabled: true` -> `opacity: 0.4`, press is silent | disabled key stays silent | C |
| SETUP (work->pos) | `setupPos`: tool measure, 3D probe, base write, reserve, safe retract, part zero, warm-up, calculator | steps 0.001/0.01/0.1, clear offset, coolant, spindle | no setup set | C, D |
| TEST (work->prog) | `testWorkplace`: program catalogue, editor, search/replace, run mode, single block, M01, program extents, display | open file, edit, single block, optional stop, dry run, rewind, start from block; first three are stubs | no simulation, extents, graphics clear | C, D |
| RUN (work->prog) | `runProg`; screen: distance-to-go, 4 views (`AUTO_VIEWS`), transparent code window | none | no RUN screen | C, D |
| Tool measure | `tool-measure.ts` (phases, timings 1.4/1.8/0.5/1.1 s), `tool-measure-dialog/` | "Measure length" without behaviour | no wizard | E |
| Probe wizard | `probe.ts`/`probe-shapes.ts` (`PROBE_CYCLES`, `PROBE_TOOL = 99`, `PROBE_STEPS`), `probe/probe-wizard/` | texts exist, `pWrite` just closes | no rules | E |
| Hardware keyboard | `hardware/hardware-panel/` 6×5, keys 72×72, `keys.ts` | none; only `Keys.onPressed` | no pendant | F |
| MPG pendant | `side-status/jog.tsx`, `MPG_STEPS`, mode `MPG` | no separate pendant; `MPG` only as a mode | no handwheel emulator | F |
| Side column | `side-status/`: tool (current + next), correctors S/F/G0, active G, load, execution | `sideStatus` "MODES", outdated | composition and order | B, D |
| MDI line | in SETUP on `SetupScreen` (keyboard and touch) | only the MDI mode screen inside `pos` | not as in React | D |
| Themes | 6: `THEME_OPTIONS`, `THEME_CLASSES`, Minimal tokens in `globals.css` | 4 numeric palettes (`pal_light/dark/graphite/mint`), radii 8/6/4/3/2 | no Minimal, no tokens | E |
| Touch mode | `ui-mode-context.ts`, `touch/touch-deck/` | none | no touch mode | E |
| Texts | `SOFTKEY_LABEL`, `MODE_LABEL`, `WORKPLACE_LABEL`, `TAB_LABEL`, `SCREEN_LABEL`; 19 lines checked | Russian literals inline; 19 labels checked | parity covers little | A |
| Quick program (CAM) | `lib/cnc/cam/` — 141 files, 15 kinds; `components/cnc/cam/` — 25 files | none; C++ `apps/quickprog` is a console demo with 4 kinds. The React entry is restored in `0.33.2`: F8 "Быстрая УП" in the program editor | no panel CAM | G |

Consequence: "word-for-word port" rests on 19 checked lines; everything else is from memory and
has already diverged (tab count, slot count, sets, locking, side column).

## 2. Porting rules (apply to every wave)

1. **The source of texts and sets is React.** Labels come from `SOFTKEY_LABEL` and the neighbouring
   `data.ts`/`types.ts` tables; no new names are invented in QML. Parity grows with every node.
2. **`preview.qml` is a wrapper, not a copy.** In wave A it becomes a ~10-line window over
   `main.qml` (`Main { anchors.fill: parent }`); the 3567-line hand copy is deleted. This removes a
   whole class of drift and the "edit both files" rule.
3. **The machine manifest holds texts and sets only.** A generator on the `lib/cnc` side writes
   `apps/qt-interface/panel-data.js`: tabs and labels, workplaces, F1–F8 sets `{ id, label }` by
   workplace and screen, `MODE_LABEL`, `SCREEN_LABEL`, `SOFTKEY_LABEL`. The format is a
   QML-compatible JS module (`.pragma library`), not JSON. The guard checks freshness.
4. **Theme tokens live apart from the text manifest.** The `.theme-minimal` CSS overrides (~350
   lines) are not parsed by regex. One TS token file (or a one-off CSS generator) is the source;
   the guard compares it with `globals.css`, QML reads ready name -> value pairs.
5. **Repeat behaviour, not code.** The reducer, React components and Tailwind are not ported; the
   same rules (machine busy, emergency stop, refusal reason) are short functions next to `root` or
   inside extracted components. One "action id -> behaviour" table; no special paths.
6. **Split, do not grow the monolith.** New nodes become QML components next to `main.qml`
   (`SoftkeyBar.qml`, `WorkScreen.qml`, `AutoScreen.qml`, `PendantWindow.qml`, `MpgWindow.qml`,
   `ToolMeasureDialog.qml`, `Theme.qml`, `TouchDeck.qml`). `main.qml` stays the frame: header,
   tabs, screen stack, bar. The guard holds file sizes.
7. **Versions are separate.** The port bumps `qt-interface` and the product; `react-interface` is
   bumped only if its code or texts changed (`VERSIONING.md`, section 2). The viewer and test data
   are untouched. The journal is the root `CHANGELOG.md`.
8. **Verify by running, not by eye.** The automatic `panel_probe` (offscreen QML load) checks
   counts and sets; `preview.qml` is the visual check without 3D; the RUN screen with the toolpath
   is checked in a live viewer window. A screenshot of the key state goes to `img/`.
9. **The panel is 1024×600 and a word must fit the slot.** A softkey label word is at most
   `SOFTKEY_LABEL_MAX = 14` characters; it is part of the check, not taste. Other labels (tabs,
   header) are checked by the same probe.
10. **Operation without a touchscreen is an equal mode.** Every screen, dialog and action is
   reachable from the physical keys and the keyboard: `F1`..`F8`, arrows, `Tab`, `Enter`, `Esc`,
   digits, the jog pad and the handwheel. Touch is an additional way, never the only one; mouse and
   hover are optional. This is checked by a "keyboard only" scenario on every screen, not by eye.

## 3. React -> Qt map (where the executor looks)

| Subsystem | React (source of truth) | Qt (receiver) |
|---|---|---|
| Workplace and mode | `lib/cnc/nav.ts` (`SET_WORKPLACE`, `SET_MODE`), `types.ts` | `workplace` property on `root`, `setWorkplace()` |
| Tabs and screens | `types.ts` (`TABS`, `TAB_LABEL`, `SCREEN_LABEL`), `cnc-panel.tsx` | `tabsRow` + `screensStack` in `main.qml` |
| Softkey bar | `lib/cnc/softkeys.ts`, `softkeys/` | `SoftkeyBar.qml` + `panel-data.js` |
| Refusal reason | `softkey-slots.ts` (`reason`, `HINT`), `guards.ts` | `SoftkeyBar.qml` + message line |
| SETUP | `screens/setup-screen.tsx`, set `setupPos` | `WorkScreen.qml`, screen `pos` |
| TEST | `screens/test-screen.tsx`, `program-screen.tsx`, `testWorkplace` | `WorkScreen.qml`, screen `prog` |
| RUN | `auto-screen.tsx`, `runProg`, `AUTO_VIEWS` | `AutoScreen.qml` |
| Tool measure | `tool-measure.ts`, `tool-measure-dialog/` | `ToolMeasureDialog.qml` |
| Probe wizard | `probe.ts`, `probe-shapes.ts`, `probe/probe-wizard/` | rules next to `root`, UI in `offset` |
| Pendant keyboard | `hardware/hardware-panel/`, `keys.ts` | `PendantWindow.qml` (separate window, wave F) |
| MPG pendant | `side-status/jog.tsx`, `MPG_STEPS`, mode `MPG` | `MpgWindow.qml` (separate window, wave F) |
| Side column | `components/cnc/side-status/` | column next to the screen stack |
| Themes | `lib/cnc/theme.ts`, `params/display.ts`, `globals.css` | `Theme.qml`, tokens from a separate TS source |
| Touch mode | `components/cnc/touch/`, `ui-mode-context.ts` | `TouchDeck.qml`, a mode flag |
| Quick program (CAM) | `lib/cnc/cam/`, `components/cnc/cam/` | wave G: C++/QML core, see §4 |
| Text parity | `qt-parity.test.ts` | extended over all `*.qml` |

## 4. Waves and steps

Each wave ends with acceptance that goes red on a divergence. Estimates are rough, for the
dispatcher. "Pre." are preconditions; steps run in order; checks are green after a step before the
next one starts.

### Wave A. Foundation (2-3 days)

Pre.: none.

1. **`preview.qml` is a wrapper.** Replace the copy with a 1024×600 `Window` holding
   `Main { anchors.fill: parent }`; confirm `main.qml` registers as type `Main` from the same
   directory and FontLoader finds `fonts/`. Acceptance: `preview.qml` is under 20 lines and the
   panel starts as before.
2. **`panel-data.js` generator.** A script on the `react-interface` side (TypeScript next to
   `lib/cnc`, run via `pnpm`) writes tabs/labels, workplaces, F1–F8 sets by workplace and screen
   (id + label, no behaviour), `MODE_LABEL`, `SCREEN_LABEL`, `SOFTKEY_LABEL`. Format: a JS module
   for QML, not JSON; Russian text in UTF-8.
3. **Parity reads the whole port.** `qt-parity.test.ts` walks `apps/qt-interface/**/*.qml`, not only
   `main.qml`; for every set it compares slot ids and labels, 5 tabs and workplace labels. The old
   19 `SHARED` lines stay until the end of wave C.
4. **`panel_probe` skeleton.** Offscreen load of `main.qml` with checks: 5 tabs, 8 slots, theme
   list, text without U+FFFD. Run by the guard, no manual step. (Before wave B some checks may be
   `xfail`; the mechanism must exist.)
5. **Guard.** A step in `tools/check.ps1`: rebuild `panel-data.js` and compare with the committed
   copy; a divergence is a `FAIL` naming the field. Versions and `CHANGELOG.md` in the same commit.
6. **The product entry point.** The panel does not start on the controller through `qmlscene`: it
   needs a proper launch (QtVCP/QML loader or an own exe without a console) with the `MockBackend`
   and `CNC.Edges` paths. `qmlscene` stays a debugging tool. The Qt port version is bumped in the
   same commit. **Done on Windows:** `scripts/build-qt-interface.ps1` builds
   `build/qt-interface/lynx-panel.exe`; the QtVCP loader comes later on the controller.

Checks: `pnpm test`, `pnpm guard`, `preview.qml` run.
Acceptance: changing a label in `data.ts` without rebuilding the manifest goes red; no
`preview.qml` copy; `panel_probe` runs from the guard.
Risk: the manifest is awkward for QML. Decision: a JS module (fixed), proven on one set before
exporting them all.

### Wave B. Visible frame (3-4 days)

Pre.: A.

1. Header per `header-bar.tsx`: workplaces (3 buttons), program name, alarm count, "Temp", "Start",
   clock; no duplicate workplace icon.
2. Tabs: 7 -> 5, labels from the manifest; Work merges `pos` and `prog`; the 3D tab leaves the panel
   no earlier than the viewer returns to Work (wave D) — until then it stays temporary so a working
   feature is not lost.
3. `WorkScreen.qml`: coordinate strip (text-2xl, 72 px), trajectory preview with views, right
   `side-status` column (tool, correctors, active codes, load, execution) on existing data, no new
   logic.
4. `SoftkeyBar.qml`: 8 slots, F1..F8 badges, lucide icon, label; data from `softkeysFor` for now,
   the manifest switch is wave C. The old 7-slot `padSoft` is deleted.

Checks: `panel_probe` (5 tabs, 8 slots), `preview.qml`, screenshot in `img/`.
Acceptance: the 1024×600 screen matches React in the scope of header, tabs, coordinate strip,
trajectory window, side column and bar.
Risk: `main.qml` is 3558 lines — extract components immediately, not after the wave.

### Wave C. Workplaces, state and sets (3-4 days)

Pre.: A, B.

1. `workplace` property (`SETUP`/`TEST`/`RUN`), three buttons in the order RUN, TEST, SETUP;
   `setWorkplace()` mirrors `nav.ts`: refuse while the machine is busy, `SETUP` -> `JOG`,
   `TEST`/`RUN` -> `AUTO`, `dryRun = (wp === 'TEST')`.
2. `machineMode` becomes derived from `workplace` + manual switching; one source.
3. Remove `tabLocked`; prohibitions move into actions (`guards.ts`): offset write while busy, motion
   on emergency stop.
4. F1–F8 sets from the manifest: window -> workplace -> screen, as `getSoftkeys`. The "action id ->
   behaviour" table sits next to `root`; a test compares its id set with the manifest.
5. Refusal reason: lock icon and hint on a disabled key; a press prints `reason` to the message
   line (default `FALLBACK_REASON`).

Checks: `panel_probe` (sets by workplace, reasons), `preview.qml` in every mode.
Acceptance: switching workplaces changes screen, mode and set; no tab is struck through; a disabled
key names its reason.
Risk: two mode systems. Decision: derived `machineMode` (step 2).

### Wave D. Screen content and the viewer (4-6 days)

Pre.: B, C.

1. `WorkScreen.qml`: SETUP — coordinates + MDI line; TEST — program view with code curtain,
   "Clear graphics"; RUN — delegates to `AutoScreen.qml`.
2. `AutoScreen.qml`: position strip, "mm to block", distance-to-go, Single block/M01/Dry run lamps,
   4 views (`AUTO_VIEWS`), transparent code window.
3. The viewer as a child (the `MockBackend`/`GcodeToolpath` plugin already exists; load the program
   from the catalogue, not `viewSample`). After this the temporary 3D tab is removed.
4. Single-block viewing and "Start from block" (`PROG_TO_CURSOR`), restoring modal codes.
5. Program editor and search/replace.

Checks: `panel_probe`; `preview.qml` — views and strip without 3D; live window — toolpath, code
window, single-block step.
Acceptance: F8 cycles 4 views; "Machining" is not hidden; the code window does not cover the header;
the program loads from the catalogue.
Risk: offscreen does not draw Quick3D — some checks are live-window only (`UNVERIFIED`).

### Wave E. Wizards, themes, touch (6-8 days)

Pre.: C, D (softkey windows from C).

1. `ToolMeasureDialog.qml`: approach parameters, sensor scene, phases
   `idle->rapid->fine->touch->retract->done` with the same timings; F1 in SETUP only opens the
   window, the start is "Start measurement" inside; emergency stop interrupts.
2. Probe wizard: `PROBE_STEPS`, `probe.ts` rules, `PROBE_TOOL = 99` requirement, writing the result
   to an offset (`pWrite` currently closes), repeat.
3. Softkey windows (`fileDialog`, `toolDialog`, `calc`, `search`, `toolMeasure`): the window rule
   overrides the screen set.
4. `Theme.qml`: 6 themes from the separate token source; Minimal — 1 px lines, 0 gap, radius from
   `--radius`, no shadows; hard-coded radii become a token.
5. `TouchDeck.qml`: touch deck, softkeys without F labels, Start/Stop/Reset/Emergency row only on
   Work, MDI in the SETUP bottom zone, combined view vertical; the mode is a `[DISPLAY] UI_MODE`
   parameter.

Checks: `panel_probe` (themes, probe steps), `preview.qml` in every theme and both modes.
Acceptance: measuring through to a written length; emergency stop interrupts; the probe measuring
step requires T99; the write goes to an offset; a theme change keeps geometry; no F labels in touch
mode.
Risk: portrait 1080×1920 does not fit the 1024×600 preview — checked in a separate window and
honestly marked.

### Wave F. Pendant emulators: keyboard and MPG (3-4 days)

Pre.: B, C (shared action table and header).

Purpose — debugging on real hardware before the physical pendant exists. The windows are
**separate windows** over the panel, not part of the main screen.

1. **`PendantWindow.qml`** — virtual pendant: 6×5 grid (digits without Shift, direct letters
   `G M X Y Z S F T N O A B C D`), 72×72 keys, Start/Stop/Reset/Emergency, air/wash/coolant/
   handwheel, jog, multipliers, spindle/feed/rapid. Behaviour comes from the same action table as
   the main screen: the window has no logic of its own.
2. **`MpgWindow.qml`** — handwheel emulator: axis `X/Y/Z/A`, multiplier `×1/×10/×100`
   (`MPG_STEPS`), wheel (drag or `+/-` buttons), pulses go through the same action as the physical
   MPG; the step per pulse comes from the multiplier.
3. **Opening from the interface.** In the service screen, a "Debug" section (or a `[DEBUG]`
   softkey) with "Virtual pendant" and "MPG pendant" buttons. The windows are `Window`s with
   `visible` from a `root` property; pressing again raises the existing window, a second one is not
   created. The section is hidden in normal work so it does not distract the operator.
4. **Safety.** The windows are a debugging tool: actions pass the same prohibitions (busy,
   emergency stop) as the main screen; a window does not bypass `guards.ts`.
5. **Acceptance on hardware.** In QtVCP the window opens over the panel, a pendant key changes the
   same state and message line as a physical key; MPG at `×10` moves the axis exactly 0.01 mm per
   pulse; closing the window does not reset state.

Checks: `panel_probe` — windows open/close, repeated open does not multiply windows; `preview.qml`
+ live window; pendant and MPG screenshots in `img/`.
Acceptance: both buttons in Debug open separate windows; actions match the main screen; MPG moves
the selected axis by the selected step.
Risk: a second window needs `Window` and `QtQuick.Window`; QtVCP supports it, but it is checked in
the same runtime the panel runs in.

### Wave G. Quick program (CAM) — separate work (8-12 days)

Pre.: waves A-F and H (CAM after the interface port).

**What already exists.** CAM is implemented in React in TypeScript: `lib/cnc/cam/` (141 files, 15
operation kinds, planner, emitter, `.qprog`, tests) and `components/cnc/cam/` (25 files, overlay).
In C++ there is only the console demo `apps/quickprog`: 608 self-check cases, plans **four** kinds
(face, pocket, circle pocket, drill), reads a `.qprog` with all 15 kinds and skips the unsupported
ones with a summary line. There is no panel CAM in C++ or Qt.

**Entry (fixed in `0.33.2`).** Previously the React panel had no path to CAM through the
interface: the "Быстрая УП" softkey was declared only in `progScreen` and was shadowed by the
workplace sets (`testWorkplace`/`runProg`) and by the editor set. Now F8 "Быстрая УП" is in the
program edit mode, and the same slot closes the overlay; a softkey test holds the slot. The
operator either edits the open file or creates a new one and opens the quick program from the
editor.

1. **The carrier is the C++ core (decided, §7).** Grow `apps/quickprog` to 15 kinds: types and the
   `.qprog` document, then the kind planners, then the emitter. QML gets the core as a library with
   a thin wrapper (type/facade), not a copy of the algorithms. The TypeScript implementation stays
   the reference for comparing the output, not a second carrier.
2. **Order of kinds.** First compare the four already ported kinds (face, pocket, circle pocket,
   drill) with React byte for byte, then add the rest one by one: contour, slot, chamfer, tap,
   counterbore, thread mill, engraving, pocket with island, rest, probe, custom G-code. Every kind
   is covered by a reference test.
3. **`CamOverlay.qml`:** operation list, forms, preview, save with `.bak`, insert into the open
   program, program check.
4. **Opening** — from the TEST screen, as in React; in touch mode with a separate button. The React
   editor entry is already restored (`0.33.2`); Qt gets it in this wave.
5. **Order.** The C++ core comes after interface parity (A-F, H), not in parallel: first the Qt
   panel, then CAM. This is the dispatcher's decision of 01.11.2026.

Checks: C++ `quickprog_check`, new kind tests, golden program comparison, `panel_probe`.
Acceptance: all 15 kinds plan and emit the same program as React on the demo part.
Risk: two sources of algorithms. Decision: one carrier (C++ or JS), the other is removed.

### Wave H. Operation without a touchscreen: physical keys (2-3 days)

Pre.: waves A-F; closes after them because it checks every screen at once.

The goal is a panel fully driven by the physical console and the keyboard, without touches or a
mouse.

1. **A complete key map.** Every action on a screen has a key or a sequence: `F1`..`F8` are the
   softkeys, arrows/`Tab` move focus and lists, `Enter` starts or confirms, `Esc` resets or closes,
   digits select tabs and type, letters type MDI. The map is compared with `keys.ts` and the softkey
   set: no action without a key, no key without a label.
2. **Focus and selection.** A visible focus ring, a cursor in lists and tables, `PgUp`/`PgDn`, form
   traversal with `Tab`/`Shift+Tab`, `Enter` confirms a field. Hints do not depend on hover; a
   disabled key names its reason in the message line.
3. **The panel layout.** The screen does not require touch zones: the touch deck and large touch
   buttons are hidden or collapsed, their actions stay reachable from the console. 1024×600, labels
   fit the slot.
4. **Dialogs without gestures.** Every window (file, tool, calculator, search, measure, probe, CAM)
   opens, navigates and closes from the keyboard. Dragging is optional: lists and fields have keys;
   the MPG emulator works from the keyboard too (`+`/`-`, arrows).
5. **The "keyboard only" scenario.** Walk the full operator path without touches or a mouse: start,
   select a program, edit, CAM, save/insert, check, start with confirmation, stop, reset. For every
   screen — a screenshot and the recorded keys.

Checks: key routing tests (`keys.test.ts`), `panel_probe` (focus and keys), a `preview.qml` run
without a mouse, screenshots in `img/`.
Acceptance: no action requires a touch; the scenario is passed with the keyboard alone; every
action shows its key; a disabled key explains its reason.
Risk: touch gestures (swipe, drag) may remain the only way somewhere — the scenario finds them and
a key is added.

## 5. Order and estimate

First A -> B -> C: after them the frame, workplaces and sets are checked by machine, and the first
visible result appears in B. Then D (screens and viewer), E (wizards, themes, touch), F (emulators
for debugging on hardware). CAM (G) is a separate wave after visual parity.

Rough estimate: A 2-3, B 3-4, C 3-4, D 4-6, E 6-8, F 3-4 days — **21-29 days** for one person to
visual parity; H (operation without a touchscreen) is another **2-3 days** and closes after A-F;
G (CAM) is **8-12 days** separately.

**The first pass.** Start with **A (foundation)** and immediately **B (visible frame)**: after B the
panel already looks like React and can be shown on the controller. Then C and F, then D, E, H. CAM
(G) is separate after the interface port, as the dispatcher decided.

## 6. General risks

* **Monolith.** `main.qml` at 3558 lines is split across waves (rule §2.6); without it the changes
  stop fitting in a head. The guard holds sizes like the `machine.ts` mark.
* **Three sources of text.** While React, QML and the manifest coexist, any two can diverge. The
  manifest (wave A) closes this by machine; without it waves B-E drift again by hand.
* **Hand copy of `preview.qml`.** A risk before wave A; after it is a wrapper and the copy is gone.
* **Two pendants.** The virtual pendant (wave F) must not introduce a second action table: one
  shared table only. Otherwise a key on the screen and on the pendant diverge.
* **Qt runtime.** The panel starts only in a runtime with the `MockBackend` plugin
  (`build/qmlbackend/...`); a `qml` of another minor version will not load the module
  (`docs/PORT-CPP.md`, §11). Check in the same runtime the panel runs in.
* **Live 3D only in a window.** Offscreen does not draw Quick3D; the RUN screen with the toolpath is
  accepted in a live window.
* **CAM — a second carrier.** TypeScript stays the reference, the product uses the C++ core; there
  is no second implementation in the product, otherwise the algorithms diverge.

## 7. Dispatcher decisions (fixed 31.10.2026)

1. **Manifest `panel-data.js` — do it** (wave A). The generator lives in `react-interface`
   (`lib/cnc` is the source of texts), the guard is in `tools/check.ps1`.
2. **The viewer is a child of the panel** on the RUN screen (not a separate window); the program is
   loaded from the catalogue, linked with `docs/PORT-CPP.md`.
3. **Touch mode — port it** (wave E in full).
4. **Themes — all 6**, Minimal tokens from a separate TS source, not by hand.
5. **Virtual pendant — needed, in a separate window** from the interface (wave F). It is needed for
   debugging on real hardware while there is no physical panel.
6. **MPG pendant emulator — needed, in a separate window** from the interface (wave F), with axis
   and multiplier selection and the same actions as the physical handwheel.
7. **`preview.qml` — a wrapper**, the hand copy is deleted (wave A).
8. **CAM (quick program) — a separate wave after the interface port** (G), after A-F and H; the
   carrier is C++ (decision 11).
9. **Operation without a touchscreen is mandatory** (wave H): every screen and dialog is driven by
   physical keys and the keyboard; touch is an additional way.
10. **Qt is the product, React is the reference.** The controller runs Qt; React is frozen after
    stabilization (start, CAM insert) and serves as the reference for texts, behaviour and tests.
    New features go to Qt only.
11. **CAM uses the C++ core.** `apps/quickprog` grows to 15 kinds and is exposed to QML through a
    wrapper; TypeScript stays the reference for comparing the output, no second implementation in
    the product.
12. **The debug entry point is a separate exe.** On Windows the panel runs as
    `build/qt-interface/lynx-panel.exe` (built by `scripts/build-qt-interface.ps1`); on the
    controller a QtVCP loader comes later. `qmlscene` is only the fallback debugging path.

## 8. Out of scope

The viewer port (separate plan — `docs/PORT-CPP.md`), changes to the React interface itself, test
NC programs and the postprocessor. CAM is wave G: its C++ core starts in parallel with B/C/D, the
overlay after the frame.
