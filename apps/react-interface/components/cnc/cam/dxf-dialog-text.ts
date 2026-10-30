// The words of the DXF import dialog (PLAN-QUICKPROG, 7.6, «Импорт DXF»): the lines of the contour
// list, the warning of the skipped entities and the reason of a failed read.
//
// The project has no DOM renderer, so the dialog is checked on these pure helpers: the list is a
// row per contour with the number the operator types and the `CAM_DXF` action carries, and the
// reason is the one the reader already wrote, never a second copy. The file text stays out: the
// dialog keeps the reading, not the file.
import type { DxfReading } from '@/lib/cnc/cam/dxf-source'
import type { DxfContour } from '@/lib/cnc/cam/dxf-read'

/** One row of the contour list: the number (one-based, as the operator reads it), points and the close. */
export interface DxfRow {
  /** The zero-based number the `CAM_DXF` action carries. */
  index: number
  /** The number the list shows and the card prints: one-based. */
  number: number
  points: number
  closed: boolean
  /** `3 тч · замкнут` — the row of the list. */
  text: string
}

/** The row of one contour; the same words a contour card prints (`keySize`). */
export function dxfRow(contour: DxfContour, index: number): DxfRow {
  const count = contour.points.length
  return {
    index,
    number: index + 1,
    points: count,
    closed: contour.closed,
    text: `${String(count)} тч · ${contour.closed ? 'замкнут' : 'открыт'}`,
  }
}

/** The rows of a read file in the order the file gives them; an unreadable file has no rows. */
export function dxfRows(reading: DxfReading): DxfRow[] {
  return reading.ok ? reading.contours.map(dxfRow) : []
}

/**
 * The warning above the list: the skipped entities of the reader, or the empty file that gave no
 * contour at all. `null` when the file was read whole and holds a contour.
 */
export function dxfWarning(reading: DxfReading): string | null {
  if (!reading.ok) return reading.warning
  // A file of unsupported entities says what it skipped first: the operator learns which drawing
  // words the reader did not take, not only that the list is empty.
  if (reading.warning !== null) return reading.warning
  if (reading.contours.length === 0) return 'В файле нет контуров: добавьте линию, полилинию, дугу или окружность.'
  return null
}

/** The reason of a failed read; `null` when the file was read and the list can be shown. */
export function dxfError(reading: DxfReading): string | null {
  return reading.ok ? null : reading.reason
}

/** The caption of the dialog: the count of the read contours, or the word about the missing ones. */
export function dxfSummary(reading: DxfReading): string {
  if (!reading.ok) return 'Файл не прочитан'
  const count = reading.contours.length
  const word =
    count % 10 === 1 && count % 100 !== 11
      ? 'контур'
      : count % 10 >= 2 && count % 10 <= 4 && (count % 100 < 12 || count % 100 > 14)
        ? 'контура'
        : 'контуров'
  return `Контуры в файле: ${String(count)} ${word}`
}

/**
 * The caption of the add button: the plain word while nothing is picked or the file holds one
 * contour, the number once the operator chooses between several.
 */
export function dxfAddLabel(row: DxfRow | null, total: number): string {
  return row === null || total <= 1 ? 'Добавить контур' : `Добавить контур ${String(row.number)}`
}

/** What happens after the press: the list closes and the card appears in the document. */
export const DXF_AFTER_ADD = 'Контур встанет в список операций; файл в панели не сохраняется.'

/** The caption under the file input, the same words in the panel and on the touch. */
export const DXF_PICK_HINT = 'Файл ASCII DXF: линия, полилиния, дуга или окружность. Файл не сохраняется.'
