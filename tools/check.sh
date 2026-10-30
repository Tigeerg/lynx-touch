#!/bin/sh
# tools/check.sh - style and structure guard for the project under Linux (rules - CLAUDE.md).
#
# Run:  bash tools/check.sh
#
# Runs the shared check list tools/checks.txt; the same list drives the
# list in tools/check.ps1 (pnpm guard under Windows), so adding a check is
# one manifest line. The text rules of CLAUDE.md, file size labels and
# pnpm/React runs stay in tools/check.ps1: they need Windows or node; on
# Linux they are printed as skip.
#
# There is no Python layer: all checks are C++ (probes and the viewer entry point).
#
# A check whose tool is missing (no built probes, the MockBackend QML module
# or viewer entry point) is printed as skip: on a machine without the toolchain
# the guard stays green. A red check prints its output in full.
#
# Return code: 0 - all executed checks are green; 1 - there is a red one.

set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

n_ok=0
n_fail=0
n_skip=0

print_ok() { printf 'ok    %s\n' "$1"; n_ok=$((n_ok + 1)); }
print_bad() { printf 'FAIL  %s\n' "$1"; n_fail=$((n_fail + 1)); }
print_skip() { printf 'skip  %s\n' "$1"; n_skip=$((n_skip + 1)); }

# Built artefacts: probes in build/edges, controller module in build/qmlbackend,
# viewer entry point in build/viewer. Build - bash scripts/build-viewer.sh.
have_probe=0
if [ -x build/edges/gcode_probe ]; then
    have_probe=1
fi
have_plugin=0
if [ -f build/qmlbackend/MockBackend/qmldir ] && [ -x build/viewer/gcodeviewer ]; then
    have_plugin=1
fi
# The quick programming demo: the self-check build/quickprog/quickprog_check.
# Build - bash scripts/build-quickprog.sh.
have_quickprog=0
if [ -x build/quickprog/quickprog_check ]; then
    have_quickprog=1
fi

echo "check: пробники = $have_probe, плагин MockBackend и вьювер = $have_plugin, quickprog = $have_quickprog"

# Screenless mode for the whole run: viewer runs and the frame capture inherit it.
# The offscreen frame is captured, but only a live platform renders the Quick3D scene.
export QT_QPA_PLATFORM=offscreen

while IFS='|' read -r label need cmdline; do
    case "$label" in
        ''|'#'*) continue ;;
    esac
    case "$need" in
        probe|plugin|frame|quickprog) ;;
        *)
            print_bad "$label: неизвестное требование '$need' в tools/checks.txt"
            continue
            ;;
    esac
    if [ "$need" = 'quickprog' ] && [ "$have_quickprog" != '1' ]; then
        print_skip "$label (нет собранной проверки quickprog: bash scripts/build-quickprog.sh)"
        continue
    fi
    if [ "$need" = 'probe' ] && [ "$have_probe" != '1' ]; then
        print_skip "$label (нет собранных пробников: bash scripts/build-viewer.sh)"
        continue
    fi
    if { [ "$need" = 'plugin' ] || [ "$need" = 'frame' ]; } && [ "$have_plugin" != '1' ]; then
        print_skip "$label (нет собранного плагина MockBackend или вьювера: bash scripts/build-viewer.sh)"
        continue
    fi
    log=$(sh -c "$cmdline" 2>&1)
    code=$?
    # The return code is not enough: a crash inside the run does not change it - viewer
    # runs print "loaded OK" even on failure. Such failures must be visible, so a
    # Traceback or a WARNING/ERROR/ line in the output paints the check red.
    if [ "$code" -ne 0 ] || printf '%s\n' "$log" | grep -qE 'Traceback|^(WARNING|ERROR) '; then
        print_bad "$label (код возврата $code)"
        printf '%s\n' "$log" | sed 's/^/      /'
    else
        print_ok "$label"
    fi
done < tools/checks.txt

# Steps outside the manifest: they need Windows or node (see tools/check.ps1).
print_skip 'pnpm: typecheck, test, format:check (только Windows/сторож pnpm guard)'
print_skip 'tools/check.ps1: текстовые правила CLAUDE.md, метки размеров, сборка C++ под MSVC'

printf '\ncheck: ok %d, FAIL %d, skip %d\n' "$n_ok" "$n_fail" "$n_skip"
[ "$n_fail" -eq 0 ] || exit 1
exit 0
