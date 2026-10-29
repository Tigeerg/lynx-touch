# apps/quickprog/CMakeLists.txt - the quick programming demo (PLAN-QUICKPROG, 7.2).
#
# C++17 and the standard library only: neither Qt nor OCCT, so the demo builds
# on any machine with a compiler. Targets:
#   quickprog        the console application;
#   quickprog_check  the self-check, exit code 0 - all cases passed, 1 - a
#                    mismatch. Its round-trip cases parse the generated file with
#                    the viewer core (gv::parseGcode / gv::parseUi*) and link a
#                    local copy of the parser, gvcore_quickprog.
#
# Build (bash scripts/build-quickprog.sh does the same):
#   cmake -S apps/quickprog -B build/quickprog -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build/quickprog
cmake_minimum_required(VERSION 3.16)
project(quickprog LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Output goes straight into the build directory, not into Debug/Release
# subdirectories: the same path in every generator, so scripts and the report
# can name the binary without guessing.
set(QP_OUTPUT_DIR "${CMAKE_BINARY_DIR}")

# Exact numbers of the emitted file must not change with a compiler flag: the
# geometry is compared by coordinates, and contraction of a multiply with an add
# (FMA) shifts the last digits.
if (MSVC)
    add_compile_options(/W4 /WX /utf-8 /fp:precise)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror -ffp-contract=off -fno-fast-math)
endif()

# The core is a library on its own: the application and the check use the same
# code, and the core does not know about the console.
add_library(quickprog_core STATIC
    core/cli.cpp
    core/emit.cpp
    core/emit_moves.cpp
    core/face_options.cpp
    core/option_words.cpp
    core/circle_pocket_options.cpp
    core/drill_options.cpp
    core/pocket_options.cpp
    core/qprog_operation.cpp
    core/feeds.cpp
    core/path.cpp
    core/path_face.cpp
    core/path_pocket.cpp
    core/path_circle_pocket.cpp
    core/path_drill.cpp
    core/serialize.cpp
    core/serialize_read.cpp
    core/serialize_shared.cpp
    core/summary.cpp
    core/validate.cpp
)
target_include_directories(quickprog_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# The viewer core for the round-trip: the parser and the UI_* tags of
# modules/gcode-viewer/cpp/core, linked into the check as a static library of
# this build. modules/** is not touched and its CMakeLists is not included
# (it does find_package(Qt6 REQUIRED)), so the sources are named here.
#
# The set is what the parser itself pulls (see `#include` in gcode_parser.cpp):
# gcode_parser.cpp, text_util.cpp, ui_tags.cpp. normals/outline/stl/tooling and
# the rest of gvcore are not needed - the check reads a path and tags, not
# geometry. The sources are compiled as-is from the module, no copy in the tree.
#
# The flags repeat gvcore: -ffp-contract=off -fno-fast-math (/fp:precise on
# MSVC). The parser reproduces the reference byte for byte and computes the CPython
# hypot with std::fma: contraction of a multiply with an add shifts the last
# digits of the points, and the round-trip would then disagree with the viewer
# for a reason of the build, not of the generator.
set(GV_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../modules/gcode-viewer/cpp/core")
add_library(gvcore_quickprog STATIC
    ${GV_CORE_DIR}/gcode_parser.cpp
    ${GV_CORE_DIR}/text_util.cpp
    ${GV_CORE_DIR}/ui_tags.cpp
)
# The core headers are included as "core/gcode_parser.h" and "core/ui_tags.h"
# with <viewer cpp> in the search path. The demo has its own core/ directory too,
# but no file name collides: the quickprog core/ has no gcode_parser.h,
# ui_tags.h, gv_types.h or text_util.h, so "core/<name>" resolves to the viewer
# only for the parser headers (see the includes of checks_roundtrip.cpp).
target_include_directories(gvcore_quickprog PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/gcode-viewer/cpp)
# The core target owns its warnings: /W4 /WX above is for the demo. A change
# in modules/** that warns would otherwise fail the demo build for a reason of
# the viewer, not of the quick programming. add_compile_options applies to the
# whole directory, so -Werror is cancelled for this one target.
if (MSVC)
    target_compile_options(gvcore_quickprog PRIVATE /utf-8 /fp:precise /W3 /WX-)
else()
    target_compile_options(gvcore_quickprog PRIVATE -ffp-contract=off -fno-fast-math -Wno-error)
endif()

add_executable(quickprog main.cpp)
target_link_libraries(quickprog PRIVATE quickprog_core)
set_target_properties(quickprog PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${QP_OUTPUT_DIR}")

# The cases are split by meaning; every file opens its own anonymous
# namespace, and quickprog_check.cpp declares and calls them.
add_executable(quickprog_check
    tests/quickprog_check.cpp
    tests/checks_emit.cpp
    tests/checks_face.cpp
    tests/checks_pocket.cpp
    tests/checks_circle.cpp
    tests/checks_drill.cpp
    tests/checks_summary.cpp
    tests/checks_emit_coolant.cpp
    tests/checks_qprog.cpp
    tests/checks_roundtrip.cpp
    tests/roundtrip_fixtures.h
)
target_link_libraries(quickprog_check PRIVATE quickprog_core gvcore_quickprog)
set_target_properties(quickprog_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${QP_OUTPUT_DIR}")

enable_testing()
add_test(NAME quickprog_check COMMAND quickprog_check)
