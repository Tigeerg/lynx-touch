#!/bin/sh
# scripts/build-viewer.sh - build of the NC viewer in C++ on Linux: the QML plugin CNC.Edges,
# the gvcore core, the gvqtgeom geometries, the gvstep STEP layer, the QML module MockBackend and the entry
# point build/viewer/gcodeviewer.
#
# Usage:  bash scripts/build-viewer.sh
#          CNC_QT_DIR=<Qt6 directory> CNC_OCC_DIR=<OCCT directory> bash scripts/build-viewer.sh
#
# The SDK and compiler environment on Linux is assembled by environment.yml in the root
# (conda-forge): conda env create -f environment.yml. The compiler in the environment is
# not a luxury: the conda build of OCCT links against the symbol __cxa_call_terminate@CXXABI_1.3.15,
# which GCC 11 of a stock Ubuntu 22.04 does not have.
#
# The script does the same as scripts/build-viewer.ps1 (the breakdown of targets and artifacts is there),
# minus Windows: the MSVC environment is not captured, vcvars64.bat is not needed, the compiler
# and the linker are the system ones. SDK paths are taken from the environment: CNC_QT_DIR (Qt6 with Quick3D),
# CNC_OCC_DIR (OCCT with a CMake package), CNC_CMAKE, CNC_NINJA. If not set, $CONDA_PREFIX is taken
# in the conda-forge layout (lib/cmake/Qt6 and lib/cmake/opencascade), and
# if they are not there either, the variables are not set at all, and find_package looks for the paths itself.
# The build directory is CNC_BUILD_DIR, by default <repo>/build/edges.
#
# The dynamic linker finds Qt and OCCT by the build rpath (CMake fills it itself, BUILD_RPATH),
# so LD_LIBRARY_PATH is not needed, and the entry point (cpp/viewer/main.cpp) on Linux does not touch
# PATH.
#
# Built: <build>/CNC/Edges/ (qmldir, libcncedgesplugin.so,
# libcncedgespluginplugin.so),
# <repo>/build/qmlbackend/MockBackend/ (libgvqmlbackend.so, libgvqmlbackendplugin.so),
# <repo>/build/viewer/gcodeviewer.
#
# Exit code: 0 - built, the artifacts are in place; 1 - tool not found or build
# error.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$script_dir/.." && pwd)
src_dir="$root/modules/gcode-viewer/cpp"
build_dir=${CNC_BUILD_DIR:-$root/build/edges}

fail() {
    echo "build-viewer: FAIL $1" >&2
    exit 1
}

# Qt6 directory: lib/cmake/Qt6/Qt6Config.cmake is expected inside.
is_qt_dir() {
    [ -n "$1" ] && [ -f "$1/lib/cmake/Qt6/Qt6Config.cmake" ]
}

# OCCT directory: lib/cmake/opencascade/OpenCASCADEConfig.cmake is expected inside.
is_occ_dir() {
    [ -n "$1" ] && [ -f "$1/lib/cmake/opencascade/OpenCASCADEConfig.cmake" ]
}

# Tool: the value of the environment variable, otherwise the first command found in
# PATH, otherwise - from the bin directories of Qt/OCCT/the environment: the conda environment keeps cmake
# and ninja in its bin, and without activating the environment they are not in PATH.
find_tool() {
    override=$1
    shift
    if [ -n "$override" ]; then
        [ -x "$override" ] && printf '%s' "$override"
        return 0
    fi
    for name in "$@"; do
        hit=$(command -v "$name" 2>/dev/null || true)
        if [ -n "$hit" ]; then
            printf '%s' "$hit"
            return 0
        fi
    done
    for dir in "${qt_dir:+$qt_dir/bin}" "${occ_dir:+$occ_dir/bin}" "${CONDA_PREFIX:+$CONDA_PREFIX/bin}"; do
        [ -n "$dir" ] || continue
        for name in "$@"; do
            if [ -x "$dir/$name" ]; then
                printf '%s' "$dir/$name"
                return 0
            fi
        done
    done
    return 0
}

qt_dir=${CNC_QT_DIR:-}
occ_dir=${CNC_OCC_DIR:-}
if [ -z "$qt_dir" ] && is_qt_dir "${CONDA_PREFIX:-}"; then
    qt_dir=$CONDA_PREFIX
elif [ -n "$qt_dir" ] && ! is_qt_dir "$qt_dir"; then
    fail "не найдено $qt_dir/lib/cmake/Qt6/Qt6Config.cmake - проверьте CNC_QT_DIR"
fi
if [ -z "$occ_dir" ] && is_occ_dir "${CONDA_PREFIX:-}"; then
    occ_dir=$CONDA_PREFIX
elif [ -n "$occ_dir" ] && ! is_occ_dir "$occ_dir"; then
    fail "не найдено $occ_dir/lib/cmake/opencascade/OpenCASCADEConfig.cmake - проверьте CNC_OCC_DIR"
fi

cmake=$(find_tool "${CNC_CMAKE:-}" cmake)
[ -n "$cmake" ] || fail 'cmake не найден: задайте CNC_CMAKE=<путь к cmake> или активируйте окружение (conda activate gcode-cpp)'
ninja=$(find_tool "${CNC_NINJA:-}" ninja ninja-build)
[ -n "$ninja" ] || fail 'ninja не найден: задайте CNC_NINJA=<путь к ninja> или активируйте окружение (conda activate gcode-cpp)'

# CMAKE_PREFIX_PATH: the found directories separated by a semicolon. If nothing is set,
# the key is not passed at all: find_package looks for the paths.
prefix=''
for dir in "$qt_dir" "$occ_dir"; do
    if [ -n "$dir" ]; then
        if [ -n "$prefix" ]; then prefix="$prefix;$dir"; else prefix="$dir"; fi
    fi
done

echo "build-viewer: Qt  = ${qt_dir:-не задан: каталог ищет find_package}"
echo "build-viewer: OCCT= ${occ_dir:-не задан: каталог ищет find_package}"
echo "build-viewer: cmake=$cmake"
echo "build-viewer: ninja=$ninja"
echo "build-viewer: build=$build_dir"

# The configure parameters are collected into positional ones: a path with a space reaches
# cmake as a single word.
set -- -S "$src_dir" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
       -DCMAKE_MAKE_PROGRAM="$ninja"
if [ -n "$prefix" ]; then
    set -- "$@" -DCMAKE_PREFIX_PATH="$prefix"
fi

if ! "$cmake" "$@"; then
    fail 'конфигурирование не прошло: разбор ошибки - в выводе cmake выше'
fi
if ! "$cmake" --build "$build_dir"; then
    fail 'сборка не прошла: разбор ошибки - в выводе cmake выше'
fi

module_dir="$build_dir/CNC/Edges"
qmldir="$module_dir/qmldir"
plugin="$module_dir/libcncedgespluginplugin.so"
backing="$module_dir/libcncedgesplugin.so"
[ -f "$qmldir" ] || fail "модуль не создан: нет $qmldir"
[ -f "$plugin" ] || fail "модуль не создан: нет $plugin"
[ -f "$backing" ] || fail "модуль не создан: нет $backing"

echo ''
echo '--- CNC/Edges/qmldir ---'
cat "$qmldir"
echo "build-viewer: модуль  $module_dir"
echo "build-viewer: плагин  $plugin"
echo "build-viewer: import path для QML: $build_dir"

# Probes: the same targets as on Windows, only without the .exe suffix. They are run by
# the guards (tools/checks.txt) and by a human.
for probe_name in stepledges_probe gcode_probe version_probe geometry_probe step_probe stl_probe \
                  silhouette_probe tangents_probe; do
    probe="$build_dir/$probe_name"
    [ -x "$probe" ] || fail "пробник не собран: нет $probe"
    echo "build-viewer: пробник $probe"
done

backend_import="$root/build/qmlbackend"
backend_module="$backend_import/MockBackend"
[ -f "$backend_module/qmldir" ] || fail "QML-модуль MockBackend не собран: нет $backend_module/qmldir"
[ -f "$backend_module/libgvqmlbackend.so" ] || fail "контроллер не собран: нет $backend_module/libgvqmlbackend.so"
echo "build-viewer: модуль MockBackend $backend_module"
echo "build-viewer: import path контроллера: $backend_import"

viewer="$root/build/viewer/gcodeviewer"
[ -x "$viewer" ] || fail "точка входа вьювера не собрана: нет $viewer"
echo "build-viewer: точка входа вьювера $viewer"

keyprobe="$root/build/viewer/keyprobe"
[ -x "$keyprobe" ] || fail "пробник клавиш не собран: нет $keyprobe"
echo "build-viewer: пробник клавиш $keyprobe"

echo 'build-viewer: OK'
exit 0
