// A DXF contour as one `contour` operation (PLAN-QUICKPROG, 7.6, «Импорт DXF»).
//
// The file is not kept: the reader (`dxf-read.ts`) turns it into contours, the operator picks one,
// and this module makes the operation of it. The contour carries the mill of the document defaults
// and the depth of the console; the allowance and the side are zero and `material`, as a profile cut
// to size from the outside. The points come in the coordinates of the DXF, Z 0: the plane of the
// drawing is the plane of the top, as the contour form expects it.
import { dxfContours, type DxfContour } from './dxf-read'
import { newOperation } from './defaults'
import type { ContourOperation, Point3, ProgramShape } from './ops'
import { stockTop } from './ops'

/** The id of the operation of an imported contour: `DXF: <points>`. */
export const dxfId = (contour: DxfContour): string => `DXF: ${String(contour.points.length)} т.`

/**
 * The `contour` operation of a contour, or `null` when it has fewer than two points: a single point
 * is not a profile and the contour rules would refuse it anyway.
 */
export function dxfOperation(contour: DxfContour, program: ProgramShape): ContourOperation | null {
  if (contour.points.length < 2) return null
  const topZ = stockTop(program.stock)
  const points: Point3[] = contour.points.map((point) => ({ x: point.x, y: point.y, z: topZ }))
  return {
    ...newOperation('contour', { id: dxfId(contour), topZ }),
    points,
    closed: contour.closed,
    allowanceMm: 0,
    side: 'material',
  }
}
