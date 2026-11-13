# apps/qt-interface/CMakeLists.txt - the standalone Qt panel for debugging on Windows.
#
# The panel is main.qml plus the backend contract. `lynx-backend` is the only place that knows
# whether the data comes from the simulator or, later, from LinuxCNC; QML only binds to it.
cmake_minimum_required(VERSION 3.16)
project(lynx_panel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Quick3D Test)

# The LinuxCNC link is written on the controller: without this option the build has the adapter and
# its tests, and `--backend=linuxcnc` reports that the link is missing (docs/LINUXCNC-BACKEND.md).
option(LYNX_WITH_LINUXCNC "Build against the LinuxCNC libraries (controller only)" OFF)

add_library(lynx-backend STATIC
    backend/machine_types.cpp
    backend/machine_types.h
    backend/machine_backend.cpp
    backend/machine_backend.h
    backend/sim_state.h
    backend/fixture_reader.cpp
    backend/fixture_reader.h
    backend/sim_backend.cpp
    backend/sim_backend.h
    backend/linuxcnc_link.h
    backend/linuxcnc_backend.cpp
    backend/linuxcnc_backend.h
    backend/linuxcnc_nml_link.cpp
    backend/backend_factory.cpp
    backend/backend_factory.h
)
if (LYNX_WITH_LINUXCNC)
    target_compile_definitions(lynx-backend PUBLIC LYNX_WITH_LINUXCNC)
endif()
target_include_directories(lynx-backend PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/backend")
target_link_libraries(lynx-backend PUBLIC Qt6::Core)
if (MSVC)
    target_compile_options(lynx-backend PRIVATE /W4 /WX /utf-8)
endif()

add_executable(lynx-panel WIN32 main.cpp)
target_link_libraries(lynx-panel PRIVATE lynx-backend Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Quick3D)

enable_testing()
add_executable(lynx-backend-test backend/tests/tst_backend.cpp)
target_link_libraries(lynx-backend-test PRIVATE lynx-backend Qt6::Test)
if (MSVC)
    target_compile_options(lynx-backend-test PRIVATE /W4 /WX /utf-8)
endif()
add_test(NAME lynx-backend COMMAND lynx-backend-test)

# The LinuxCNC adapter is tested with a fake link, so it can be checked on any platform.
add_executable(lynx-linuxcnc-test backend/tests/tst_linuxcnc.cpp)
target_link_libraries(lynx-linuxcnc-test PRIVATE lynx-backend Qt6::Test)
if (MSVC)
    target_compile_options(lynx-linuxcnc-test PRIVATE /W4 /WX /utf-8)
endif()
add_test(NAME lynx-linuxcnc COMMAND lynx-linuxcnc-test)

set_target_properties(lynx-panel lynx-backend-test lynx-linuxcnc-test PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
)
if (MSVC)
    target_compile_options(lynx-panel PRIVATE /W4 /WX /utf-8)
endif()
