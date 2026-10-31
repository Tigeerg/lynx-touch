// The material of an operation through an edit (PLAN-QUICKPROG, 7.4, Q6).
//
// The grey box of the form names the stock material (aluminium or steel); the cut is rated by it
// (`feeds.ts`). The attribute must survive the round trip through the file AND the edit of the
// operation: a form that drops it writes the default aluminium line, and the next start rates the
// cut for the wrong material. This case holds that chain in one place.
import { describe, expect, it } from 'vitest'

import { initialState, reducer, type Action } from '../machine'
import type { MachineState } from '../types'
import { newOperation, newProgram } from './defaults'
import type { QuickProgram } from './ops'
import { parseProgram, serializeProgram } from './serialize'

const STOCK = { min: { x: -50, y: -40, z: -20 }, max: { x: 50, y: 40, z: 0 } }

const tool = (number: number, diameterMm: number, hNumber = number) => ({ number, diameterMm, hNumber })

const cutting = (spindleRpm: number, feedMmMin: number, plungeFeedMmMin: number) => ({
  spindleRpm,
  feedMmMin,
  plungeFeedMmMin,
})

/** A document of one steel pocket of the demo stock. */
const steelPocket = (): QuickProgram => ({
  ...newProgram(STOCK, 'STEEL'),
  operations: [
    {
      ...newOperation('pocket', {
        id: 'Карман',
        topZ: 0,
        depthMm: 4,
        stepDownMm: 2,
        tool: tool(1, 8),
        material: 'steel',
      }),
      cutting: cutting(4000, 500, 150),
      rect: { x: -30, y: -20, w: 60, h: 40 },
    },
  ],
})

const run = (s: MachineState, ...actions: Action[]) => actions.reduce(reducer, s)

describe('материал операции переживает правку (PLAN-QUICKPROG 7.4, Q6)', () => {
  it('пишет `op.material steel` и читает обратно `steel`', () => {
    const text = serializeProgram(steelPocket())
    expect(text).toContain('op.material steel\n')
    const parsed = parseProgram(text)
    if (!parsed.ok) throw new Error(`${String(parsed.line)}: ${parsed.reason}`)
    expect(parsed.program.operations[0].material).toBe('steel')
  })

  it('сохраняет `op.material steel` после CAM_EDIT, CAM_ACCEPT и сохранения', () => {
    // The operator opens the steel pocket for edit, changes the step over and accepts: the form
    // carries `material` (`valuesOf`) and the accept must keep it in the operation, so the `.qprog`
    // written next to the `.ngc` still names steel.
    const opened = reducer(initialState, { type: 'CAM_OPEN' })
    const base: MachineState = { ...opened, cam: { ...opened.cam!, program: steelPocket() } }
    const edited = run(
      base,
      { type: 'CAM_EDIT', index: 0 },
      { type: 'CAM_PARAM', field: 'stepOverPct', value: '50' },
      { type: 'CAM_ACCEPT' },
    )
    expect(edited.message?.level).not.toBe('warn')
    expect(edited.cam?.program.operations[0].material).toBe('steel')

    const saved = run(edited, { type: 'CAM_SAVE', at: 1_000 })
    expect(saved.message?.level).toBe('info')
    const model = saved.files.find((file) => file.name === 'STEEL-1.qprog')
    if (model === undefined) throw new Error('no STEEL-1.qprog')
    expect(model.lines).toContain('op.material steel')
    // The model round-trips: reading it back keeps steel on the edited operation.
    const parsed = parseProgram(model.lines.join('\n'))
    if (!parsed.ok) throw new Error(`${String(parsed.line)}: ${parsed.reason}`)
    expect(parsed.program.operations[0].material).toBe('steel')
  })
})
