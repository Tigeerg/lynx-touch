// The G82 counterboring cycle (PLAN-QUICKPROG 7.6): the words of one seat and the closing G80.
//
// G82 is G81 with a dwell `P` at the floor. Every seat carries the whole cycle (`G82 X Y Z R P`),
// not only a bare move as G81 does: the dwell is part of the cycle block, and a control reading a
// bare `X Y` after `G82` would run it with the old P. The feed travels modally, as in every cycle.
import type { Block, EmitState } from './emit-state'
import { coord, feedWord } from './emit-words'

/** The parser resolution: a coordinate that differs by less is the same coordinate. */
const EPS = 1e-6

/** A counterboring cycle G82: the depth, the R plane, the dwell at the floor and the feed. */
export interface CounterboreWords {
  depthZ: number
  rZ: number
  dwellSec: number
  feedMmMin: number
}

/** The `P` of G82: seconds up to three digits (`P0.2`), the integer form when whole (`P1`). */
export const dwellWord = (seconds: number): string =>
  Math.abs(seconds - Math.round(seconds)) < EPS ? String(Math.round(seconds)) : seconds.toFixed(3)

/**
 * The blocks of one G82 counterboring cycle: every seat carries `G82 X Y Z R P`, the dwell repeated
 * so a control never runs a bare move as a hole, and `G80` closes the cycle.
 */
export const counterboreBlocks = (
  holes: { x: number; y: number }[],
  cycle: CounterboreWords,
  state: EmitState,
): Block[] => {
  const blocks: Block[] = []
  let hasFeed = state.hasFeed
  let feed = state.feed
  for (const hole of holes) {
    let words = `G82 X${coord(hole.x)} Y${coord(hole.y)} Z${coord(cycle.depthZ)} R${coord(cycle.rZ)}`
    words += ` P${dwellWord(cycle.dwellSec)}`
    if (!hasFeed || Math.abs(cycle.feedMmMin - feed) > EPS) {
      words += ` F${feedWord(cycle.feedMmMin)}`
      hasFeed = true
      feed = cycle.feedMmMin
    }
    blocks.push({ text: words })
  }
  blocks.push({ text: 'G80' })
  state.hasFeed = hasFeed
  state.feed = feed
  return blocks
}
