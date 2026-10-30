// The G-code emitter: document + plan -> file lines (PLAN-QUICKPROG, 7.1, 7.3, 7.4), C2.
//
// The emitter reads the document and the planned path and returns the whole file as a list of
// lines. It never recomputes a coordinate: a number in the file is a number of a move. The block
// order is the reference post's and is proven by the golden files in `etl/`:
//
//   %  (MACHINE) (UI_STOCK)                      header tags
//   N10 G21 G90 G94 G17 G40 G49 G80              modals
//   G54, G64 P.. Q.., G0 Z<safe>                 offset, tolerance, park
//   M1                                           from the second operation, when on
//   (UI_OP) (UI_STAGE) (UI_TOOL)                 tags of the operation
//   G53 G0 Z0 / T.. M6 / G43 Z<safe> H<n>        park, change, length compensation
//   S.. M3 / M8                                  spindle, coolant
//   G0 / G1 / G3 X.. Y.. Z.. F..                 moves, or the G81/G83 cycle of a drilling
//   M9 / M5 / G0 Z<safe>                         end of the operation
//   G53 G0 Z0 / M30 / %                          footer
//
// The file is a list of lines for `setLines`, not a file: `CAM_SAVE` writes it through `files.ts`.
import type { CamMove } from './moves'
import type { Point3, QuickProgram } from './ops'
import { operationUuid, type ProgramPath } from './path'
import { drillPlan } from './path-drill'
import type { Block, EmitState } from './emit-state'
import { freshState, preambleBlocks, trailerBlocks } from './emit-state'
import { footerBlocks, headerBlocks } from './emit-frame'
import { cycleBlocks, moveBlocks } from './emit-moves'
import { coord, effectiveH, numberedBlock, SEQUENCE_STEP } from './emit-words'

/** The first block number of the file, the reference post's start. */
const FIRST_SEQUENCE = 10

/**
 * The park point the header leaves the tool at: safe Z, XY unknown. A NaN never equals anything,
 * so the virtual point never swallows the first real X or Y of an operation.
 */
const parkPoint = (safeZ: number): Point3 => ({ x: Number.NaN, y: Number.NaN, z: safeZ })

/** The blocks of one operation: its preamble, its moves or cycle, its trailer. */
const operationBlocks = (program: QuickProgram, path: ProgramPath, index: number, state: EmitState): Block[] => {
  const spans = path.operations
  const span = spans[index]
  const operation = program.operations[span.opIndex]
  const last = index + 1 < spans.length ? spans[index + 1].first : path.moves.length
  const blocks = [
    ...preambleBlocks(
      operation,
      operationUuid(path.uuid, index),
      effectiveH(operation),
      program.safeZ,
      program.stopAfterOp,
      state,
    ),
  ]
  const park = parkPoint(program.safeZ)
  let parked = park
  if (operation.kind === 'drill') {
    // A cycle is a machine feature, not a path: the holes come from the same layout the planner drew.
    const plan = drillPlan(program, operation)
    if (plan.points.length > 0) {
      blocks.push({ text: `G0 Z${coord(plan.cycle.rZ)}` })
      blocks.push(
        ...cycleBlocks(
          plan.points.map((hole) => ({ ...hole, z: plan.cycle.rZ })),
          plan.cycle,
          state,
        ),
      )
    }
    // G80 brings the drill back to the R plane, not to safe Z; the trailer parks it.
    parked = { ...park, z: plan.cycle.rZ }
  } else {
    const moves: CamMove[] = path.moves.slice(span.first, last)
    blocks.push(...moveBlocks(moves, park, state))
    parked = moves.length > 0 ? moves[moves.length - 1].to : park
  }
  blocks.push(...trailerBlocks(operation.coolant, program.safeZ, parked.z, state))
  return blocks
}

/** The whole file as lines: the percent sign, the header, every operation, the footer. */
export function emitProgram(program: QuickProgram, path: ProgramPath): string[] {
  const state = freshState()
  const blocks: Block[] = [...headerBlocks(program)]
  path.operations.forEach((_, index) => blocks.push(...operationBlocks(program, path, index, state)))
  blocks.push(...footerBlocks())
  let sequence = FIRST_SEQUENCE
  const lines = ['%']
  for (const block of blocks) {
    // Comments and the tags of the frame are not numbered blocks, as in the reference post.
    if (block.text.startsWith('(')) {
      lines.push(block.text)
      continue
    }
    lines.push(numberedBlock(sequence, block.text))
    sequence += SEQUENCE_STEP
  }
  lines.push('%')
  return lines
}
