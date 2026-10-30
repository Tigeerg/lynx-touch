// The flow of the imported contours (PLAN-QUICKPROG, 7.6, «Импорт DXF»).
//
//   data   -> contours  `dxfContours`, the reader of `dxf-read.ts` (the entities of `ENTITIES`)
//   data   -> record    `readDxf`, the whole file as the operator sees it: the contours and the
//                       unsupported entities of the warning at once
//   contour -> operation `dxfOperation`, the `contour` operation of one picked contour
//
// The file is not kept anywhere: the action carries the text once, and the document holds the
// operation only.
import type { DxfContour, DxfResult } from './dxf-read'
import { dxfContours } from './dxf-read'

export type { DxfContour, DxfPoint, DxfResult } from './dxf-read'
export { CHORD_TOL_MM, chordCount, dxfContours } from './dxf-read'

/** The file as the overlay shows it: the contours or the reason, and the skipped entity types. */
export type DxfReading =
  { ok: true; contours: DxfContour[]; warning: string | null } | { ok: false; reason: string; warning: string | null }

/** True for a text that looks like a DXF at all: it has the section word. */
export const looksLikeDxf = (text: string): boolean => /^\s*SECTION\s*$/m.test(text) || /ENTITIES/.test(text)

/** The warning of the unsupported entities, or `null` when every entity was read. */
export const skippedWarning = (skipped: string[]): string | null =>
  skipped.length === 0 ? null : `Пропущены сущности DXF: ${[...new Set(skipped)].join(', ')}.`

/** The contours of the file and the warning of the unsupported entities. */
export function readDxf(text: string): DxfReading {
  const result: DxfResult = dxfContours(text)
  const warning = skippedWarning(result.skipped)
  return result.ok ? { ok: true, contours: result.contours, warning } : { ok: false, reason: result.reason, warning }
}
