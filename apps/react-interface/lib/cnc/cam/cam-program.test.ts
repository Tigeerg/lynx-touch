// The header of the overlay edited in the document (PLAN-QUICKPROG, 5 steps 1-2): the work offset,
// the stock box, the safe Z and the name. The header is not the form: the edit changes the program,
// the preview follows at once, and the common `validate` answers with the field it talks about.
import { describe, expect, it } from 'vitest'

import { initialState, reducer, type Action } from '../machine'
import type { MachineState } from '../types'
import type { CamState } from './cam'
import { DEFAULT_STOCK, SAFE_Z_CLEARANCE_MM, newOperation, newProgram } from './defaults'
import { stockTop, type Operation, type QuickProgram } from './ops'

const run = (s: MachineState, ...actions: Action[]) => actions.reduce(reducer, s)
const camOf = (s: MachineState): CamState => {
  if (s.cam === null) throw new Error('the overlay is closed')
  return s.cam
}

const FACE: Operation = {
  ...newOperation('face', { id: '', topZ: 0, depthMm: 1, stepDownMm: 1 }),
  tool: { number: 1, diameterMm: 12, hNumber: 1 },
}

/** The overlay with one face operation over the demo stock, named `POLKA`. */
const open = (): MachineState => {
  const program: QuickProgram = { ...newProgram(DEFAULT_STOCK, 'POLKA'), operations: [FACE] }
  const opened = run(initialState, { type: 'CAM_OPEN' })
  return { ...opened, cam: { ...camOf(opened), program } }
}

describe('CAM_PROGRAM: шапка меняет программу, не форму', () => {
  it('смена WCS меняет УП: G55 вместо G54, предпросмотр пересчитан', () => {
    const before = open()
    const after = run(before, { type: 'CAM_PROGRAM', field: 'wcs', value: 'G55' })
    const text = (camOf(after).preview ?? []).join('\n')

    expect(camOf(after).program.wcs).toBe('G55')
    // The offset is a numbered block of the file, as the reference post writes it.
    expect(text).toMatch(/^N\d+ G55$/m)
    expect(text).not.toMatch(/^N\d+ G54$/m)
    // The header is not the form: the form stays closed and no debounce is waiting.
    expect(camOf(after).form).toBeNull()
    expect(camOf(after).pending).toBe(false)
  })

  it('смена заготовки пересчитывает безопасный Z', () => {
    const before = open()
    const after = run(before, { type: 'CAM_PROGRAM', field: 'stock.max.z', value: '10' })
    const stock = camOf(after).program.stock

    expect(stock.max.z).toBe(10)
    expect(camOf(after).program.safeZ).toBe(stockTop(stock) + SAFE_Z_CLEARANCE_MM)
    expect(camOf(after).error).toBeNull()
  })

  it('невалидный WCS — ошибка с полем wcs, документ не портится', () => {
    const before = open()
    const after = run(before, { type: 'CAM_PROGRAM', field: 'wcs', value: 'G60' })

    expect(camOf(after).program.wcs).toBe('G54')
    expect(camOf(after).error?.field).toBe('wcs')
    expect(after.message?.level).toBe('warn')
  })

  it('безопасный Z опущен ниже верха заготовки — ошибка у safeZ', () => {
    // Safe Z is typed under the top of the stock: the common check names the field, not the form.
    const after = run(open(), { type: 'CAM_PROGRAM', field: 'safeZ', value: '-5' })

    expect(camOf(after).error?.field).toBe('safeZ')
    expect(camOf(after).program.safeZ).toBe(-5)
    expect(after.message?.level).toBe('warn')
  })

  it('неизвестное поле шапки — отказ, программа как была', () => {
    const before = open()
    const after = run(before, { type: 'CAM_PROGRAM', field: 'g64P', value: '0.5' })

    expect(after.message?.level).toBe('warn')
    expect(camOf(after).program).toEqual(camOf(before).program)
  })
})
