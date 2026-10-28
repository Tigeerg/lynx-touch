# ROADMAP.md — from the current panel to alpha and the physical bench

This is a plan, not a report of work done. It answers three questions: what counts as alpha, in what order we
move, and what proves a stage is finished. The source of truth for behavior is the code and its run; this file
only holds intent, boundaries and exit criteria.

Russian original — [ROADMAP.md](ROADMAP.md); both are one source and are edited together.

## 1. Starting decisions

| Question | Decision |
|---|---|
| Qt port in alpha | No. Alpha is the React panel and the viewer. Qt comes after alpha as a separate stage |
| Viewer in alpha | Toolpath, stock, stock removal and a **simple** collision check. No new mathematical methods and no CSG (building a volume from simple solids) |
| Alpha acceptance | Desk with a real program from `cad-testdata`; a real machine is not required |
| Video audience | Operator and technologist. We show a working scenario, not a set of screens |
| Three panel configurations | Required in the audit and the demo: physical keys, hybrid, touch-only |
| Quick programming | After alpha: first alpha proves usability, then we add operation generators |
| Alpha version | Proposed `0.30.0-alpha.1` for the demo alpha and `0.30.0-alpha.2` for the functional alpha. Pre-release rules are fixed in `VERSIONING.md` at M0 |

Effort classes, not dates: `S` — one pass, `M` — several passes, `L` — a separate work branch. Date ranges
appear after M0, when the Architect estimates dependencies.

## 2. Baseline on 2026-10-26

| Project | Version | What exists | What blocks alpha |
|---|---|---|---|
| product | `0.22.0` | four projects, roles, skills, plans | no pre-release rule, no demo script |
| `react-interface` | `0.16.0` | panel and touch variants, 8 softkeys, workplaces, probing, themes | three hardware configurations not tested as separate scenarios; open debt in `TODO.md` §7-9 |
| `qt-interface` | `0.5.8` | QML port and `preview.qml` | nine stages behind `docs/PORT-QML.md`; not in alpha |
| `gcode-viewer` | `0.58.6` | toolpath, part, stock, stock removal, holder-vs-stock collision | open stock removal defects, field cost, some checks need a live window |
| `cad-testdata` | `0.5.1` | `Test_model.ngc`, `Test_model.STL`, post with `UI_*` | need a demo set and stated limits |

Facts that must not be lost:

* the viewer already has toolpath, stock removal and holder-vs-stock collision; the simple collision is
  **finishing the existing feature**, not new mathematics;
* stock field on `Test_model.ngc`: cell 0.5 mm — 192-425 ms, cell 0.2 mm — 2.3-8.4 s; exact removal on the
  `alt` branch — 118-791 ms. The demo uses a measured preset, not "looks fine";
* `pnpm guard` has known red steps due to the environment (`TODO.md` §8); M1 fixes them or names them, but
  never hides them;
* there is no real controller link: the panel state machine is local. Alpha does not hide this;
* the three panel configurations differ in input and screen, not in three state machines. Behavior has one
  source; presentation and access change.

## 3. What alpha means

### 3.1 Two alphas

**Alpha 1 — demo for usability evaluation.** After the first stage: current product, AI-model audit, obvious
fixes, three configurations, real program, simple collision. The goal is to show the operator and technologist
what already works and collect usability findings.

**Alpha 2 — functional alpha for an external video.** After probing, zero finding and viewer work: the same
scenario, but with working tool measurement, stock dimensions and part zero wizards, plus a clear collision
result. This is the version that can be shown wider.

Both alphas are **demo** versions, not production. They do not replace machine acceptance and do not promise
real-machine safety.

### 3.2 What "video-ready" means

* one demo scenario passes **three times in a row** on a clean machine;
* no blockers, dead keys or silent refusals on the demo path;
* a real program, not an invented file;
* three configurations checked and their differences named;
* the build is reproducible from README;
* screenshots, a check log, known limitations and a short video script exist;
* version, changelog and `projects.json` agree.

### 3.3 Outside alpha

* Qt port, viewer inside Qt, transparent code window over 3D in Qt;
* real machine, SMDTAXI, drives, scales, physical keyboard;
* quick programming (pockets, faces, ends);
* new stock removal methods, CSG, undercuts, 4-5 axes;
* safety certification and production acceptance.

## 4. Three panel configurations

| id | Screen | Input | What we check |
|---|---|---|---|
| **P1. Panel** | 10 inch, 1024x600 | physical `F1`..`F8`, MPG, emergency stop; no touch | every needed function is reachable without touch; no "tap the screen" hints; focus and keys do not get lost |
| **P2. Hybrid** | 10 inch, 1024x600, touch | physical keys + touch; touch rotates the 3D view | key and touch do the same thing; no duplicates; touch targets are finger-sized; view rotation does not disturb work |
| **P3. Touch** | larger than 10 inch, resolution TBD | touch only | no dependency on `F1`..`F8`; every quick function is available on screen; large targets and clear gestures |

P3 is not "the same screen, larger". Resolution and scale are decided at M0: either a separate layout or a
scalable grid. This is an open question, not a guess.

## 5. Stages

### M0. Alpha contract and demo script — S

**Who:** Product Manager leads, Architect checks feasibility, CNC Technologist checks meaning, dispatcher
approves.

**Work:**

* approve Alpha 1 and Alpha 2 boundaries from this roadmap;
* fix P1/P2/P3: P3 resolution, MPG presence, physical key set, touch behavior;
* write a 7-10 step demo script on a real program;
* fix the pre-release rule in `VERSIONING.md`;
* agree who provides operators and technologists for the demo;
* name the sensor and zero-finding methods taken into M3 and those deferred.

**Exit:** approved sections 3-4, demo script, acceptance matrix from section 12, open questions without guesses.

### M1. AI-model audit and obvious fixes — M

This is the first work stage. It adds no features: it checks what exists and removes obvious defects.

**Who:** Tester leads runs, Reviewer leads code and standards, UI specialist and CNC Technologist lead usability
and meaning, Coder fixes, Integrator covers viewer and build, Architect guards the boundaries.

**Audit passes:**

1. **Functional smoke.** Start React panel and viewer, load a real program, modes, screens, softkeys,
   emergency stop, reset, messages. No silent button.
2. **Three configurations.** The same scenario on P1, P2 and P3. Check quick-function reachability,
   duplicates, extra steps, small targets, dependency on `F` keys.
3. **Information architecture.** Where the operator finds program, offsets, tool, probing, zero finding,
   messages, service. What is buried too deep.
4. **Duplicate functions.** One operation must not have two names or two paths with different behavior.
   A duplicate is either merged into one source or explained in the report.
5. **Code and standards.** Reviewer looks for obvious blockers: crashes, wrong numbers, empty `catch`,
   hardcode, dead branches, deviations from `CLAUDE.md`.
6. **Viewer.** Real program, toolpath, stock, removal, collision, performance, `[QML warning]`.
7. **Probing and zero finding.** What works, what does not, where the operator does not understand the next
   step. This feeds M3.
8. **Documents.** README, `ARCHITECTURE.md`, `TODO.md`, `CHANGELOG.md` do not lie about current behavior.

**How AI models participate.** Several independent sessions or models get the same build and different roles:
Tester, Reviewer, UI specialist, CNC Technologist, Integrator. No model is the truth: every finding is verified
by a run, a screenshot or code, and a domain finding by the CNC Technologist. The model proposes a hypothesis;
the human and Tester confirm it. Findings go into one register, not "a hundred reports".

**What counts as an obvious defect:**

* the panel crashes, freezes or shows a wrong number;
* a button does nothing and does not explain why;
* one function has two names or two places with different behavior;
* a function the operator needs is unreachable on P1 or P3;
* a message lies about what happened;
* text does not fit its slot, overlaps a neighbor or is unreadable;
* a check is red and its cause is not named;
* a document promises behavior that does not exist.

**Not in M1:** no new screens, generators, themes or features; no Qt; no architecture change without a blocker;
no "improvements" that do not affect the demo.

**M1 exit:**

* a findings register with severity, address, effect and decision: fix, defer, reject;
* all demo-path blockers fixed;
* duplicates merged or explained;
* quick functions reachable on P1/P2/P3;
* the viewer loads a real program and the simple collision shows a result;
* `typecheck`, `test`, `format:check`, `build` green; `guard` green or `ENV` with a reason;
* demo script version 1 passes three times;
* `TODO.md` holds deferred work, not forgotten work.

### M2. Demo to operator and technologist — M

**Who:** Product Manager leads, UI specialist prepares screens and materials, CNC Technologist checks the
scenario, Tester records evidence, dispatcher organizes participants.

**Work:**

* 3-6 participants: operators and technologists; where possible each sees their own configuration, not only P1;
* the same tasks in every configuration: load a program, find a quick function, open the right screen, view the
  toolpath, check collision, find an offset, open probing, run ESTOP and RESET;
* observe without hints: where they look, how many steps they take, where they err, what they call extra;
* a short post-task interview: what is convenient, what blocks, what is missing, what is duplicated;
* video and screenshots are evidence, not a replacement for the report.

**Metrics:** task success, error count, extra steps, time to the needed function, clarity of refusal, subjective
rating. Numbers without a method are not published.

**M2 exit:**

* usability report with top problems and participant quotes;
* decisions on duplicates and information architecture;
* prioritized fix list for Alpha 1;
* confirmation that the three configurations differ in input, not in logic;
* Alpha 1 built and shown; video or demo recording attached.

### M3. Tool measurement, stock dimensions, zero finding — L

**Who:** CNC Technologist leads the domain matrix, UI specialist designs the wizard and graphics, Coder writes
the state machine and offset write, Tester checks phases, Reviewer checks safety and standards.

**Matrix first, then code.** For every sensor and method describe: what is measured, with what, how many touches,
which feed, what counts as success, what happens on a miss, where the result is written, what the operator sees.

Minimum set for Alpha 2:

* **Tool:** length measurement on a setter, write to the tool table, repeat, cancel, emergency stop;
* **Stock dimensions:** measure along two or three axes, write to stock, plausibility check;
* **Part zero:** edge, corner, center, surface; method and sensor selection; write to work offset;
* **Graphics:** what is measured, where the probe goes, where the touch is, what the result and error are.

We do not take every sensor and method at once. M0 fixes the two or three mandatory methods for alpha; the rest
come later.

**M3 exit:** a wizard runs from start to result write; the operator understands the next step without reading
code; emergency stop interrupts; a wrong sensor or feed does not write a false value; checks and docs updated.

### M4. Viewer and simple collision — M

**Who:** Architect sets the boundary, Integrator and Coder own the viewer, Tester runs probes, Reviewer checks
numbers and threads.

**Work:**

* finish the demo path: real program -> toolpath -> stock -> stock removal -> collision;
* simple collision is the existing holder-vs-stock check: clear result, dangerous sections highlighted, a
  separate "no collision" case;
* never report "clear" when the check did not run; the refusal reason is visible;
* measure performance on the demo program and record it; choose the quality preset for the video;
* known stock removal defects (hairline, fan, CSG, color edge side) are not fixed in alpha unless visible in the
  demo scenario; each deferred item goes to `TODO.md`.

**M4 exit:** the demo program loads, collision shows safe and unsafe cases, frames and measurements attached,
`loaded OK` and no `[QML warning]` on the demo path.

**Alpha 2 is assembled after M4.**

### M5. Quick programming in the control — L

**After alpha.** Simple operations: pocket, face, end; drilling and chamfer by agreement.
The detailed plan, input forms and estimate — [`PLAN-QUICKPROG.en.md`](PLAN-QUICKPROG.en.md).

* parameter form and preview on stock;
* program generator, collision and stock-bound checks;
* write to file, name, confirmation;
* generator tests and acceptance on a real program;
* dangerous combinations blocked with a reason.

**Exit:** three operations produce valid G-code, preview matches the result, the operator can cancel before
write, invalid input creates no file.

### M6. Qt port and viewer integration — L

**After alpha.** Work follows `docs/PORT-QML.md`, stages 0-9: manifest, workplaces, Work tab, 8 softkeys,
screens, AUTO, probing, theme, touch, acceptance. Then the viewer as a child element of the Qt panel.

**Exit:** the Qt panel repeats the alpha scenario verbatim; `qt-parity.test.ts` and the manifest hold texts and
sets; 3D works in a live window; differences from React are named.

### M7. Setup through the operator interface — L

This is a separate workstream, not a side edit of the Service screen.

* INI/HAL parameter catalog through `ParamDef`: read, range check, write, password;
* dangerous parameters require confirmation and a clear reason;
* backup, before/after diff, rollback;
* LinuxCNC setup without hand-editing files for typical tasks;
* change log and author;
* acceptance with the CNC Technologist and on the real configuration.

**Exit:** a typical machine setup is done through the interface; an invalid value is rejected before write;
rollback restores a working state.

### M8. Physical keyboard, HMI and bench — L

**M8a. Keyboard and HAL.**

* HMI device: key matrix, `F1`..`F8`, MPG, emergency stop, backlight;
* firmware or USB HID, key map, debounce, link loss;
* HAL pins: every key produces the expected signal; emergency stop is a hardware circuit, not software only;
* P1 check: physical keyboard without touch.

**M8b. Bench, SMDTAXI, drives and scales.**

* bench assembly, SMDTAXI, drives, scales, safety;
* LinuxCNC configuration, axis check, homing, alarms;
* full run: program, pause, reset, probing, zero finding, collision;
* acceptance on the bench, not on the desk;
* limitations and recovery instructions.

**Exit:** the bench powers up, axes move, scales read, keyboard and MPG work through HAL, emergency stop stops,
the full scenario passes on hardware.

## 6. Alpha acceptance matrix

The matrix describes Alpha 2. In Alpha 1 the probing, stock-dimension and zero rows are checked only to the
extent of current behavior and marked as work in progress, not presented as a finished wizard.

| Check | P1 panel | P2 hybrid | P3 touch |
|---|---|---|---|
| Start and workplace switch | keys | keys and touch | touch |
| Load real program | keys | both | touch |
| Toolpath and stock | no touch | no touch + rotate | touch, large targets |
| Simple collision | result and reason | result and reason | result and reason |
| Offsets and tool | keys | both | touch |
| Tool measurement | wizard | wizard | wizard |
| Stock dimensions and part zero | wizard | wizard | wizard |
| Messages and refusal reason | readable | readable | readable |
| ESTOP and RESET | physical key | physical and screen | screen |
| Quick functions | all reachable | no duplicates | all reachable |

Row criterion: the task is done without a hint, without a dead button and without a false message. An exception
is allowed only with a report entry and a target date.

## 7. Roles and gates by stage

| Stage | Lead | Participants | Gate |
|---|---|---|---|
| M0 | Product Manager | Architect, CNC Technologist, dispatcher | G1: contract and demo script approved |
| M1 | Tester and Reviewer | UI, CNC Technologist, Coder, Integrator, Architect | G4-G5: audit, fixes, green checks |
| M2 | Product Manager | UI, CNC Technologist, Tester, dispatcher | G6: usability report and Alpha 1 accepted |
| M3 | CNC Technologist and UI | Coder, Tester, Reviewer | G4-G6: wizards and domain check |
| M4 | Architect | Integrator, Coder, Tester, Reviewer | G4-G6: viewer and Alpha 2 |
| M5 | Architect | Coder, UI, Tester, Reviewer | G4-G6: generator and preview |
| M6 | Architect | UI, Coder, Tester, Reviewer | G4-G6: Qt parity |
| M7 | Architect | Coder, Integrator, Tester, Reviewer, CNC Technologist | G4-G6: setup without manual files |
| M8 | Integrator | Coder, CNC Technologist, Tester, Reviewer, dispatcher | G4-G6: bench and full run |

In chat these roles are grouped into four profiles from `AI_WORKFLOW.md`, section 9: `lead` (Product Manager +
Architect), `builder` (Coder + Integrator), `product-ux` (UI + CNC Technologist), `verifier` (Tester +
Reviewer). The dispatcher talks only to `lead`.

## 8. Risks and how we hold them

| Risk | What we do |
|---|---|
| Qt slips into alpha | boundary in section 3; any Qt change is a separate pass after Alpha 2 |
| Stock removal defects eat the demo | fix only what is visible on the demo path; the rest goes to `TODO.md` |
| Red `guard` due to environment | M1: fix the environment or name `ENV` with a reason; never hide |
| An AI model presents a guess as a defect | every finding is verified by a run, a screenshot or the CNC Technologist |
| No real machine | desk acceptance with a real program; limits stated; machine after alpha |
| P3 needs another layout | decision at M0; do not stretch 1024x600 onto a large screen by eye |
| Sensors and zero-finding methods sprawl | M3 matrix and two or three mandatory methods; the rest later |
| Quick programming drags the postprocessor | separate stage M5, not before Alpha 2 |
| Deadlines turn into promises | no dates in this roadmap; after M0, ranges with caveats |

## 9. Artifacts and release

For Alpha 1 and Alpha 2:

* tag and version, `CHANGELOG.md`, `projects.json`, `VERSION`;
* `docs/DEMO.md` — video script and shot order;
* `docs/ALPHA-ACCEPTANCE.md` — matrix, runs, screenshots, known limitations;
* M1 findings register with decisions;
* README: clean-machine quick start, requirements, known limitations;
* archive `ZIP/CNC-operator-panel-v<version>` when needed for handover;
* video or demo recording with captions.

## 10. Open questions

1. P3 resolution and physical size; scalable grid or a separate layout.
2. Exact physical key set for P1/P2: which operations must be on the keyboard.
3. Sensor and zero-finding method list for M3: mandatory versus after alpha.
4. Demo program and part: is `Test_model.ngc` enough or do we need a dedicated showcase.
5. Video format: length, language, whether all three configurations are shown in one clip.
6. Who provides operators and technologists for M2 and how many participants are needed.
7. Exact quick-programming scope for M5: pocket, face, end, drilling, chamfer.
8. Order of M7 and M8: setup UI before the bench or in parallel.

## 11. First pass

1. Approve this roadmap and answer open questions 1, 3, 4, 6.
2. Write the demo script and acceptance matrix.
3. Build the current version on a clean machine, take baseline P1/P2/P3 screenshots.
4. Run the M1 audit by roles and merge findings into one register.
5. Fix only blockers and obvious defects; write deferred work to `TODO.md`.
6. Show Alpha 1 to an operator and technologist and collect the M2 report.

No new features start before M1 exits. The exception is a fix without which the demo path fails, and it must be
recorded in the findings register.
