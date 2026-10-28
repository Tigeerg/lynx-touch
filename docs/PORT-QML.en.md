# PORT-QML.en.md — porting the operator panel from React to Qt (QML)

This is a **plan**, not a report of finished work: bringing the Qt port of the panel
(`apps/qt-interface/main.qml`) into line with the React panel (`apps/react-interface`). The source of
truth about behaviour is the code and its run; this file holds the intent, the numbers and the caveats.

The port exists and is far from empty: `main.qml` is 3453 lines (267 KB), `preview.qml` is a thin
`Window` 1024x600 wrapper over the same file, with `assets/` and `fonts/` beside it. Text parity is
already wired: `apps/react-interface/lib/cnc/qt-parity.test.ts` pins 19 shared labels. The rule is
`CLAUDE.md`, section 5: **the port is checked against the original word for word, not translated
again**, so a drift must fail a test instead of surfacing on the operator's screen.

**Why the plan is rewritten.** The React panel moved far ahead over the last passes: operator
workplaces (SETUP/TEST/RUN), the merge of Position and Program into a single Work tab, per-workplace
F1-F8 sets, the AUTO screen with Distance-to-Go and a transparent code window, the animated tool
measure, the Minimal theme, the removal of tab locking. The Qt port stayed at roughly React
`0.12.0`-`0.14.x`. Current numbers: React `0.20.5`, Qt `0.5.8`.

## 1. Inventory: what Qt has and what it lacks

| Panel node | React `0.20.5` | Qt `0.5.8` (`main.qml`) | Gap |
|---|---|---|---|
| Top level | Workplaces `SETUP`/`TEST`/`RUN`, buttons ordered AUTO, TEST, SETUP; a workplace sets the machine mode and dry run | only `machineMode` (AUTO/MDI/JOG/MPG) with a mode switch | no workplaces |
| Tabs | 5: Work, Offsets, Tool, Messages, Service; Work covers Position and Program | 6: Position, Program, Offsets, Tool, Messages, Service | +1 tab |
| Tab locking | removed: any screen in any mode | `tabLocked` (`:100`): AUTO locks offsets/tool/service, MDI locks program/service; strikeout and `MouseArea.enabled: !locked` | the whole locking policy lives only in Qt |
| Softkeys | 8 slots F1-F8, the set depends on the workplace and the screen (`softkeys.ts:166-240`) | `softkeysFor(screen)` (`:280`), 2-7 keys, `padSoft` pads to **7** (`:275`) | set, slot count, screen branch instead of workplace |
| SETUP (preparation) | Tool measure, 3D probe, Base write, Base table, Safe retract, Go to part zero, Warm-up, Calculator | none; Position carries steps 0.001/0.01/0.1, zero relative, coolant, spindle | no preparation set |
| TEST | File catalogue, Editor, Search/replace, Dry run, Program bounds, Graphics clear, View, Hide code | Program carries Open file, Edit, Single block, Optional stop, Dry run, Rewind, Start from block | no program review, no graphics clear |
| AUTO | Distance-to-Go strip, one shared header, transparent code window over the toolpath, three views in a cycle (trajectory / gcode / both), legend on the right | none | no AUTO screen |
| Work screen | `SetupScreen`/`ProgramScreen`/`TestScreen`/`AutoScreen` by workplace | one layout per tab | no split by workplace |
| Tool measure | subsystem with phases (rapid and fine approach, touch, retract) and a modal dialog with phase and delta | the label "Замер длины" with no behaviour | no wizard |
| Probe wizard | steps, T99 call, write to an offset, retry (`probe-wizard.ts`) | `probeSteps` and key labels (`:184`, `:300-308`) | texts only, no rules |
| Disabled key | a reason beside it: a lock icon, a hint and a message line | `disabled: true`, no hint | no refusal reason |
| Themes | 5, including Minimal (1 px lines, no shadows or gradients, rounding from `--radius`, panel gap 0) | one light palette, radii hardcoded `radius: 8/6/4` | no themes, no tokens |
| Touch mode | `touch-ui`: softkeys without the F1-F8 keycap, a touch deck, the Start/Stop/Reset/Emergency row hidden on every tab but Work | none | no touch mode |
| Texts | `SOFTKEY_LABEL` (`data.ts`), one source per operation | 19 labels pinned, the rest are copies | parity covers but a part |
| Where the state lives | reducer (`lib/cnc`), subsystems | properties and functions of `root`, array copies | different mechanics |

Consequence: the "word for word" rule is held by those 19 lines today. Everything else relies on memory.

## 2. Port rules (agreed once, valid for every stage)

1. **React is the source of texts and sets.** Labels come from `SOFTKEY_LABEL` and the neighbouring
   tables in `data.ts`; no new names are invented in QML. `qt-parity.test.ts` grows with every new node.
2. **A generated panel manifest instead of hand copies.** A script (from `lib/cnc`) emits
   `apps/qt-interface/panel-data.js`: the tabs and their labels, the workplaces, the F1-F8 sets by
   workplace and screen, the operation labels. Qt reads that file and keeps no tables by hand. Its
   freshness is a guard step: `panel-data.js` is rebuilt from `lib/cnc` and compared with what is in the
   repository. That removes the whole class of "a label drifted" defects.
3. **Repeat the behaviour, not the code.** The reducer, the React components and Tailwind are not
   ported; the same rules (refuse while busy, on ESTOP, with the spindle on, and the refusal reason) are
   short functions beside the `root` properties.
4. **Split instead of growing the monolith.** `main.qml` is 3453 lines; new nodes arrive as separate QML
   components (`WorkScreen.qml`, `AutoScreen.qml`, `SoftkeyBar.qml`, `ToolMeasureDialog.qml`,
   `Theme.qml`), mirroring the React components. A guard mark holds each size, as it holds
   `machine.ts` (`tools/check.ps1`).
5. **Versions stay separate.** A panel edit raises `react-interface` and `qt-interface` independently
   (`VERSIONING.md`, section 2); the viewer and the test data are untouched.
6. **Verification is a run, not an eye.** The panel is captured through `preview.qml` (no 3D), the
   window through a live `gcodeviewer`; React and Qt frames are compared on key states (see §5).

## 3. Stages

Every stage ends with an acceptance check that fails on a drift. The estimates are rough, for the
owner's planning, not a promise.

### Stage 0. Manifest and guard (1 day)
* Work: emit `panel-data.js` from `lib/cnc`; extend `qt-parity.test.ts` to tabs, workplaces and softkey
  sets; add a guard step for manifest freshness.
* Artifact: `apps/qt-interface/panel-data.js`, the extended `qt-parity.test.ts`, a step in
  `tools/check.ps1`.
* Acceptance: editing a label in React without rebuilding the manifest fails; the Qt softkey count is 8
  on every Work screen.
* Risk: a manifest format awkward for QML. Mitigation: emit a JavaScript object (QML imports it), not JSON.

### Stage 1. Workplaces and modes (2-3 days)
* Work: a workplace state (`SETUP`/`TEST`/`RUN`), three buttons ordered AUTO, TEST, SETUP; a workplace
  sets the machine mode and the dry run; moving between workplaces keeps the current tab.
* Artifact: a `workplace` property, `setWorkplace`, header buttons, mirroring `nav.ts:63-72`.
* Acceptance: cycling the workplaces yields the expected mode and dry run; the tab is kept.
* Risk: Qt already has its own mode switch. Mitigation: it stays as a manual shift inside a workplace, as
  in React (`SET_MODE` lives next to `SET_WORKPLACE`).

### Stage 2. The Work tab and unlocking (1-2 days)
* Work: 6 tabs to 5; Position and Program merge into Work with the content chosen by the workplace;
  `tabLocked`, the strikeout, the reduced opacity and `enabled: !locked` are removed - every screen is
  reachable in every mode.
* Artifact: the tab bar, `panel-data.js`, the removed `tabLocked`.
* Acceptance: all 5 tabs open in every mode; none is struck out.
* Risk: the locks had reasons (offsets in AUTO are dangerous). Mitigation: the reason moves to a refusal
  of the individual action (writing an offset while busy), not to closing the screen - as React does now.

### Stage 3. Softkeys: 8 slots, refusal reasons (2-3 days)
* Work: `softkeysFor(screen)` becomes the workplace-and-screen set from the manifest; 8 slots instead of
  7; a disabled key carries a lock icon, a tooltip and a message line (the React `hint`).
* Artifact: `SoftkeyBar.qml`, reading the set from `panel-data.js`.
* Acceptance: the SETUP/TEST/AUTO sets match the `softkeys` reference; a disabled key names its reason.
* Risk: in Qt some keys carry behaviour (`machine: "spindle"`). Mitigation: each manifest key has an `id`,
  and the "id to action" table is kept by hand and checked by the set of ids.

### Stage 4. Screens by workplace (3-4 days)
* Work: the Work content by workplace: SETUP - coordinates and MDI, TEST - program review with the code
  curtain and Graphics clear (hides the viewer and gives the code the full width), AUTO - the screen of
  stage 5.
* Artifact: `WorkScreen.qml` with three branches.
* Acceptance: switching the workplace changes the content, the softkey set and the title.
* Risk: Graphics clear needs the viewer inside the panel. Mitigation: the viewer becomes a child element
  (stage 5, see the question in §6).

### Stage 5. The AUTO screen (3-4 days)
* Work: the Distance-to-Go strip (mm to the block and per axis, the remaining program estimate), one
  shared header, a transparent code window over the toolpath, three views in a cycle (trajectory / gcode /
  both), the line-type legend at the right edge.
* Artifact: `AutoScreen.qml`, the view in the state, mirroring `auto-screen.tsx`.
* Acceptance: F8 cycles the three views; the machining column never hides; the code window does not cover
  the header.
* Risk: transparency over 3D. Mitigation: the same trick as React - a code window without a fill or a
  header on top of the viewer; when `preview.qml` cannot show 3D, check by the rules and in a live window.

### Stage 6. Tool measure and the probe wizard (3-4 days)
* Work: the measure phases (rapid approach, fine approach, touch, retract), a modal dialog with the
  phase, the length and the delta, writing into the tool table; the probe wizard gets its rules (steps,
  T99, write, retry).
* Artifact: `ToolMeasureDialog.qml`, the probe rules beside `root`.
* Acceptance: the phases run through to the length being written; an emergency stop aborts the measure; a
  probe step requires T99.
* Risk: Qt has no reducer. Mitigation: the measure is a QML state machine property, like `probeStep` today.

### Stage 7. The Minimal theme (2-3 days)
* Work: the light and dark Minimal tokens from `globals.css` in `Theme.qml`; rounding from `--radius`
  (`0.375rem`), panel gap 0, 1 px dividers, no shadows, no gradients; a theme switch.
* Artifact: `Theme.qml` with the palettes, radius resolution instead of the `8/6/4` numbers.
* Acceptance: switching the theme changes the palette and keeps the geometry; no hardcoded `radius:`
  remains in the nodes.
* Risk: a QML palette cannot be derived from CSS automatically. Mitigation: the manifest (stage 0) also
  emits the tokens and the guard compares them with `globals.css`.

### Stage 8. Touch mode (2-3 days)
* Work: the touch layout - softkeys without the F1-F8 keycap, a deck (workplace, manual feed, overrides),
  the Start/Stop/Reset/Emergency row hidden on every tab but Work.
* Artifact: `TouchDeck.qml`, the mode flag in `Theme.qml`.
* Acceptance: touch mode mentions no F-keys; the Offsets tab has no cycle row.
* Risk: Qt may not need a touch mode at all (see the question in §6).

### Stage 9. Acceptance and guard (1-2 days)
* Work: `tools/check.ps1` runs `qt-parity.test.ts` and the manifest comparison; frames of the key states
  of React and Qt are stored side by side for the eye; `texts.test` (no `U+FFFD`) extends to QML.
* Artifact: the guard steps, the frame directory.
* Acceptance: the guard fails on a drifted label, softkey set or theme token.
* Risk: `preview.qml` without 3D cannot show the toolpath. Mitigation: check the panel in `preview.qml`
  and the AUTO screen with the toolpath in a live viewer window.

## 4. Order and estimate

Stages 0-3 come first: after them the sets and tabs are visibly checked and the rest is built on solid
ground. Then 4-6 (screens and wizards), then 7-8 (look), then 9 (guard). The rough total is **20-30 days**
for one person; stages 0-2 are already a visible part worth starting for.

## 5. General risks

* **The monolith.** `main.qml` at 3453 lines must be cut along the stages, or the edits stop fitting in
  a head; the per-file size guard works as the `machine.ts` mark does.
* **The Qt runtime.** The PySide6 process carries its own Qt 6.12; a build against another minor version
  links by name to that runtime. For the panel this means checking in the same runtime it is launched in
  (`docs/PORT-CPP.md`, §11, runtime limits).
* **Three sources of texts.** While React, QML and the manifest all exist, any two may drift. The
  manifest (stage 0) is the only machine check; without it stages 1-8 start drifting by hand again.
* **Live 3D only in a window.** Offscreen does not draw Quick3D; part of the AUTO-screen checks
  unavoidably needs a live window.

## 6. Open questions for the owner

1. **Are workplaces the top level of the Qt panel?** React says yes; Qt knows only the machine mode
   today. If the Qt panel is driven differently, stages 1, 4 and 5 change.
2. **The transparent code window over the toolpath** - does the viewer become a child of the panel in
   Qt, or do the panel and the viewer stay separate windows?
3. **How many themes in Qt?** Five, as in React, or Minimal alone?
4. **Is a touch mode needed in Qt?** If the panel also sits on a touch screen, stage 8 is mandatory.
5. **Who owns the manifest?** Stage 0 is infrastructure across two projects; without a decision on the
   owner it will hang between `react-interface` and `qt-interface`.

## 7. What this plan does not do

The viewer port (a separate plan, `docs/PORT-CPP.md`), edits to the React interface itself, and the test
programs.
