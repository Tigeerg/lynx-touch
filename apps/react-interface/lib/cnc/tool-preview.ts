/**
 * The tools the block shows: the one in the spindle and the next one the program calls. The operator
 * prepares the next tool while the current one is cutting, so a programme with several `T` words must
 * not show only the first tool of the list.
 *
 * `calls` are the tools in the order the program calls them (`programTools`); `current` is the tool in
 * the spindle. A tool that the program never calls is still shown with the first call, so the block
 * is never empty and the next change is visible.
 */
export function toolPreview(calls: readonly number[], current: number): number[] {
  const at = calls.indexOf(current)
  if (at >= 0) return calls.slice(at, at + 2)
  return calls.length > 0 ? [current, calls[0]] : [current]
}
