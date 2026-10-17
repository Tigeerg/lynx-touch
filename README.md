# CNC Панель оператора

Панель оператора ЧПУ. Состоит из четырёх проектов: React-интерфейс, Qt-порт
интерфейса, 3D-вьювер управляющих программ и тестовые данные/постпроцессор.

**Версия продукта:** `0.15.1` (см. [`VERSION`](VERSION)).
Версии проектов и правила ведения — в [`projects.json`](projects.json) и
[`VERSIONING.md`](VERSIONING.md).

## Проекты

### 1. React интерфейс — `react-interface` (`0.11.0`)
Основной интерфейс панели оператора (Next.js, сгенерирован в v0.dev). Здесь
отрабатываются отображение и основная логика ЧПУ. Два режима UI: экранная
панель (softkey) и сенсорный; маршрут `/classic` — классическая панель.

- Экраны/логика: `components/cnc/`, `lib/cnc/` (`machine.ts`, `toolpath.ts`, `probe.ts`, …)
- Сенсорный режим: `components/cnc/touch/`, переключатель — `ui-mode.tsx`
- Точка входа: `app/page.tsx`
- Запуск: `pnpm install && pnpm dev`

### 2. Qt-порт интерфейса — `qt-interface` (`0.5.4`)
Перенос React-интерфейса на Qt/QML (`apps/qt-interface`).

- `main.qml`, `preview.qml`, генератор `gen_preview_qml.py`, ассеты/шрифты
- Запуск: `python apps/qt-interface/preview_qml.py`

### 3. 3D-вьювер УП — `gcode-viewer` (`0.45.0`)
3D-визуализация управляющей программы (`modules/gcode-viewer`).

- `GcodeViewer3D.qml` — сцена/камера/материалы/анимация/панель режимов
- `mock_backend.py` — парсер G-кода, геометрии, инструмент/оправка, этапы,
  скорости 1:1, анализ столкновений, проверка инструмента
- `toolpath.vert` / `toolpath.frag` — шейдеры линий
- Зависимости Python-проверок и PySide6-инструментов — [`requirements-dev.txt`](requirements-dev.txt)
  (`pip install -r requirements-dev.txt`): `cadquery-ocp==8.0.1.1.0` — импорт STEP (модель и обводка
  граней из B-rep); без него STEP не читается, вьювер использует STL. Версия
  зафиксирована: 7.7.2 не читает STEP вовсе, а `test_normals_golden.py` сверяет
  тесселяцию бит-в-бит и к версии OCP чувствителен
- Запуск: `python modules/gcode-viewer/preview_3d.py "data/cad-testdata/Test_model.ngc"`
  (старый путь через PySide6; C++-точка входа — в разделе «Сборка и запуск вьювера»)
- Диагностика идёт через `logging`: уровень задаёт `CNC_LOG_LEVEL` (по умолчанию `INFO`,
  `DEBUG` покажет источник контура — STEP или меш; сбои загрузки печатаются как `WARNING`)
  (рядом подхватывается `Test_model.step`, иначе — `Test_model.stl`)
- Журнал изменений: [`modules/gcode-viewer/CHANGELOG.md`](modules/gcode-viewer/CHANGELOG.md)

Возможности вьювера: траектория (G0/G1/G2/G3, циклы сверления), заготовка
(`UI_STOCK`), модель детали (STEP — кожа + настоящие рёбра граней; STL —
fallback), инструмент по данным УП (`UI_TOOL`), оправка ER20, режимы
«весь код / ограничить длину», этапы-операции (`UI_OP`), воспроизведение 1:1
с подачей, анализ столкновений «оправка vs заготовка», проверка соответствия
инструмента УП и таблицы стойки.

#### Сборка и запуск вьювера (C++)

В рантайме вьювер работает на C++: точка входа — `build/viewer/gcodeviewer`
(`modules/gcode-viewer/cpp/viewer/main.cpp`), QML-модуль контроллера — `MockBackend`
из `build/qmlbackend`. Питоновского бэкенда в пути исполнения нет.

Нужны: Qt 6 (Core, Gui, Qml, Quick, Quick3D), OCCT с CMake-пакетом
(`lib/cmake/opencascade/OpenCASCADEConfig.cmake`), CMake не ниже 3.16, Ninja и
компилятор C++17. Проект документирует Qt 6.12.0 и OCCT 8.0.1; коду нужен Qt не ниже
6.7 (`explicitTextureWidth`/`explicitTextureHeight` в `GcodeViewer3D.qml`). Каталоги
задают переменные `CNC_QT_DIR` (пример умолчания — `C:\Qt\6.12.0\msvc2022_64`) и
`CNC_OCC_DIR`; не заданы — сборка ищет их сама.

Windows:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build-viewer.ps1
```

Скрипт снимает окружение MSVC через `vcvars64.bat` и зовёт CMake с генератором Ninja.
Собранное: `build\edges` (QML-модуль `CNC.Edges` и пробники `stepledges_probe`,
`gcode_probe`, `geometry_probe`, `step_probe`, `stl_probe`, `silhouette_probe`,
`tangents_probe`), `build\cpp` (мост `gvbridge.dll`, шим `gvqtshim.dll`),
`build\qmlbackend` (модуль `MockBackend`) и `build\viewer` (`gcodeviewer.exe`,
`keyprobe.exe`). Сборка не прошла — скрипт печатает `build-viewer: FAIL` и называет
недостающее: `vcvars64.bat` (`CNC_VCVARS`), `cmake.exe` (`CNC_CMAKE`) или `ninja.exe`
(`CNC_NINJA`).

Запуск: `build\viewer\gcodeviewer.exe [--smoke] [путь к УП]`. Qt обязан быть в `PATH`
до старта процесса (exe линкуется с `Qt6Core.dll`), каталог плагина и DLL OCCT точка
входа добавляет себе сама:

```powershell
$env:PATH = "C:\Qt\6.12.0\msvc2022_64\bin;$env:PATH"
build\viewer\gcodeviewer.exe data\cad-testdata\Test_model.ngc
```

Живое окно печатает `loaded g-code from: … segments: N` и ни одной строки
`[QML warning]`. Offscreen (`$env:QT_QPA_PLATFORM = "offscreen"`) печатает
`offscreen mode - loaded OK.`; `--smoke` — короткая проверка старта: грузит QML вьювера
и печатает `RESULT: OK (parse-level)`.

Linux: сборка тем же CMake, без MSVC. Практичный путь — окружение conda-forge из
[`environment.yml`](environment.yml); всё одним шагом (окружение + сборка, он же
печатает команды подчистки) — `bash scripts/setup-env.sh`. Вручную:

```bash
conda env create -f environment.yml
conda activate gcode-cpp
CNC_QT_DIR="$CONDA_PREFIX" CNC_OCC_DIR="$CONDA_PREFIX" bash scripts/build-viewer.sh
```

Окружение держит и компилятор (`cxx-compiler`), а не только SDK: conda-сборка
OCCT линкуется символом `__cxa_call_terminate@CXXABI_1.3.15`, которого нет в GCC 11
системных Ubuntu 22.04, — системным компилятором сборка падает на линковке.
Сборка идёт компилятором окружения; после смены компилятора каталог сборки чистят
(`rm -rf build`), CMake помнит компилятор в кэше.

Вручную то же самое:

```bash
cmake -S modules/gcode-viewer/cpp -B build/edges -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$CONDA_PREFIX;$CONDA_PREFIX"
cmake --build build/edges
```

Запуск: Qt и OCCT загрузчик находит по rpath сборки, `LD_LIBRARY_PATH` не нужен (если
SDK собран нестандартно, каталоги Qt и OCCT можно добавить в `LD_LIBRARY_PATH`):

```bash
./build/viewer/gcodeviewer data/cad-testdata/Test_model.ngc
QT_QPA_PLATFORM=offscreen ./build/viewer/gcodeviewer --smoke
```

Ожидается Qt не ниже 6.7: проект документирует 6.12, conda-forge даёт 6.12.0.
Путь проверен 2026-10-17 на Ubuntu 22.04 (окружение из `environment.yml`): сборка
проходит, offscreen-прогон `Test_model.ngc` печатает `segments: 70914` без
`[QML warning]` (эталон), `--smoke` — `RESULT: OK (parse-level)`. Признаки те же,
что на Windows. Известная проблема: полный offscreen-прогон завершается SIGSEGV
после строки `offscreen mode - loaded OK.` (на teardown; `--smoke` завершается
чисто) — смотрите журнал вьювера.

Проверки вьювера: сторож `pnpm guard` (`tools/check.ps1`) зовёт сборку, проверяет
наличие пробников, гоняет `gcodeviewer.exe` offscreen и `keyprobe.exe`; пробники C++
сверяют себя с эталоном `modules/gcode-viewer/oracle_manifest.json` (ключ `--manifest`)
без Python. Питоновские точки входа `preview_3d.py` и `smoke_test_qml.py` запускают тот
же C++-вьювер через PySide6 — это более старый способ, для точки входа
`gcodeviewer.exe` он не нужен.

### 4. Тестовые УП/модели и постпроцессор — `cad-testdata` (`0.5.1`)
- `data/cad-testdata/Test_model.ngc`, `data/cad-testdata/Test_model.STL` — тестовые УП и модель
- `data/cad-testdata/linuxcnc_ui.cps` — постпроцессор с тегами `UI_*` (LinuxCNC)
- `data/cad-testdata/linuxcnc.cps` — базовый постпроцессор

## Быстрый старт

```bash
# React интерфейс
pnpm install
pnpm dev

# 3D-вьювер (сборка - в разделе «Сборка и запуск вьювера (C++)»)
./build/viewer/gcodeviewer data/cad-testdata/Test_model.ngc
```

## Документация

- [`VERSIONING.md`](VERSIONING.md) — версионирование и порядок внесения изменений
- [`projects.json`](projects.json) — манифест проектов и версий
- [`DESIGN.md`](DESIGN.md) / [`DESIGN.en.md`](DESIGN.en.md) — архитектура 3D-вьювера и технический долг
  (русский / английский)
- [`modules/gcode-viewer/CHANGELOG.md`](modules/gcode-viewer/CHANGELOG.md) — журнал вьювера

## Лицензия

Apache License 2.0 — см. [`LICENSE`](LICENSE).
