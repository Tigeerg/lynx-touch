# apps/quickprog/CMakeLists.txt - the quick programming demo (PLAN-QUICKPROG, 7.2).
#
# C++17 and the standard library only: neither Qt nor OCCT, so the demo builds
# on any machine with a compiler. Two targets:
#   quickprog        the console application;
#   quickprog_check  the self-check, exit code 0 - all cases passed, 1 - a
#                    mismatch. The round-trip through the viewer core
#                    (gv::parseGcode) is a later slice and is not linked here.
#
# Build (the manual commands are in the report; a script is a later slice):
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
    core/feeds.cpp
    core/path.cpp
    core/path_face.cpp
    core/validate.cpp
)
target_include_directories(quickprog_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(quickprog main.cpp)
target_link_libraries(quickprog PRIVATE quickprog_core)
set_target_properties(quickprog PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${QP_OUTPUT_DIR}")

add_executable(quickprog_check tests/quickprog_check.cpp)
target_link_libraries(quickprog_check PRIVATE quickprog_core)
set_target_properties(quickprog_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${QP_OUTPUT_DIR}")

enable_testing()
add_test(NAME quickprog_check COMMAND quickprog_check)
