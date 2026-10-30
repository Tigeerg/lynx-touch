# Эталоны для ручной проверки вьювером

Здесь лежат два `.ngc`, которые **сгенерировала панель** (её эмиттер из `lib/cnc/cam/`), а не демо
`apps/quickprog`. Они нужны для ручного шага приёмки Q3 (PLAN-QUICKPROG, §7.5, §10): сохранённую УП
надо открыть 3D-вьювером и убедиться, что она читается — сегменты, этапы, подписи `UI_OP`/`UI_TOOL`.

| Файл | Что в нём |
|---|---|
| `quick-mixed.ngc` | три операции: черновая плоскость (фреза 12), карман (фреза 8), сетка отверстий `G83 Q2` (сверло 5) |
| `quick-circles.ngc` | круглый карман дугами `G3` (фреза 8) и круг отверстий `G81` (сверло 5) |

Оба набора совпадают с эталонами `lib/cnc/cam/etl/mixed.ngc` и собираются из документов `etl-cases.ts`
(торцевание + карман + сверление) и документа кругового случая; числа те же, что в демо, поэтому
геометрия — эталонная для панели.

## Как сгенерировать заново

1. Собрать бандл скрипта через `esbuild`, который уже стоит в зависимостях интерфейса, и запустить
   `node` (обёртка в `node_modules/.bin/esbuild` сама требует `node` в `PATH`):

   ```
   export PATH=/tmp/opencode/node22/bin:$PATH     # любой node 20+
   cd apps/react-interface
   P=node_modules/.pnpm/esbuild@0.21.5/node_modules/esbuild/bin/esbuild
   $P /tmp/gen-viewer-fixtures.ts --bundle --platform=node --format=cjs \
      --outfile=/tmp/gen-viewer-fixtures.cjs --log-level=warning
   node /tmp/gen-viewer-fixtures.cjs
   ```

2. Содержимое `/tmp/gen-viewer-fixtures.ts` — только вызовы панельного кода:

   ```ts
   import { writeFileSync } from 'node:fs'
   import { ETL_CASES } from '.../lib/cnc/cam/etl-cases'
   import { buildProgram } from '.../lib/cnc/cam/path'
   import { emitProgram } from '.../lib/cnc/cam/emit'
   import { newOperation, newProgram } from '.../lib/cnc/cam/defaults'

   const out = '.../testdata/viewer'
   const dump = (name, program) => {
     const built = buildProgram(program)
     if (!built.ok) throw new Error(name + ': ' + built.problems.join('; '))
     writeFileSync(`${out}/${name}.ngc`, emitProgram(program, built.path).join('\n') + '\n')
   }
   dump('quick-mixed', ETL_CASES.find((c) => c.name === 'mixed')!.program)
   // quick-circles: круглый карман + круг отверстий, см. документ ниже
   ```

   Документ `quick-circles` — тот же, что в `etl-cases.ts` (`circleCase` + `ringCase`): заготовка
   `100x80x20`, безопасная Z 5, `G54`; круглый карман `x10 y5 r15`, глубина 4, шаг 1.5, фреза 8;
   круг отверстий `x5 y5 r20`, 6 отверстий от 0°, глубина 5, сверло 5 (твёрдый сплав).

3. Проверить, что предпросмотр не пуст и геометрия сходится с эталоном демо: тест
   `lib/cnc/cam/pipeline.test.ts` сравнивает эмиттер панели с `etl/*.ngc` слово в слово.

## Парсер вьювера

Панель дополнительно проверяет УП правилами парсера вьювера без вызова C++ —
`viewerIssues()` в `lib/cnc/cam/viewer-check.ts`, тесты — `viewer-check.test.ts`. Она не повторяет
геометрию, а ловит то, что вьювер прочитает иначе: дуга без конечной точки, цикл без `R`, `Q` у
`G81`, операция без `UI_OP`/`UI_TOOL`. Тест `viewer-check.test.ts` требует, чтобы все `etl/*.ngc`
проходили без ошибок; единственное предупреждение у них и у файлов отсюда — `park-as-move`:
`G53` не является машинным ходом для парсера, и финальная парковка `G53 G0 Z0` читается как
перемещение в ноль.

## Как проверить вьювером

Собрать вьювер (`scripts/build-viewer.ps1` на Windows, CMake-цель `gcodeviewer` на Linux), затем из
каталога сборки:

```
gcodeviewer --smoke <путь>/testdata/viewer/quick-mixed.ngc
QT_QPA_PLATFORM=offscreen gcodeviewer <путь>/testdata/viewer/quick-circles.ngc
```

Чего ждать:

* `--smoke` — `RESULT: OK` (разбор файла без загрузки модели). Код возврата 0.
* обычный offscreen-прогон печатает `loaded OK`, число `segments` и этапы; предупреждений
  `[QML warning]` нет, код возврата 0.

Эталоны сняты на этой машине пробником, который зовёт тот же `gv::parseGcode`, что и вьювер
(`/tmp/opencode/ngcinfo/ngcinfo`, C++ из `modules/gcode-viewer/cpp/core`):

| Файл | segments | этапы (подпись · инструмент) | последняя точка |
|---|---|---|---|
| `quick-mixed.ngc` | 169 | «Черновая плоскость · T1 фреза D12», «Карман · T2 фреза D8», «Сетка · T3 сверло D5» | 20, 10, 0 |
| `quick-circles.ngc` | 867 | «Круглый карман · T2 фреза D8», «Круг отверстий · T7 сверло D5» | 15, 22.321, 0 |

Последняя точка — в нуле по Z: это и есть предупреждение `park-as-move` (парковка читается ходом).
Если вьювер на стенде покажет `RESULT: FAILED` или этапы без имён — это дефект вьювера или
эмиттера, его результат пишется в приёмку Q3 вместе с командой и выводом.
