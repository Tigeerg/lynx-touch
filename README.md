# CNC Панель оператора

Панель оператора ЧПУ. Состоит из четырёх проектов: React-интерфейс, Qt-порт
интерфейса, 3D-вьювер управляющих программ и тестовые данные/постпроцессор.

**Версия продукта:** `0.11.0` (см. [`VERSION`](VERSION)).
Версии проектов и правила ведения — в [`projects.json`](projects.json) и
[`VERSIONING.md`](VERSIONING.md).

## Проекты

### 1. React интерфейс — `react-interface` (`0.7.0`)
Основной интерфейс панели оператора (Next.js, сгенерирован в v0.dev). Здесь
отрабатываются отображение и основная логика ЧПУ. Два режима UI: экранная
панель (softkey) и сенсорный; маршрут `/classic` — классическая панель.

- Экраны/логика: `components/cnc/`, `lib/cnc/` (`machine.ts`, `toolpath.ts`, `probe.ts`, …)
- Сенсорный режим: `components/cnc/touch/`, переключатель — `ui-mode.tsx`
- Точка входа: `app/page.tsx`
- Запуск: `pnpm install && pnpm dev`

### 2. Qt-порт интерфейса — `qt-interface` (`0.5.1`)
Перенос React-интерфейса на Qt/QML (`apps/qt-interface`).

- `main.qml`, `preview.qml`, генератор `gen_preview_qml.py`, ассеты/шрифты
- Запуск: `python apps/qt-interface/preview_qml.py`

### 3. 3D-вьювер УП — `gcode-viewer` (`0.13.12`)
3D-визуализация управляющей программы (`apps/qt-interface`).

- `GcodeViewer3D.qml` — сцена/камера/материалы/анимация/панель режимов
- `mock_backend.py` — парсер G-кода, геометрии, инструмент/оправка, этапы,
  скорости 1:1, анализ столкновений, проверка инструмента
- `toolpath.vert` / `toolpath.frag` — шейдеры линий
- Зависимость: `pip install cadquery-ocp==8.0.1.1.0` — импорт STEP (модель и обводка
  граней из B-rep); без него STEP не читается, вьювер использует STL. Версия
  зафиксирована: 7.7.2 не читает STEP вовсе, а `test_normals_golden.py` сверяет
  тесселяцию бит-в-бит и к версии OCP чувствителен
- Запуск: `python modules/gcode-viewer/preview_3d.py "data/cad-testdata/Test_model.ngc"`
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

### 4. Тестовые УП/модели и постпроцессор — `cad-testdata` (`0.5.1`)
- `data/cad-testdata/Test_model.ngc`, `data/cad-testdata/Test_model.STL` — тестовые УП и модель
- `data/cad-testdata/linuxcnc_ui.cps` — постпроцессор с тегами `UI_*` (LinuxCNC)
- `data/cad-testdata/linuxcnc.cps` — базовый постпроцессор

## Быстрый старт

```bash
# React интерфейс
pnpm install
pnpm dev

# 3D-вьювер
python modules/gcode-viewer/preview_3d.py "data/cad-testdata/Test_model.ngc"
```

## Документация

- [`VERSIONING.md`](VERSIONING.md) — версионирование и порядок внесения изменений
- [`projects.json`](projects.json) — манифест проектов и версий
- [`DESIGN.md`](DESIGN.md) / [`DESIGN.en.md`](DESIGN.en.md) — архитектура 3D-вьювера и технический долг
  (русский / английский)
- [`modules/gcode-viewer/CHANGELOG.md`](modules/gcode-viewer/CHANGELOG.md) — журнал вьювера

## Лицензия

Apache License 2.0 — см. [`LICENSE`](LICENSE).
