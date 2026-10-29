#!/bin/sh
# scripts/build-quickprog.sh - build of the quick programming demo on Linux
# (apps/quickprog, PLAN-QUICKPROG, 7.2).
#
# Usage:  bash scripts/build-quickprog.sh
#          CNC_CMAKE=<cmake> CNC_NINJA=<ninja> CNC_BUILD_DIR=<dir> bash scripts/build-quickprog.sh
#
# The demo needs neither Qt nor OCCT: C++17 and the standard library are enough,
# so this script does not look for SDK directories. It does what
# scripts/build-viewer.sh does for its tools: the compiler is the system one
# unless CNC_CXX names another, the generator is Ninja, the configuration is
# Release. The round-trip cases of quickprog_check link the viewer core parser
# (see apps/quickprog/CMakeLists.txt), the sources are taken from
# modules/gcode-viewer/cpp/core - no SDK, no Qt, no OCCT.
#
# Tools: CNC_CMAKE, CNC_NINJA, CNC_CXX are the values of the environment
# variables, otherwise the first command found in PATH (cmake, ninja or
# ninja-build, g++ or c++ or clang++). The build directory is CNC_BUILD_DIR, by
# default <repo>/build/quickprog.
#
# Built: <build>/quickprog (the console demo) and <build>/quickprog_check (the
# self-check, exit code 0 - all cases passed).
#
# Exit code: 0 - built, the artifacts are in place; 1 - tool not found or build
# error.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$script_dir/.." && pwd)
src_dir="$root/apps/quickprog"
build_dir=${CNC_BUILD_DIR:-$root/build/quickprog}

fail() {
    echo "build-quickprog: FAIL $1" >&2
    exit 1
}

# Tool: the value of the environment variable, otherwise the first command found
# in PATH.
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
    return 0
}

cmake=$(find_tool "${CNC_CMAKE:-}" cmake)
[ -n "$cmake" ] || fail 'cmake не найден: задайте CNC_CMAKE=<путь к cmake>'
ninja=$(find_tool "${CNC_NINJA:-}" ninja ninja-build)
[ -n "$ninja" ] || fail 'ninja не найден: задайте CNC_NINJA=<путь к ninja>'
cxx=$(find_tool "${CNC_CXX:-}" g++ c++ clang++)
[ -n "$cxx" ] || fail 'компилятор не найден: задайте CNC_CXX=<путь к g++ или clang++>'

echo "build-quickprog: cmake=$cmake"
echo "build-quickprog: ninja=$ninja"
echo "build-quickprog: cxx  =$cxx"
echo "build-quickprog: build=$build_dir"

# The configure parameters are collected into positional ones: a path with a
# space reaches cmake as a single word.
set -- -S "$src_dir" -B "$build_dir" -G Ninja -DCMAKE_BUILD_TYPE=Release \
       -DCMAKE_CXX_COMPILER="$cxx" -DCMAKE_MAKE_PROGRAM="$ninja"

if ! "$cmake" "$@"; then
    fail 'конфигурирование не прошло: разбор ошибки - в выводе cmake выше'
fi
if ! "$cmake" --build "$build_dir"; then
    fail 'сборка не прошла: разбор ошибки - в выводе cmake выше'
fi

app="$build_dir/quickprog"
check="$build_dir/quickprog_check"
[ -x "$app" ] || fail "демо не собрано: нет $app"
[ -x "$check" ] || fail "проверка не собрана: нет $check"

echo "build-quickprog: демо     $app"
echo "build-quickprog: проверка $check"
echo 'build-quickprog: OK'
exit 0
