'use client'

// The panel clock keeps its own second. It lived in useCncMachine, so its once-a-second setState
// re-rendered the whole panel - every screen and every softkey - for a value only the header
// shows (CLAUDE.md, section 2: one module, one job; section 6.6: local state is for interface
// detail, not machine state).
import { useEffect, useState } from 'react'

const TIME_OPTIONS: Intl.DateTimeFormatOptions = { hour12: false }

export function Clock({ className }: { className?: string }) {
  const [time, setTime] = useState<string | null>(null)

  useEffect(() => {
    const id = window.setInterval(() => setTime(new Date().toLocaleTimeString('ru-RU', TIME_OPTIONS)), 1000)
    return () => window.clearInterval(id)
  }, [])

  return (
    <time className={className} suppressHydrationWarning>
      {time ?? '--:--:--'}
    </time>
  )
}
