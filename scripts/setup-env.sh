#!/bin/sh
# scripts/setup-env.sh - окружение и сборка 3D-вьювера УП на Linux одним шагом.
#
# Запуск:  bash scripts/setup-env.sh
#
# По шагам:
#   1. Находит conda (PATH, затем $HOME/miniconda3 и $HOME/anaconda3); её нет -
#      печатает, что поставить, и выходит с кодом 1.
#   2. Создаёт окружение gcode-cpp из environment.yml (уже есть - пропускает).
#   3. Собирает вьювер тем же окружением: conda run ... bash scripts/build-viewer.sh
#      (SDK и компилятор берутся из окружения, см. environment.yml).
#
# Установка пользовательская: dotfiles и PATH не меняются, и подчистка - две
# команды, их скрипт печатает в конце.
#
# Код возврата: 0 - окружение и сборка на месте; 1 - conda не найдена или
# сборка не прошла.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$script_dir/.." && pwd)
env_name=gcode-cpp

fail() {
    echo "setup-env: FAIL $1" >&2
    exit 1
}

# conda: команда из PATH, иначе бинарь пользовательских установок Miniconda.
conda=''
for candidate in "$(command -v conda 2>/dev/null || true)" \
                 "${HOME:-}/miniconda3/bin/conda" \
                 "${HOME:-}/anaconda3/bin/conda"; do
    if [ -n "$candidate" ] && [ -x "$candidate" ]; then
        conda=$candidate
        break
    fi
done
[ -n "$conda" ] || fail 'conda не найдена: поставьте Miniconda (docs.conda.io/en/latest/miniconda.html)
и повторите - скрипт подхватит $HOME/miniconda3/bin/conda'
echo "setup-env: conda = $conda"

if "$conda" env list 2>/dev/null | awk '{print $1}' | grep -qx "$env_name"; then
    echo "setup-env: окружение $env_name уже есть - создание пропущено"
else
    echo "setup-env: создаю окружение $env_name из environment.yml"
    "$conda" env create -f "$root/environment.yml" \
        || fail 'окружение не создалось: разбор ошибки - в выводе conda выше'
fi

echo "setup-env: собираю вьювер (conda run -n $env_name)"
"$conda" run -n "$env_name" --no-capture-output bash "$root/scripts/build-viewer.sh" \
    || fail 'сборка не прошла: разбор ошибки - в выводе cmake выше'

base=$("$conda" info --base 2>/dev/null || echo '<каталог conda>')
cat <<EOF

setup-env: готово. Запуск вьювера:
  ./build/viewer/gcodeviewer data/cad-testdata/Test_model.ngc

Как подчистить (установка пользовательская, dotfiles и PATH не менялись):
  $conda env remove -n $env_name   # окружение SDK и компилятора
  rm -rf $root/build               # артефакты сборки вьювера
Если conda ставилась только ради этого, её можно удалить целиком:
  rm -rf $base
EOF
