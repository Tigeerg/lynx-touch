#!/bin/sh
# tools/check.sh - сторож стиля и структуры проекта под Linux (правила - CLAUDE.md).
#
# Запуск:  bash tools/check.sh
#
# Исполняет общий список проверок tools/checks.txt; тем же списком правится
# список в tools/check.ps1 (pnpm guard под Windows), так что добавить проверку -
# одна строка манифеста. Текстовые правила CLAUDE.md, метки размеров файлов и
# pnpm-прогоны React остаются в tools/check.ps1: им нужен Windows или node; на
# Linux они печатаются как skip.
#
# Питоновского слоя нет: все проверки - C++ (пробники и точка входа вьювера).
#
# Проверка, которой не хватает инструмента (нет собранных пробников, QML-модуля
# MockBackend или точки входа вьювера), печатается как skip: на машине без
# тулчейна сторож остаётся зелёным. Красная проверка печатает свой вывод целиком.
#
# Код возврата: 0 - все исполняемые проверки зелёные; 1 - есть красная.

set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root"

n_ok=0
n_fail=0
n_skip=0

print_ok() { printf 'ok    %s\n' "$1"; n_ok=$((n_ok + 1)); }
print_bad() { printf 'FAIL  %s\n' "$1"; n_fail=$((n_fail + 1)); }
print_skip() { printf 'skip  %s\n' "$1"; n_skip=$((n_skip + 1)); }

# Собранное: пробники в build/edges, модуль контроллера в build/qmlbackend,
# точка входа вьювера в build/viewer. Сборка - bash scripts/build-viewer.sh.
have_probe=0
if [ -x build/edges/gcode_probe ]; then
    have_probe=1
fi
have_plugin=0
if [ -f build/qmlbackend/MockBackend/qmldir ] && [ -x build/viewer/gcodeviewer ]; then
    have_plugin=1
fi

echo "check: пробники = $have_probe, плагин MockBackend и вьювер = $have_plugin"

# Режим без экрана на весь прогон: его наследуют прогоны вьювера и съёмка кадра.
# Кадр offscreen снимается, но сцену Quick3D рисует только живая платформа.
export QT_QPA_PLATFORM=offscreen

while IFS='|' read -r label need cmdline; do
    case "$label" in
        ''|'#'*) continue ;;
    esac
    case "$need" in
        probe|plugin|frame) ;;
        *)
            print_bad "$label: неизвестное требование '$need' в tools/checks.txt"
            continue
            ;;
    esac
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
    # Кода возврата мало: падение внутри прогона его не меняет - прогоны вьювера
    # печатают "loaded OK" и при отказе. Такие отказы обязаны быть видны, поэтому
    # Traceback или строка WARNING/ERROR/ в выводе красят проверку.
    if [ "$code" -ne 0 ] || printf '%s\n' "$log" | grep -qE 'Traceback|^(WARNING|ERROR) '; then
        print_bad "$label (код возврата $code)"
        printf '%s\n' "$log" | sed 's/^/      /'
    else
        print_ok "$label"
    fi
done < tools/checks.txt

# Шаги вне манифеста: им нужен Windows или node (см. tools/check.ps1).
print_skip 'pnpm: typecheck, test, format:check (только Windows/сторож pnpm guard)'
print_skip 'tools/check.ps1: текстовые правила CLAUDE.md, метки размеров, сборка C++ под MSVC'

printf '\ncheck: ok %d, FAIL %d, skip %d\n' "$n_ok" "$n_fail" "$n_skip"
[ "$n_fail" -eq 0 ] || exit 1
exit 0
