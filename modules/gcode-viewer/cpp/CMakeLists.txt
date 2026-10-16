# QML-плагин "CNC.Edges" — StepEdges: рёбра B-Rep модели как Lines-геометрия.
# Рядом собирается чистое ядро gvcore (парсер G-кода, теги UI_*) и его пробник
# gcode_probe — они в плагин не входят и Qt не требуют.
#
# Сборка (скрипт делает то же самое):
#   powershell -ExecutionPolicy Bypass -File scripts/build-viewer.ps1
# Вручную:
#   call <VS2022>\VC\Auxiliary\Build\vcvars64.bat
#   cmake -S modules/gcode-viewer/cpp -B build/edges -G Ninja -DCMAKE_BUILD_TYPE=Release ^
#         -DCMAKE_PREFIX_PATH="<Qt6>/msvc2022_64;<OCCT>/Library"
#   cmake --build build/edges
#
# Модуль кладётся в build/edges/CNC/Edges/: qmldir, cncedgesplugin.dll (резервная
# библиотека с типами и алгоритмом) и cncedgespluginplugin.dll (точка входа, её
# грузит QML). Импорт-путь для потребителя — build/edges; там же лежит пробник
# stepledges_probe.exe:
#   QML:      import CNC.Edges 1.0
#   PySide6:  engine.addImportPath("<repo>/build/edges")
# Библиотеки OCCT линкуются по импортированным целям TK* из конфиг-пакета, а если их
# в пакете нет — по переменным OpenCASCADE_INCLUDE_DIR/OpenCASCADE_LIBRARY_DIR.

cmake_minimum_required(VERSION 3.16)
project(cnc_edges_plugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick Quick3D)
find_package(OpenCASCADE REQUIRED)

# Чистое ядро gvcore (этап 2 плана переноса): парсер G-кода, теги UI_*. Ни Qt, ни
# OCCT не нужны - только стандартная библиотека. Цель отдельная от плагина, чтобы
# ядро можно было собрать и сверить с Python без Qt.
#
# /fp:precise обязателен: ядро сверяется с Python побайтово, а /fp:fast разрешает
# переассоциацию и слияние умножения со сложением (FMA), что сдвигает результат.
add_library(gvcore STATIC
    core/gcode_parser.cpp
    core/path_prep.cpp
    core/text_util.cpp
    core/ui_tags.cpp
)
target_include_directories(gvcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if (MSVC)
    target_compile_options(gvcore PRIVATE /utf-8 /fp:precise)
else()
    target_compile_options(gvcore PRIVATE -ffp-contract=off -fno-fast-math)
endif()

# Пробник ядра: отдельная цель, в плагин не входит. Печатает точки, флаги,
# этапы и sha256 тех же сериализаций, что считает Python, и пишет те же массивы
# дампом; проверка - modules/gcode-viewer/test_gcode_core.py.
add_executable(gcode_probe gcode_probe.cpp)
set_target_properties(gcode_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(gcode_probe PRIVATE /utf-8)
endif()
target_link_libraries(gcode_probe PRIVATE gvcore)

qt_standard_project_setup()
if(COMMAND qt_policy)
    # Префикс ресурсов QML-модуля ':/qt/qml/' — значение по умолчанию для новых проектов.
    qt_policy(SET QTP0001 NEW)
endif()

# Модуль разделён по образцу Qt: резервная библиотека cncedgesplugin (SHARED) несёт
# типы и алгоритм, плагин cncedgespluginplugin (MODULE) — точка входа, которую QML
# грузит по строке "plugin cncedgespluginplugin" в qmldir. Резервная библиотека
# линкуется в пробник; MODULE-библиотеку CMake линковать не разрешает.
qt_add_library(cncedgesplugin SHARED)

set_target_properties(cncedgesplugin PROPERTIES
    QT_QML_GENERATE_QMLLS_INI OFF
    # Резервная библиотека лежит рядом с плагином: иначе QML не найдёт её при загрузке.
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/CNC/Edges
    # Windows: класс StepEdgesGeometry нужен пробнику, а по умолчанию DLL ничего не экспортирует.
    WINDOWS_EXPORT_ALL_SYMBOLS ON
)

qt_add_qml_module(cncedgesplugin
    URI CNC.Edges
    VERSION 1.0
    # Каталог модуля повторяет URI: <build>/CNC/Edges/. Импорт-путь потребителя — <build>.
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/CNC/Edges
    SOURCES
        stepledgesgeometry.h
        stepledgesgeometry.cpp
)

target_compile_definitions(cncedgesplugin PRIVATE OCCT_HANDLE_NOCAST)
if (MSVC)
    target_compile_options(cncedgesplugin PRIVATE /utf-8 /MP)
endif()

# OpenCASCADE: сначала импортированные цели (TKxxx::), иначе классические переменные
# из старого конфиг-пакета.
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

# Пробник сверки с питоновским эталоном: отдельная цель, в плагин не входит.
# Читает геометрию через сам плагин (setSource -> setShape), печатает число
# отрезков и sha256 буфера; сравнение с эталоном ведёт
# modules/gcode-viewer/test_stepledges_plugin.py.
add_executable(stepledges_probe stepledges_probe.cpp)
set_target_properties(stepledges_probe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
if (MSVC)
    target_compile_options(stepledges_probe PRIVATE /utf-8 /MP)
endif()
target_link_libraries(stepledges_probe PRIVATE cncedgesplugin Qt6::Core Qt6::Qml Qt6::Quick3D)
target_include_directories(stepledges_probe PRIVATE ${OpenCASCADE_INCLUDE_DIR})
