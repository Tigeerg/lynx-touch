// Which slice of a long NC program the list draws. A real CAM program can hold tens of thousands
// of blocks (the test model is 110 586); drawing them all froze the panel. The list therefore keeps
// a window of blocks around the block it follows and slides the window as the cursor moves. The
// window is a plain function so the arithmetic is checked without a browser.
export const PROGRAM_WINDOW = 60

/**
 * First and one-past-last block of the visible window. `follow` is the block the list follows (the
 * run line, the simulation line or the cursor); it is kept roughly in the middle of the window,
 * and the window never leaves `[0, total)`.
 */
export function programWindow(follow: number, total: number, size = PROGRAM_WINDOW): { start: number; end: number } {
  if (total <= size) return { start: 0, end: total }
  const start = Math.min(Math.max(0, follow - Math.floor(size / 2)), total - size)
  return { start, end: start + size }
}
