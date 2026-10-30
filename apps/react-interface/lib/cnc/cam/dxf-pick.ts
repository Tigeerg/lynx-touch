// The `CAM_DXF` action (PLAN-QUICKPROG, 7.6, «Импорт DXF»): the picked contour into the document.
//
// The action carries the text of the file and the number of the contour; this module reads the file
// once and hands back the reading. The text lives only in the action: the state keeps the
// operation, never the file. A file without a supported entity and a number outside the list are
// refusals with a reason the operator reads, and the document stays as it was.
import type { ContourOperation, ProgramShape } from './ops'
import { readDxf } from './dxf-source'
import { dxfOperation } from './dxf-contour'

/** The operation of the picked contour, or the reason it cannot be added. */
export type DxfPicked = { ok: true; op: ContourOperation; warning: string | null } | { ok: false; reason: string }

/**
 * The operation of contour `index` of the file text; the plane of the operation is the top of the
 * stock of `program`. The number is zero-based, as the list of the overlay is.
 */
export function pickDxfContour(text: string, index: number, program: ProgramShape): DxfPicked {
  const reading = readDxf(text)
  if (!reading.ok) return { ok: false, reason: reading.reason }
  const contour = reading.contours[index]
  if (contour === undefined) {
    return { ok: false, reason: `В файле DXF нет контура ${String(index + 1)}: их ${String(reading.contours.length)}.` }
  }
  const op = dxfOperation(contour, program)
  return op === null
    ? { ok: false, reason: `Контур ${String(index + 1)} из DXF короче двух точек.` }
    : { ok: true, op, warning: reading.warning }
}
