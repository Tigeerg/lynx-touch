# QML extension plugin "CNC.Edges" - StepEdges (B-Rep edge Lines geometry).
#
# Configure (SDK paths are the same as scripts/build-viewer.ps1 uses):
#   cmake -S modules/gcode-viewer/cpp -B build/edges -G "Visual Studio 18 2026" -A x64 ^
#         -DQt6_DIR=<Qt>/lib/cmake/Qt6 -DOpenCASCADE_DIR=<OCCT>/cmake
#   cmake --build build/edges --config Release
#
# The plugin lands into build/edges/CNC/Edges/; consumers add that directory:
#   QML:  import CNC.Edges 1.0
#   PySide6:  engine.addImportPath("<build>/edges")
# Linking note: OCCT libs are resolved via the config-package targets when
# available (TK* imported), otherwise via OpenCASCADE_{INCLUDE_DIR,LIBRARY_DIR}.

cmake_minimum_required(VERSION 3.16)
project(cnc_edges_plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Quick3D)
find_package(OpenCASCADE REQUIRED)

qt_standard_project_setup()

qt_add_library(cncedgesplugin MODULE)

set_target_properties(cncedgesplugin PROPERTIES
    QT_QML_GENERATE_QMLLS_INI OFF
)

qt_add_qml_module(cncedgesplugin
    URI CNC.Edges
    VERSION 1.0
    SOURCES
        stepledgesgeometry.h
        stepledgesgeometry.cpp
    NO_PLUGIN_CLASS           # QML_ELEMENT from C++ registers the type
)

target_compile_definitions(cncedgesplugin PRIVATE OCCT_HANDLE_NOCAST)
if (MSVC)
    target_compile_options(cncedgesplugin PRIVATE /utf-8 /MP)
endif()

# OpenCASCADE: prefer modern imported targets (TKxxx::), fall back to the
# classic variables provided by older configs.
set(OCC_NEEDED TKernel TKMath TKTopAlgo TKGeomBase TKBRep TKDESTEP TKXSBase)
foreach(lib IN LISTS OCC_NEEDED)
    if (TARGET OpenCASCADE::${lib})
        target_link_libraries(cncedgesplugin PRIVATE OpenCASCADE::${lib})
    else()
        target_link_libraries(cncedgesplugin PRIVATE ${lib})
    endif()
endforeach()
target_include_directories(cncedgesplugin PRIVATE ${OpenCASCADE_INCLUDE_DIR})

target_link_libraries(cncedgesplugin PRIVATE
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::Quick3D)
