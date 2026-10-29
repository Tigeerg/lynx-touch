// The ladder scheme manager: the pick list, load and save of a scheme into the PLC, and the new,
// copy, rename and remove commands. The editor's element cases live in ladder-reducer.ts and the
// form cases in ladder-forms.ts; both hand the actions they do not handle here. The data and the
// navigation helpers come from ladder-nav.ts, the new rung and the free address from
// ladder-reducer.ts.
import { clampIndex } from '../clamp'
import { isBusy } from '../guards'
import { formatStamp } from '../time'
import type { Result } from '../subsystem'
import { emptyMem, isLatchKind, coilKind } from './ladder-data'
import { activeScheme, cloneRungs } from './ladder-nav'
import { freeAddr, newRung } from './ladder-reducer'
import type { LadderAction } from './ladder-reducer'
import type { CoilKind, LadderContact, LadderRung, LadderScheme, LadderState, MachineState } from '../types'

const patch = (s: MachineState, p: Partial<LadderState>): MachineState => ({
  ...s,
  ladder: { ...s.ladder, ...p },
})

export function uniqueName(schemes: LadderScheme[], base: string) {
  const names = new Set(schemes.map((x) => x.name))
  if (!names.has(base)) return base
  for (let i = 2; ; i++) if (!names.has(`${base}_${i}`)) return `${base}_${i}`
}

const replaceAt = <T>(list: T[], index: number, item: T) => list.map((x, i) => (i === index ? item : x))

const DIRTY_BLOCK = 'Сначала сохраните (F7) или отмените правку (Esc)'

export function ladderSchemeReducer(s: MachineState, a: LadderAction): Result {
  const L = s.ladder

  switch (a.type) {
    case 'LADDER_MANAGER':
      return { s: patch(s, { manager: a.open, pick: L.active, form: null }) }

    case 'LADDER_PICK_MOVE':
      return { s: patch(s, { pick: clampIndex(L.pick, a.delta, L.schemes.length) }) }

    case 'LADDER_LOAD': {
      if (L.dirty) return { s, msg: DIRTY_BLOCK, level: 'warn' }
      if (isBusy(s)) return { s, msg: 'Загрузка схемы в ПЛК во время цикла запрещена', level: 'warn' }
      const scheme = L.schemes[L.pick]
      if (!scheme) return { s, msg: 'Схема не выбрана', level: 'warn' }
      return {
        s: {
          ...patch(s, {
            active: L.pick,
            draft: L.draft ? cloneRungs(scheme.rungs) : null,
            manager: false,
            col: 0,
            path: 0,
            mem: emptyMem(),
          }),
          cursor: { ...s.cursor, service: 0 },
        },
        msg: `Схема ${scheme.name}.lad загружена в ПЛК`,
      }
    }

    case 'LADDER_NEW': {
      if (L.dirty) return { s, msg: DIRTY_BLOCK, level: 'warn' }
      if (isBusy(s)) return { s, msg: 'Остановите программу', level: 'warn' }
      const rungs = [newRung([], 1)]
      const scheme: LadderScheme = {
        name: uniqueName(L.schemes, 'plc_new'),
        title: 'Новая схема',
        rungs,
        modified: a.at === undefined ? '' : formatStamp(a.at),
      }
      const schemes = [...L.schemes, scheme]
      const index = schemes.length - 1
      return {
        s: {
          ...patch(s, {
            schemes,
            active: index,
            pick: index,
            draft: cloneRungs(rungs),
            dirty: false,
            manager: false,
            col: 0,
            mem: emptyMem(),
          }),
          cursor: { ...s.cursor, service: 0 },
        },
        msg: `Создана схема ${scheme.name}.lad — она активна в ПЛК`,
        level: 'warn',
      }
    }

    case 'LADDER_COPY': {
      const src = L.schemes[L.pick]
      if (!src) return { s, msg: 'Схема не выбрана', level: 'warn' }
      const copy: LadderScheme = {
        ...src,
        name: uniqueName(L.schemes, `${src.name}_copy`),
        title: `${src.title} (копия)`,
        rungs: cloneRungs(src.rungs),
        modified: a.at === undefined ? '' : formatStamp(a.at),
      }
      const at = L.pick + 1
      const schemes = [...L.schemes.slice(0, at), copy, ...L.schemes.slice(at)]
      return {
        s: patch(s, { schemes, pick: at, active: L.active >= at ? L.active + 1 : L.active }),
        msg: `Создана копия ${copy.name}.lad`,
      }
    }

    case 'LADDER_RENAME': {
      const target = L.schemes[L.pick]
      if (!target) return { s, msg: 'Схема не выбрана', level: 'warn' }
      return { s: patch(s, { form: { kind: 'scheme', target: L.pick, values: [target.name, target.title] } }) }
    }

    case 'LADDER_REMOVE': {
      if (L.schemes.length === 1) return { s, msg: 'Нельзя удалить единственную схему', level: 'warn' }
      if (L.pick === L.active) return { s, msg: 'Схема активна в ПЛК — сначала загрузите другую', level: 'warn' }
      const removed = L.schemes[L.pick]
      const schemes = L.schemes.filter((_, i) => i !== L.pick)
      return {
        s: patch(s, {
          schemes,
          active: L.active > L.pick ? L.active - 1 : L.active,
          pick: Math.min(L.pick, schemes.length - 1),
        }),
        msg: `Схема ${removed.name}.lad удалена`,
        level: 'warn',
      }
    }

    case 'LADDER_SAVE': {
      if (!L.draft) return { s }
      if (isBusy(s)) return { s, msg: 'Запись в ПЛК во время цикла запрещена', level: 'error' }
      const seen = new Map<string, { row: number; kind: CoilKind }>()
      for (const [i, r] of L.draft.entries()) {
        const kind = coilKind(r.coil)
        const prev = seen.get(r.coil.addr)
        if (prev && !(isLatchKind(prev.kind) && isLatchKind(kind))) {
          return {
            s: { ...s, cursor: { ...s.cursor, service: i } },
            msg: `Катушка ${r.coil.addr} задана дважды (ступени ${prev.row + 1} и ${i + 1})`,
            level: 'error',
          }
        }
        seen.set(r.coil.addr, { row: i, kind })
      }
      const scheme = activeScheme(s)
      const schemes = replaceAt(L.schemes, L.active, {
        ...scheme,
        rungs: L.draft,
        modified: a.at === undefined ? scheme.modified : formatStamp(a.at),
      })
      return {
        s: patch(s, { schemes, draft: null, dirty: false, form: null, manager: false, col: 0, mem: emptyMem() }),
        msg: `Схема ${scheme.name}.lad сохранена и загружена в ПЛК`,
      }
    }
    default:
      return { s }
  }
}
