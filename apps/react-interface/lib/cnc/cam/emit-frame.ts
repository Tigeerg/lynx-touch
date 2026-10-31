// The header and the footer of the emitted file (PLAN-QUICKPROG, 7.1, 7.3), the port of `emit_frame.cpp`.
//
// The frame repeats `data/cad-testdata/linuxcnc_ui.cps`: the percent sign, the MACHINE and the
// UI_STOCK tags, the modal block, the work offset, the G64 path tolerance and the parking move
// before the first tool change, then M30 with the percent sign at the end. The block numbers are
// the caller's: the header and the operations share one counter, so a block keeps a readable number
// across the whole file.
import type { QuickProgram } from './ops'
import type { Block } from './emit-state'
import { commentSafe, coord, num, tag } from './emit-words'

/** The generator line: the panel and its version, so a saved file names the program that made it. */
export const GENERATOR = 'lynx-touch react-interface 0.24.0'

/** The work offsets the document may name. */
export const WCS_WORDS = ['G54', 'G55', 'G56', 'G57', 'G58', 'G59'] as const

/** True when `text` is one of G54..G59; a fallback to G54 would cut at the wrong offset. */
export const validWcs = (text: string): boolean => (WCS_WORDS as readonly string[]).includes(text)

/** The header blocks of the file, before the first operation. */
export const headerBlocks = (program: QuickProgram): Block[] => {
  const stock = program.stock
  return [
    { text: tag('MACHINE') },
    { text: tag(`  MODEL: ${commentSafe(program.name)}`) },
    { text: tag(`  GENERATOR: ${GENERATOR}`) },
    { text: tag('UI_ORIGIN: X0. Y0. Z0.') },
    {
      text: tag(
        `UI_STOCK: XMIN=${num(stock.min.x)} XMAX=${num(stock.max.x)} YMIN=${num(stock.min.y)} ` +
          `YMAX=${num(stock.max.y)} ZMIN=${num(stock.min.z)} ZMAX=${num(stock.max.z)}`,
      ),
    },
    { text: 'G21 G90 G94 G17 G40 G49 G80' },
    // A silent fallback to G54 would cut at the wrong offset; the comment names the problem instead.
    ...(validWcs(program.wcs) ? [] : [{ text: tag(`  WCS INVALID: ${commentSafe(program.wcs)} - G54 USED`) }]),
    { text: validWcs(program.wcs) ? program.wcs : 'G54' },
    { text: `G64 P${coord(program.g64P)} Q${coord(program.g64Q)}` },
    // Park above the stock before the first tool change, whatever Z the previous program left.
    { text: `G0 Z${coord(program.safeZ)}` },
  ]
}

/** The footer blocks of the file: the machine home before M30, then M30. */
export const footerBlocks = (): Block[] => [{ text: 'G53 G0 Z0' }, { text: 'M30' }]
