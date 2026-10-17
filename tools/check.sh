#!/bin/sh
# tools/check.sh - сторож стиля и структуры проекта под Linux (правила - CLAUDE.md).
#
# Запуск:  bash tools/check.sh
#
# Исполняет общий список проверок tools/checks.txt: тот же список читает
# tools/check.ps1 (pnpm guard под Windows), так что добавить проверку - одна
# строка манифеста. Текстовые правила CLAUDE.md, метки размеров файлов, сверка
# preview.qml с генератором, pnpm-прогоны React и сборка C++ под MSVC остаются в
# tools/check.ps1: им нужен Windows или node; на Linux они печатаются как skip.
#
# Проверка, которой не хватает инструмента (нет python, пакета OCP или
# собранного плагина MockBackend), печатается как skip: на машине без тулчейна
# сторож остаётся зелёным. Красная проверка печатает свой вывод целиком.
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

# Интерпретатор: в манифесте команды написаны с python, на Linux это python3.
python_bin=$(command -v python 2>/dev/null || command -v python3 2>/dev/null || true)

have_ocp=0
if [ -n "$python_bin" ] && "$python_bin" -c 'import OCP' 2>/dev/null; then
    have_ocp=1
fi
have_plugin=0
if [ -f build/qmlbackend/MockBackend/qmldir ]; then
    have_plugin=1
fi

# Плагин собран против libstdc++ своего SDK (нужен GLIBCXX_3.4.32), а python
# стартует со своей системной - QML не грузит плагин ("version not found").
# Прогон плагиновых проверок идёт с LD_LIBRARY_PATH из RUNPATH самого плагина:
# каталог подбирается из собранного .so, а не прописывается вручную.
plugin_runpath=''
if [ "$have_plugin" = '1' ] && command -v readelf >/dev/null 2>&1; then
    plugin_runpath=$(readelf -d build/qmlbackend/MockBackend/libgvqmlbackendplugin.so 2>/dev/null \
        | sed -n 's/.*R\(UN\)\?PATH.*\[\(.*\)\]/\2/p' | head -1)
fi

echo "check: python = ${python_bin:-не найден}, OCP = $have_ocp, плагин MockBackend = $have_plugin"

# Режим без экрана на весь прогон: его наследуют прогоны вьювера и qt-interface.
export QT_QPA_PLATFORM=offscreen

while IFS='|' read -r label need cmdline; do
    case "$label" in
        ''|'#'*) continue ;;
    esac
    case "$need" in
        python|ocp|plugin|live) ;;
        *)
            print_bad "$label: неизвестное требование '$need' в tools/checks.txt"
            continue
            ;;
    esac
    if [ -z "$python_bin" ]; then
        print_skip "$label (нет python)"
        continue
    fi
    if [ "$need" = 'ocp' ] && [ "$have_ocp" != '1' ]; then
        print_skip "$label (нет пакета OCP: pip install -r requirements-dev.txt)"
        continue
    fi
    if { [ "$need" = 'plugin' ] || [ "$need" = 'live' ]; } && [ "$have_plugin" != '1' ]; then
        print_skip "$label (нет собранного плагина MockBackend: bash scripts/build-viewer.sh)"
        continue
    fi
    if [ "$need" = 'live' ] && [ "${CNC_LIVE_FRAMES:-}" != '1' ]; then
        print_skip "$label (нужен эталонный стенд: аппаратный GL и живое окно; включается CNC_LIVE_FRAMES=1)"
        continue
    fi
    # В манифесте команда начинается с python - подставляем найденный интерпретатор.
    run=$(printf '%s' "$cmdline" | sed "s|^python |$python_bin |")
    if [ "$need" = 'live' ]; then
        # Живая проверка кадра требует живого окна: offscreen отдаёт чёрный кадр.
        log=$(env -u QT_QPA_PLATFORM LD_LIBRARY_PATH="$plugin_runpath${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" sh -c "$run" 2>&1)
    elif [ "$need" = 'plugin' ] && [ -n "$plugin_runpath" ]; then
        log=$(LD_LIBRARY_PATH="$plugin_runpath${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" sh -c "$run" 2>&1)
    else
        log=$(sh -c "$run" 2>&1)
    fi
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
