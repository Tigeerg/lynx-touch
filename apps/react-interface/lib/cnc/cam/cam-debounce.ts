// The debounce of the preview (PLAN-QUICKPROG, 7.5): a field edit waits for the last keystroke.
//
// The reducer stays pure and synchronous: `CAM_PARAM` applies the typed text at once and marks the
// preview `pending`, and the timer that closes the gap is a UI concern. The delay and the piece of
// state the next `CAM_PREVIEW` is read from live here, so the form and the overlay call one helper:
// a second tick keeps the first (its head start is the older edit), `fire` clears the tick first.
export const PREVIEW_DEBOUNCE_MS = 150

/** What a tick reads at fire time: the latest edit is the one worth the recompute. */
export interface PreviewClock {
  pending: boolean
  form: unknown
}

/** The bookkeeping of one timer: a new tick replaces the old one instead of adding a second. */
export interface PreviewTimer {
  arm: (fire: () => void) => void
  cancel: () => void
}

/**
 * A set/clear pair over `setTimeout`. `fire` clears the tick before it runs, so a schedule made
 * from inside `fire` is not cancelled by the function itself, and the `armed` flag lets a caller
 * skip a stateless re-arm that would only restart the countdown of the same edit.
 */
export const previewTimer = (): PreviewTimer => {
  let id: ReturnType<typeof setTimeout> | null = null
  return {
    arm: (fire) => {
      if (id !== null) clearTimeout(id)
      id = setTimeout(() => {
        id = null
        fire()
      }, PREVIEW_DEBOUNCE_MS)
    },
    cancel: () => {
      if (id !== null) clearTimeout(id)
      id = null
    },
  }
}

/** A new edit: the timer must cover it, even when the previous edit's timer is still running. */
export const needsPreview = (form: unknown, previous: unknown): boolean => form !== previous

/** A `pending` field edit with an unfamiliar form waits for the timer and nothing else. */
export const waitsForPreview = (cam: PreviewClock | null, form: unknown): boolean =>
  cam !== null && cam.pending === true && cam.form === form
