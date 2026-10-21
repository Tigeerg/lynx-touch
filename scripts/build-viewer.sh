#!/bin/sh
# scripts/build-viewer.sh - сборка вьювера УП на C++ под Linux: QML-плагин CNC.Edges,
# ядро gvcore, геометрии gvqtgeom, слой STEP gvstep, QML-модуль MockBackend и точка
# входа build/viewer/gcodeviewer.
#
# Запуск:  bash scripts/build-viewer.sh
#          CNC_QT_DIR=<каталог Qt6> CNC_OCC_DIR=<каталог OCCT> bash scripts/build-viewer.sh
#
# Окружение SDK и компилятора на Linux собирает environment.yml в корне
# (conda-forge): conda env create -f environment.yml. Компилятор в окружении -
# не роскошь: conda-сборка OCCT линкуется символом __cxa_call_terminate@CXXABI_1.3.15,
# которого нет в GCC 11 системных Ubuntu 22.04.
#
# Скрипт делает то же, что scripts/build-viewer.ps1 (разбор целей и артефактов - там
# же), минус Windows: окружение MSVC не снимается, vcvars64.bat не нужен, компилятор
# и загрузчик - системные. Пути SDK берутся из окружения: CNC_QT_DIR (Qt6 с Quick3D),
# CNC_OCC_DIR (OCCT с CMake-пакетом), CNC_CMAKE, CNC_NINJA. Не заданы - берётся
# $CONDA_PREFIX в раскладке conda-forge (lib/cmake/Qt6 и lib/cmake/opencascade), а
# если и там их нет - переменные не задаются вовсе, и пути ищет сам find_package.
# Каталог сборки - CNC_BUILD_DIR, по умолчанию <repo>/build/edges.
#
# Загрузчик находит Qt и OCCT по rpath сборки (CMake заполняет его сам, BUILD_RPATH),
# поэтому LD_LIBRARY_PATH не нужен, и точка входа (cpp/viewer/main.cpp) на Linux PATH
# не трогает.
#
# Собранное: <build>/CNC/Edges/ (qmldir, libcncedgesplugin.so,
# libcncedgespluginplugin.so),
# <repo>/build/qmlbackend/MockBackend/ (libgvqmlbackend.so, libgvqmlbackendplugin.so),
# <repo>/build/viewer/gcodeviewer.
#
# Код возврата: 0 - собрано, артефакты на месте; 1 - инструмент не найден или ошибка
# сборки.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$script_dir/.." && pwd)
src_dir="$root/modules/gcode-viewer/cpp"
build_dir=${CNC_BUILD_DIR:-$root/build/edges}

fail() {
    echo "build-viewer: FAIL $1" >&2
    exit 1
}

# Каталог Qt6: внутри ожидается lib/cmake/Qt6/Qt6Config.cmake.
is_qt_dir() {
    [ -n "$1" ] && [ -f "$1/lib/cmake/Qt6/Qt6Config.cmake" ]
}

# Каталог OCCT: внутри ожидается lib/cmake/opencascade/OpenCASCADEConfig.cmake.
is_occ_dir() {
    [ -n "$1" ] && [ -f "$1/lib/cmake/opencascade/OpenCASCADEConfig.cmake" ]
}

# Инструмент: значение переменной окружения, иначе первая найденная команда из
# PATH, иначе - из bin каталогов Qt/OCCT/окружения: conda-окружение держит cmake
# и ninja в своём bin, и без активации окружения их в PATH нет.
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

# CMAKE_PREFIX_PATH: найденные каталоги через точку с запятой. Ничего не задано -
# ключ не передаётся вовсе: пути ищет find_package.
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

# Параметры конфигурирования собираются в позиционные: путь с пробелом доезжает до
# cmake одним словом.
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

# Пробники: те же цели, что под Windows, только без суффикса .exe. Их гоняют
# сторожа (tools/checks.txt) и человек.
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
