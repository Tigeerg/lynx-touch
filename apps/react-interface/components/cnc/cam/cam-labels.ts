// The words and the small numbers of the quick-program overlay (PLAN-QUICKPROG, 7.4, C5).
//
// The captions of the kinds and the one line a card or a field shows are pure functions: the
// overlay and its tests read the same words, and a second copy of a caption has no place to hide.
// The form field tables stay in `cam-fields.ts`.
import type { Operation, OperationKind, QuickProgram } from '@/lib/cnc/cam/ops'

/** The kind names of the picker and of an operation card; P1 requires the word, not the icon. */
export const KIND_LABEL: Record<OperationKind, string> = {
  face: 'Плоскость',
  pocket: 'Карман',
  'circle-pocket': 'Круглый карман',
  drill: 'Отверстия',
  contour: 'Контур',
}

/** A tool as the operator reads it in the table: `T01 ф10`. */
export const toolText = (op: Operation): string =>
  `T${String(op.tool.number).padStart(2, '0')} ф${formatMm(op.tool.diameterMm)}`

/** Millimetres without trailing zeros: `10`, `2.5`. */
export const formatMm = (value: number): string => String(Math.round(value * 100) / 100)

/** The size the card is recognised by: the face plane, the pocket sides, the hole pattern. */
export function keySize(op: Operation): string {
  switch (op.kind) {
    case 'face':
      return `Z ${formatMm(op.z)}`
    case 'pocket':
      return `${formatMm(op.rect.w)}×${formatMm(op.rect.h)}`
    case 'circle-pocket':
      return `Ø${formatMm(op.circle.r * 2)}`
    case 'contour':
      return `${String(op.points.length)} тч · ${op.closed ? 'замкнут' : 'открыт'}`
    case 'drill': {
      const diameter = `Ø${formatMm(op.hole.r * 2)}`
      const count = op.pattern === 'grid' ? op.cols * op.rows : op.pattern === 'circle' ? op.count : 1
      return `${diameter} ×${String(count)}`
    }
  }
}

/** The header of the overlay: the work offset, the smoothing tolerance and the stock size. */
export function headerText(program: QuickProgram): { wcs: string; g64: string; stock: string } {
  const { min, max } = program.stock
  const size = (a: number, b: number) => formatMm(Math.abs(b - a))
  const topZ = Math.max(min.z, max.z)
  return {
    wcs: program.wcs,
    g64: `G64 P${formatMm(program.g64P)} Q${formatMm(program.g64Q)}`,
    stock: `${size(min.x, max.x)}×${size(min.y, max.y)}×${size(min.z, max.z)} · Z ${formatMm(topZ)}`,
  }
}
