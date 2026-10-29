'use client'

import { useEffect, useRef, type ReactNode } from 'react'
import { cn } from '@/lib/utils'

export const HOLD_DELAY_MS = 400
export const HOLD_REPEAT_MS = 90

export const KEY_BASE =
  'flex select-none items-center justify-center rounded-md border border-b-[3px] font-semibold transition-[transform,background-color] active:translate-y-px active:border-b'

export function HoldButton({
  onFire,
  className,
  label,
  children,
}: {
  onFire: () => void
  className?: string
  label: string
  children: ReactNode
}) {
  const timers = useRef<{ delay?: number; repeat?: number }>({})
  const stop = () => {
    window.clearTimeout(timers.current.delay)
    window.clearInterval(timers.current.repeat)
    timers.current = {}
  }
  useEffect(() => stop, [])

  return (
    <button
      type="button"
      tabIndex={-1}
      aria-label={label}
      onPointerDown={(e) => {
        e.currentTarget.setPointerCapture(e.pointerId)
        // A second pointer on the same key used to orphan the first delay: its interval kept
        // firing after the stop, and only one handle is kept (the review found the leak).
        stop()
        onFire()
        timers.current.delay = window.setTimeout(() => {
          timers.current.repeat = window.setInterval(onFire, HOLD_REPEAT_MS)
        }, HOLD_DELAY_MS)
      }}
      onPointerUp={stop}
      onPointerCancel={stop}
      onLostPointerCapture={stop}
      onContextMenu={(e) => e.preventDefault()}
      className={cn(KEY_BASE, className)}
    >
      {children}
    </button>
  )
}
