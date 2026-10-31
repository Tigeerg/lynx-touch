# apps/qt-interface/CMakeLists.txt - the standalone Qt panel for debugging on Windows.
#
# The panel is one QML file (main.qml) plus the built viewer modules. The executable is a normal
# window: no console, 1024x600, the QML import paths are added in main.cpp.
cmake_minimum_required(VERSION 3.16)
project(lynx_panel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Quick3D)

add_executable(lynx-panel WIN32 main.cpp)
target_link_libraries(lynx-panel PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Quick3D)
set_target_properties(lynx-panel PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}"
)
if (MSVC)
    target_compile_options(lynx-panel PRIVATE /W4 /WX /utf-8)
endif()
