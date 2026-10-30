// The words of a move and of a drilling cycle (PLAN-QUICKPROG, 7.1), the port of `emit_moves.cpp`.
//
// A word is written only when its value changes, so a strip line does not repeat the Y and the Z of
// the line before. A block that would carry no coordinates and no feed is a move to the point the
// tool already stands at: it is dropped instead of becoming a bare `G0`. An arc is G3 with X/Y/Z
// where the end point differs, the I/J of the move and the feed where it changes; a full turn is
// never written without an end point, the planner writes half arcs instead.
import type { CamMove } from './moves'
import type { Point3 } from './ops'
import type { Block, EmitState } from './emit-state'
import { coord, feedWord } from './emit-words'

/** The parser resolution: a coordinate that differs by less is the same coordinate. */
const EPS = 1e-6

/** The modal words of one operation: the axes, the feed and the motion mode. */
export interface Modal {
  hasX: boolean
  hasY: boolean
  hasZ: boolean
  x: number
  y: number
  z: number
  hasFeed: boolean
  feed: number
  /** The last motion word: 0, 1, 2 or 3; -1 before the first move of the block. */
  motion: number
}

/** The modal state at the start of an operation: the tool stands at safe Z, XY unknown. */
export const openModal = (park: Point3, state: EmitState): Modal => ({
  hasX: false,
  hasY: false,
  hasZ: true,
  x: 0,
  y: 0,
  z: park.z,
  hasFeed: state.hasFeed,
  feed: state.feed,
  motion: -1,
})

/** An axis word, or nothing when the value is the modal one; the modal value moves on. */
const axisWord = (letter: string, value: number, modal: Modal, axis: 'x' | 'y' | 'z'): string => {
  const has = modal[`has${axis.toUpperCase()}` as 'hasX' | 'hasY' | 'hasZ']
  if (has && Math.abs(value - modal[axis]) <= EPS) return ''
  modal[`has${axis.toUpperCase()}` as 'hasX' | 'hasY' | 'hasZ'] = true
  modal[axis] = value
  return `${letter}${coord(value)}`
}

const append = (words: string, word: string): string =>
  word.length === 0 ? words : words.length === 0 ? word : `${words} ${word}`

/** The words of one move; the feed word travels only with a cut and only when it changes. */
export const moveWords = (move: CamMove, modal: Modal): string => {
  const g = move.arc !== undefined ? (move.arc.cw ? 2 : 3) : move.kind === 'cut' ? 1 : 0
  let words = ''
  if (modal.motion !== g) {
    words = `G${String(g)}`
    modal.motion = g
  }
  words = append(words, axisWord('X', move.to.x, modal, 'x'))
  words = append(words, axisWord('Y', move.to.y, modal, 'y'))
  words = append(words, axisWord('Z', move.to.z, modal, 'z'))
  // I and J are not modal: both are written on every arc block, or the control has no centre.
  if (move.arc !== undefined) {
    words = append(words, `I${coord(move.arc.i)}`)
    words = append(words, `J${coord(move.arc.j)}`)
  }
  if (move.kind === 'cut' && (!modal.hasFeed || Math.abs((move.feedMmMin ?? 0) - modal.feed) > EPS)) {
    words = append(words, `F${feedWord(move.feedMmMin ?? 0)}`)
    modal.hasFeed = true
    modal.feed = move.feedMmMin ?? 0
  }
  return words
}

/** The blocks of the moves of one operation; a move to the point the tool stands at is skipped. */
export const moveBlocks = (moves: CamMove[], park: Point3, state: EmitState): Block[] => {
  const modal = openModal(park, state)
  const blocks: Block[] = []
  let previous = park
  for (const move of moves) {
    if (move.to.x === previous.x && move.to.y === previous.y && move.to.z === previous.z) continue
    const words = moveWords(move, modal)
    if (words.length === 0) continue
    blocks.push({ text: words, motion: modal.motion, feed: words.includes('F') })
    previous = move.to
  }
  state.hasFeed = modal.hasFeed
  state.feed = modal.feed
  return blocks
}

/** One canned drilling cycle: the first block carries the cycle, the depth, the R plane and Q. */
export interface CycleWords {
  peck: boolean
  depthZ: number
  rZ: number
  peckMm: number
  feedMmMin: number
}

/** The blocks of one drilling cycle: the holes, the G80 that closes it, and the modal feed. */
export const cycleBlocks = (holes: Point3[], cycle: CycleWords, state: EmitState): Block[] => {
  const blocks: Block[] = []
  let hasFeed = state.hasFeed
  let feed = state.feed
  holes.forEach((hole, index) => {
    if (index > 0) {
      blocks.push({ text: `X${coord(hole.x)} Y${coord(hole.y)}` })
      return
    }
    // The cycle word carries the depth, the planes and Q: a control starts the cycle only from it.
    let words = cycle.peck ? 'G83' : 'G81'
    words += ` X${coord(hole.x)} Y${coord(hole.y)} Z${coord(cycle.depthZ)}`
    words += ` R${coord(cycle.rZ)}`
    if (cycle.peck) words += ` Q${coord(cycle.peckMm)}`
    if (!hasFeed || Math.abs(cycle.feedMmMin - feed) > EPS) {
      words += ` F${feedWord(cycle.feedMmMin)}`
      hasFeed = true
      feed = cycle.feedMmMin
    }
    blocks.push({ text: words })
  })
  blocks.push({ text: 'G80' })
  state.hasFeed = hasFeed
  state.feed = feed
  return blocks
}
