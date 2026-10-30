// The machine state between blocks: the rule of the tool change and of the modal words (7.1, 7.3).
//
// One rule decides the preamble of an operation: the program start or a new tool parks at the
// machine home with `G53 G0 Z0`, then `T.. M6`, then `G43 Z<safe> H<n>`; the same tool keeps its
// tool and H, starts a new spindle speed when it differs, and still gets G43 when only the H row
// changed. M8 is written when the coolant was off, M9 at the end of an operation that turned it on.
// M1 marks every operation after the first. Keeping the state in one place is what makes the rule
// testable without reading the emitted text.
import type { Operation } from './ops'
import { commentSafe, coord, num, tag, toolType } from './emit-words'

/** The state of the machine between two operations. */
export interface EmitState {
  /** The feed the control holds modally, mm/min. */
  hasFeed: boolean
  feed: number
  /** The tool number the machine holds; 0 — none yet. */
  tool: number
  /** The spindle speed the machine runs at after the last operation; 0 — stopped. */
  spindleRpm: number
  /** True when the coolant was switched on by the current operation. */
  coolantOn: boolean
  /** The H row the machine was given by G43; 0 — none yet. */
  hNumber: number
  /** Operations already started: the M1 stop belongs to every one after the first. */
  operationsStarted: number
}

/** A block of the file: the numbered line and, for a move, the motion mode and the words it wrote. */
export interface Block {
  text: string
  /** The motion mode after the block: 0, 1, 2 or 3; absent when the block is not a motion. */
  motion?: number
  /** True when the block wrote a feed word F. */
  feed?: boolean
}

/** A fresh machine: no tool, no feed, no spindle, no coolant. */
export const freshState = (): EmitState => ({
  hasFeed: false,
  feed: 0,
  tool: 0,
  spindleRpm: 0,
  coolantOn: false,
  hNumber: 0,
  operationsStarted: 0,
})

/**
 * The decoded fields of one preamble: the parks, the change, the G43 and the spindle and coolant
 * words of the operation. The emitter writes them in the order of the returned list, so the rule is
 * one function and not a chain of conditions spread over the printer.
 */
export const operationStart = (operation: Operation, h: number, state: EmitState) => {
  // A custom block without a tool number changes no tool: its lines are its own program.
  const toolChanged = state.tool !== operation.tool.number
  const toolLess = operation.kind === 'custom' && operation.tool.number <= 0
  return {
    changeTool: !toolLess && (state.tool === 0 || toolChanged),
    g43: toolChanged || state.hNumber !== h,
    spindle: state.spindleRpm <= 0 || Math.abs(state.spindleRpm - operation.cutting.spindleRpm) > 1e-6,
    coolant: operation.coolant !== 'off' && !state.coolantOn,
  }
}

/**
 * The machine after the preamble of one operation, written as blocks in the order of the reference
 * post: M1 (from the second operation), the tags, the park, the change, G43, the spindle, M8.
 */
export const preambleBlocks = (
  operation: Operation,
  uuid: string,
  h: number,
  safeZ: number,
  stopAfterOp: boolean,
  state: EmitState,
): Block[] => {
  const blocks: Block[] = []
  const next = (text: string): void => {
    blocks.push({ text })
  }
  if (state.operationsStarted > 0 && stopAfterOp) next('M1')
  state.operationsStarted += 1
  const rules = operationStart(operation, h, state)
  const toolLess = operation.kind === 'custom' && operation.tool.number <= 0
  const id = operation.id.length > 0 ? operation.id : 'Операция'
  next(tag(`UI_OP: ${commentSafe(id)}`))
  next(tag(`UI_STAGE: ${uuid}`))
  // A block without a tool prints no UI_TOOL: the viewer then warns (`op-no-tool`), which is the
  // honest reading of a stage the operator did not give a tool.
  if (!toolLess) {
    next(tag(`UI_TOOL: T=${operation.tool.number} TYPE=${toolType(operation)} D=${num(operation.tool.diameterMm)}`))
  }
  if (rules.changeTool) {
    next('G53 G0 Z0')
    next(`T${operation.tool.number} M6`)
    state.tool = operation.tool.number
  }
  if (rules.g43) {
    next(`G43 Z${coord(safeZ)} H${h}`)
    state.hNumber = h
  }
  if (rules.spindle) {
    next(`S${num(operation.cutting.spindleRpm)} M3`)
    state.spindleRpm = operation.cutting.spindleRpm
  }
  if (rules.coolant) next('M8')
  state.coolantOn = operation.coolant !== 'off'
  return blocks
}

/** The machine after an operation: M9 when the coolant was on, M5, the park word when not at safe Z. */
export const trailerBlocks = (
  coolant: Operation['coolant'],
  safeZ: number,
  parkedZ: number,
  state: EmitState,
): Block[] => {
  const blocks: Block[] = []
  if (coolant !== 'off') blocks.push({ text: 'M9' })
  blocks.push({ text: 'M5' })
  state.coolantOn = false
  state.spindleRpm = 0
  if (Math.abs(parkedZ - safeZ) > 1e-6) blocks.push({ text: `G0 Z${coord(safeZ)}` })
  return blocks
}
