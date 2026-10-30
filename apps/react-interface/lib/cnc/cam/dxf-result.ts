// The result of the `CAM_DXF` action in the reducer (PLAN-QUICKPROG, 7.6, «Импорт DXF»).
//
// The reducer keeps the switch of the actions; the read of the file and the operation of the picked
// contour stand in `dxf-pick.ts`, and this module turns their answer into the state and the message.
// The file is not kept: the text is read once, and only the `contour` operation reaches the
// document. A refusal is a warning and the document stays as it was.
import { emitOf } from './cam-save'
import { pickDxfContour } from './dxf-pick'
import type { CamState } from './types'
import type { Result } from '../subsystem'
import type { MachineState } from '../types'

/** The state with the picked contour appended, or the refusal as a warning. */
export function dxfResult(s: MachineState, cam: CamState, text: string, contour: number): Result {
  const picked = pickDxfContour(text, contour, cam.program)
  if (!picked.ok) return { s, msg: picked.reason, level: 'warn' }
  const program = { ...cam.program, operations: [...cam.program.operations, picked.op] }
  const msg = `Контур из DXF добавлен: ${String(picked.op.points.length)} точек`
  return {
    // Not typing: the preview of the document is recomputed here, as `CAM_ACCEPT` does.
    s: { ...s, cam: { ...cam, program, preview: emitOf(program), error: null, pending: false } },
    msg: picked.warning === null ? msg : `${msg}. ${picked.warning}`,
  }
}
