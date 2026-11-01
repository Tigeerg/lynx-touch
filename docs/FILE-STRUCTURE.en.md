# FILE-STRUCTURE.md — project layout, artifacts and temporary files

Where things live, what is a source file, what is a build artifact and what is temporary junk. The
rules apply to every agent and person; they are checked by the guard (`tools/check.ps1`) and the
cleanup script `scripts/clean.ps1`.

## 1. What the audit found (01.11.2026)

| Place | Size | What it is | Decision |
|---|---:|---|---|
| `build/` | ~0.62 GB, 4978 files | built Qt/C++ programs, probes, Qt/OCCT DLLs | keep the project subdirectories, remove the junk in the root |
| `apps/react-interface/.next` | ~320 MB | Next.js cache | not stored; cleaned |
| `build/cpp` | 2 DLLs | old `gvbridge`/`gvqtshim` bridge | delete as obsolete |
| root of `build/` | ~30 files | our screenshots, one-off `.mjs`/`.ps1`, logs, test `.ngc` | delete; rule below |
| `img/` | 7 tracked PNGs | accepted screenshots for README/acceptance | keep; check references |
| `ZIP/` | 10 MB | local archive, ignored | not a source of truth, never committed |
| tracked `build/` | 0 files | — | keep it that way |

The junk is not the built programs; it is **one-off files dropped in the root of `build/`**. Their
place is `build/artifacts/` (temporary) or `img/` (accepted), never beside the exe.

## 2. Project tree

```
apps/
  react-interface/      panel sources (Next.js, TypeScript), testdata/
  qt-interface/         Qt port sources (QML, C++), tests/, panel-data.js
  quickprog/            C++ CAM demo (sources, tests/)
modules/
  gcode-viewer/         3D viewer: QML, cpp/, testdata, oracle_manifest.json
data/
  cad-testdata/         test NC programs/models, postprocessor
docs/                   plans, architecture, this document
img/                    ONLY accepted screenshots (tracked)
scripts/                build/run/cleanup scripts, one purpose each
tools/                  guard and shared checks
build/                  generated, ignored (see §3)
```

The root holds project documents (`README`, `CLAUDE`, `AGENTS`, `VERSION`, `projects.json`, ...)
and the licence. Sources live in `apps/`, `modules/`, `data/`, `docs/`, `scripts/`, `tools/`,
`img/`.

## 3. `build/`: generated, not source

`build/` is fully ignored; **no file from it is committed**. Inside, one directory per project:

| Directory | Contents | Created by |
|---|---|---|
| `build/edges` | gvcore, STEP/STL, probes, CNC.Edges | `scripts/build-viewer.ps1` |
| `build/viewer` | `gcodeviewer.exe`, `keyprobe`, `stock_bind_probe`, Qt/OCCT | `scripts/build-viewer.ps1` |
| `build/qmlbackend` | MockBackend | `scripts/build-viewer.ps1` |
| `build/quickprog` | `quickprog.exe`, `quickprog_check.exe` | `scripts/build-quickprog.ps1` |
| `build/qt-interface` | `lynx-panel.exe`, Qt/OCCT | `scripts/build-qt-interface.ps1` |
| `build/artifacts` | temporary screenshots, logs, trial files | agent/person |
| `build/tmp` | one-off scripts and dumps | agent/person |
| `build/logs` | run logs | agent/person |

Rules:

1. **The root of `build/` holds only directories.** No `*.png`, `*.qml`, `*.mjs`, `*.ps1`,
   `*.log`, `*.txt`, `qp-*.ngc` directly in `build/`.
2. **One-off files go to `build/tmp/`, logs to `build/logs/`, screenshots to
   `build/artifacts/screenshots/`.** All of it is disposable.
3. **Full cleanup** — `scripts/clean.ps1 -Build` (removes `build/` entirely; rebuild after).
4. **Partial cleanup** — `scripts/clean.ps1 -Artifacts` (root of `build/`, `build/artifacts`,
   `build/tmp`, `build/logs`, old `build/cpp`).
5. **Next.js cache** (`apps/react-interface/.next`) is not stored; cleaned by `-Next`.
6. **Runtime folders (`build/<project>/`) are generated.** Qt/OCCT next to the exe are placed by
   the build; SDKs are never copied by hand. The folder is cleared before deployment: an old full
   OCCT set (1 GB) otherwise stays next to the new compact one and doubles `build/`.

## 4. Screenshots and evidence

* **Accepted — only `img/`** (tracked). One file, one accepted state.
* **Working/draft — only `build/artifacts/screenshots/`** (ignored), removed by cleanup.
* Name: `<area>_<what>_<date>[_<state>].png`, Latin, e.g. `viewer_solid.png`,
  `panel_softkeys_fit.png`, `qt_work_2026-11-01.png`.
* `img/` does not take intermediate "just in case" frames, duplicates or frames with foreign UI.
* A screenshot must be referenced by documentation; an unreferenced file is a removal candidate.
* Before a release `img/` is reviewed: stale frames are removed together with their links.

## 5. Temporary scripts and dumps

* **Reusable script** -> `scripts/` (Windows `.ps1`, Linux `.sh` when needed), with a header:
  purpose, usage, exit code. Only then it is committed.
* **One-off** (test a hypothesis, capture a frame, compare files) -> `build/tmp/` or the system
  `%TEMP%\opencode\`; never committed.
* **Test dumps** (`.ngc`, `.qprog`, `.png`, logs) -> `build/artifacts/`, not `testdata/`.
* `testdata/` holds only references used by tests; temporary files are forbidden there.

## 6. Archives and third-party files

* `ZIP/` is a local archive store, ignored; not a source of truth and not an editing place.
* Release archives live in `dist/` (ignored), not in the root or `build/`.
* Downloaded SDKs (Qt, OCCT) stay outside the repository (`C:\Qt`, `D:\cnc-toolchain`); the project
  keeps only `CNC_QT_DIR`, `CNC_OCC_DIR`.
* Binaries (`*.exe`, `*.dll`, `*.pdb`, `*.lib`) are not stored in the repository (except test data,
  if ever added deliberately and recorded in `TODO.md`).

## 7. Cleanup

```powershell
# show what takes space and what would be removed (touches nothing)
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/clean.ps1

# remove one-off files and old directories, keep the builds
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/clean.ps1 -Artifacts

# remove build/ entirely (rebuild after) and the Next.js cache
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/clean.ps1 -Build -Next
```

Cleanup **never** removes `apps/`, `modules/`, `docs/`, `img/`, `data/`, `scripts/`, `tools/` or
the root documents. A path outside the `build/`/`.next` allow-list is refused.

## 8. Guard checks

`tools/check.ps1` additionally verifies:

1. git tracks no files from `build/` or `.next/`;
2. git tracks no `*.exe`, `*.dll`, `*.pdb`, `*.lib`, `*.obj`;
3. the root of `build/` contains no files, only directories;
4. the repository root contains no `*.png` (screenshots live in `img/` or `build/artifacts/`).

A violation is a `FAIL` naming the path, not a warning: otherwise the junk returns.

## 9. When to clean

* after every accepted wave — `-Artifacts`;
* before a release — `-Artifacts -Next` and a review of `img/`;
* after an SDK/build change — `-Build` and a full rebuild;
* before a commit — look at `git status --ignored`: nothing from `build/` may enter the commit.
