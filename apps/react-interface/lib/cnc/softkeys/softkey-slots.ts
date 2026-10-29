// The softkey slot: the shape of one key, the bar width, the line budget of a label and the two
// helpers every set goes through. The sets themselves live in softkey-dialogs.ts,
// softkey-workplaces.ts and softkey-screens.ts (CLAUDE.md, section 2).
import type { LucideIcon } from 'lucide-react'
import type { Action } from '../subsystem'

export const SOFTKEY_SLOTS = 8

// The label wraps inside the slot, so what must fit on one line is a single word (CLAUDE.md,
// section 5: the panel is 1024 wide, the slot is a fixed width, so the length of the line is
// part of the check, not taste). The panel is 1024 wide, the bar is eight columns with a
// 0.375rem gap and 0.375rem panel padding, so the line budget is 115 px; measured in the panel
// with its font and letter-spacing 0.64px, that is 14 characters at 12 px. A longer word is
// broken or clipped, so the guard holds the longest word of a label, not the whole label.
// Reference: "инструмента" (11) fits, a 15-character word does not.
export const SOFTKEY_LABEL_MAX = 14

export interface Softkey {
  label: string
  icon: LucideIcon
  action: Action
  active?: boolean
  disabled?: boolean
  /** Why the key is disabled: shown as a hint and in the message line. */
  reason?: string
}

export const FALLBACK_REASON = 'Сейчас недоступно'

// Pads the set to eight slots and fills in the reason for disabled keys, so a disabled key
// does not stay silent (project rule).
export const slots = (keys: (Softkey | null)[]): (Softkey | null)[] =>
  [...keys, ...Array<null>(Math.max(0, SOFTKEY_SLOTS - keys.length)).fill(null)].map((k) =>
    k && k.disabled && !k.reason ? { ...k, reason: FALLBACK_REASON } : k,
  )

/** What a press does: a disabled key explains itself instead of staying silent. */
export const softkeyAction = (k: Softkey): Action =>
  k.disabled ? { type: 'HINT', text: k.reason ?? FALLBACK_REASON } : k.action
