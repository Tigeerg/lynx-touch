// The editable cells of the overlay header (PLAN-QUICKPROG, 5 steps 1-2).
//
// The offset, the stock box, the safe Z and the name are cells of the document, not of the form:
// a cell names the `CAM_PROGRAM` pair the same way the input and the reducer do, so the table here
// is the one place a new cell is added and the header does not repeat a field name.
import type { QuickProgram } from '@/lib/cnc/cam/ops'
import { formatMm } from './cam-labels'

/** One cell of the header: the program field, its caption and the text the operator sees. */
export interface HeaderCell {
  field: string
  label: string
  value: string
}

/** The stock corners and the frame of the header, in the order the operator reads them. */
export const headerCells = (program: QuickProgram): HeaderCell[] => {
  const { min, max } = program.stock
  return [
    { field: 'wcs', label: 'Смещение', value: program.wcs },
    { field: 'stock.min.x', label: 'X min', value: formatMm(min.x) },
    { field: 'stock.max.x', label: 'X max', value: formatMm(max.x) },
    { field: 'stock.min.y', label: 'Y min', value: formatMm(min.y) },
    { field: 'stock.max.y', label: 'Y max', value: formatMm(max.y) },
    { field: 'stock.min.z', label: 'Z min', value: formatMm(min.z) },
    { field: 'stock.max.z', label: 'Z max', value: formatMm(max.z) },
    { field: 'safeZ', label: 'Безопасный Z', value: formatMm(program.safeZ) },
    { field: 'name', label: 'Имя', value: program.name },
  ]
}
