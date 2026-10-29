// The Qt port mirrors the React panel word for word (CLAUDE.md, section 5: the Qt port is checked
// against the original, not translated again). The labels both ports show for the same operation
// are compared here, so one side moving alone fails in the test instead of on the operator's
// screen. The operation names that were labelled two ways inside softkeys.ts live in data.ts
// (SOFTKEY_LABEL); this file pins the Qt side to the same strings.
import { readFileSync, readdirSync } from 'node:fs'
import { describe, expect, it } from 'vitest'
import { SOFTKEY_LABEL } from './data'

const qml = readFileSync(new URL('../../../qt-interface/main.qml', import.meta.url), 'utf8')

/**
 * The softkey labels live in a facade plus its modules (softkeys.ts re-exports ./softkeys/*), so
 * the check reads the whole set of sources, not one file: a label is found wherever it moved.
 */
const softkeysSources = (): string[] => {
  const dir = new URL('./softkeys/', import.meta.url)
  const own = readFileSync(new URL('./softkeys.ts', import.meta.url), 'utf8')
  const modules = readdirSync(dir, { withFileTypes: true })
    .sort((a, b) => a.name.localeCompare(b.name))
    .flatMap((entry) =>
      entry.isDirectory()
        ? readdirSync(new URL(`${entry.name}/`, dir))
            .filter((file) => file.endsWith('.ts'))
            .sort()
            .map((file) => readFileSync(new URL(`${entry.name}/${file}`, dir), 'utf8'))
        : entry.name.endsWith('.ts')
          ? [readFileSync(new URL(entry.name, dir), 'utf8')]
          : [],
    )
  return [own, ...modules]
}

const softkeys = softkeysSources().join('\n')

/** The operations both ports name today: the Qt softkey id and the shared label. */
const SHARED: [string, string][] = [
  ['act', 'Активировать'],
  ['teachX', 'Привязка X'],
  ['teachY', 'Привязка Y'],
  ['teachZ', 'Привязка Z'],
  ['call', 'Вызвать в шпиндель'],
  ['measure', 'Замер длины'],
  ['resetLife', 'Сброс ресурса'],
  ['ack', 'Квитировать'],
  ['alarmClearAll', 'Удалить квитированные'],
  ['svcBack', 'Назад'],
  ['svcSave', 'Сохранить'],
  ['svcDefaults', 'По умолчанию'],
  ['ladderEdit', 'Правка схемы'],
  ['toCursor', 'Старт с кадра'],
  ['pBack', 'Назад'],
  ['pNext', 'Далее'],
  ['pTool', 'Щуп T99'],
  ['pRetry', 'Повторить'],
  ['pClose', 'Закрыть щуп'],
]

describe('Qt-порт: подписи совпадают с React', () => {
  it.each(SHARED)('main.qml: %s = «%s»', (id, label) => {
    expect(qml, `main.qml:${id}`).toContain(`id: "${id}", label: "${label}"`)
    expect(softkeys, `softkeys.ts:${label}`).toContain(`'${label}'`)
  })

  it('подписи операций — из одного источника, а не копии', () => {
    for (const op of Object.keys(SOFTKEY_LABEL) as (keyof typeof SOFTKEY_LABEL)[]) {
      expect(softkeys, `softkeys.ts:${op}`).toContain(`SOFTKEY_LABEL.${op}`)
    }
    expect(qml, 'main.qml:probe').toContain(`id: "probe", label: "${SOFTKEY_LABEL.PROBE_OPEN}"`)
  })
})
