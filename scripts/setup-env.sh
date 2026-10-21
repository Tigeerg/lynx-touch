#!/bin/sh
# scripts/setup-env.sh - the environment and the build of the NC 3D viewer on Linux in one step.
#
# Usage:  bash scripts/setup-env.sh
#
# Step by step:
#   1. Finds conda (PATH, then $HOME/miniconda3 and $HOME/anaconda3); if it is not there, it
#      prints what to install and exits with code 1.
#   2. Creates the gcode-cpp environment from environment.yml (if it already exists - skips it).
#   3. Builds the viewer with the same environment: conda run ... bash scripts/build-viewer.sh
#      (the SDK and the compiler are taken from the environment, see environment.yml).
#
# The install is user-level: dotfiles and PATH are not changed, and cleanup is two
# commands, the script prints them at the end.
#
# Exit code: 0 - the environment and the build are in place; 1 - conda was not found or the
# build failed.

set -eu

script_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$script_dir/.." && pwd)
env_name=gcode-cpp

fail() {
    echo "setup-env: FAIL $1" >&2
    exit 1
}

# conda: the command from PATH, otherwise the binary of a user-level Miniconda install.
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
