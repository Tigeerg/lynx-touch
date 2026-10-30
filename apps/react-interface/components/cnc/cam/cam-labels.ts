// The words and the small numbers of the quick-program overlay (PLAN-QUICKPROG, 7.4, C5).
//
// The captions of the kinds and the one line a card or a field shows are pure functions: the
// overlay and its tests read the same words, and a second copy of a caption has no place to hide.
// The form field tables stay in `cam-fields.ts`.
import type { Operation, OperationKind, QuickProgram } from '@/lib/cnc/cam/ops'
import { probeWarnings } from '@/lib/cnc/cam/probe'
import { slotWarnings } from '@/lib/cnc/cam/validate-slot'
import { tapWarnings } from '@/lib/cnc/cam/validate-tap'
import { counterboreWarnings } from '@/lib/cnc/cam/validate-counterbore'
import { pathWarnings } from '@/lib/cnc/cam/validate-paths'
import { restWarnings } from '@/lib/cnc/cam/validate-rest'

/** The kind names of the picker and of an operation card; P1 requires the word, not the icon. */
export const KIND_LABEL: Record<OperationKind, string> = {
  face: 'Плоскость',
  pocket: 'Карман',
  'circle-pocket': 'Круглый карман',
  drill: 'Отверстия',
  contour: 'Контур',
  slot: 'Паз',
  chamfer: 'Фаска',
  tap: 'Метчик',
  counterbore: 'Цековка',
  thread: 'Резьбовая фреза',
  engrave: 'Гравировка',
  'pocket-island': 'Карман с островом',
  rest: 'Остаточная обработка',
  custom: 'Custom G Code',
  probe: 'Блок щупа',
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
    case 'slot':
      return `шир. ${formatMm(op.widthMm)}`
    case 'chamfer':
      return `фаска ${formatMm(op.chamferMm)}`
    case 'tap': {
      const diameter = `Ø${formatMm(op.hole.r * 2)}`
      const count = op.pattern === 'grid' ? op.cols * op.rows : op.pattern === 'circle' ? op.count : 1
      return `${diameter} ×${String(count)} · шаг ${formatMm(op.pitchMm)}`
    }
    case 'counterbore': {
      const diameter = `Ø${formatMm(op.hole.r * 2)}`
      const count = op.pattern === 'grid' ? op.cols * op.rows : op.pattern === 'circle' ? op.count : 1
      return `${diameter} ×${String(count)} · гнездо Ø${formatMm(op.seatDiameterMm)}`
    }
    case 'thread': {
      const side = op.internal ? 'внутр.' : 'наруж.'
      return `Ø${formatMm(op.hole.r * 2)} · шаг ${formatMm(op.pitchMm)} · ${side}`
    }
    case 'engrave':
      return `«${op.text}» ${formatMm(op.heightMm)} мм`
    case 'pocket-island':
      return `${formatMm(op.rect.w)}×${formatMm(op.rect.h)} · остров ${formatMm(op.island.w)}×${formatMm(op.island.h)}`
    case 'rest': {
      const area = op.rect.w > 0 && op.rect.h > 0 ? `${formatMm(op.rect.w)}×${formatMm(op.rect.h)}` : 'вся площадь'
      const sources = op.source.length === 0 ? 'нет исходных' : `из ${op.source.map((i) => String(i + 1)).join(', ')}`
      return `${area} · ${sources}`
    }
    case 'custom':
      return `${String(op.gcode.length)} стр. · проверьте вручную`
    case 'probe': {
      const count = op.points.length === 1 ? '1 точка' : `${String(op.points.length)} точек`
      return `${op.axis} · ${count} · ${formatMm(op.rangeMm)} мм`
    }
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

/**
 * The warnings of a kind that do not block the save (PLAN-QUICKPROG, 7.6): a slot wider than two
 * diameters, a pitch off the standard series, a probe touching faster than it cuts. The card shows
 * the count as a sign, the reason stays in the module that owns the rule; a tap without rigid
 * tapping is an error of the save and never lands here.
 */
export function kindWarnings(op: Operation, program?: QuickProgram): string[] {
  switch (op.kind) {
    case 'tap':
      return tapWarnings(op).map((w) => w.reason)
    case 'counterbore':
      return counterboreWarnings(op).map((w) => w.reason)
    case 'slot':
      return slotWarnings(op).map((w) => w.reason)
    case 'probe':
      return probeWarnings(op).map((w) => w.reason)
    case 'engrave':
    case 'pocket-island':
      return pathWarnings(op).map((w) => w.reason)
    case 'rest':
      // The empty source and the empty rest are read against the document; a card without the
      // document keeps the sources it can judge on its own (none) and shows no warning.
      return program === undefined ? [] : restWarnings(program, op).map((w) => w.reason)
    default:
      return []
  }
}

/** The word on a warning sign: one line in the card, the reasons on hover. */
export const warningText = (reasons: string[]): string => `Проверьте: ${reasons.join(' ')}`
