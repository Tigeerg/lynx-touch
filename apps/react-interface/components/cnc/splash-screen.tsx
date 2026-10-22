'use client'

import { useEffect, useState } from 'react'
import { cn } from '@/lib/utils'

const SPLASH_MS = 10_000
const LEAVE_MS = 500

type Phase = 'shown' | 'leaving' | 'gone'

// Module-level so switching Panel/Touch (which remounts the panel) doesn't replay the boot splash.
let splashDone = false

export function SplashScreen() {
  const [phase, setPhase] = useState<Phase>(() => (splashDone ? 'gone' : 'shown'))
  useEffect(() => {
    if (phase !== 'shown') splashDone = true
  }, [phase])

  useEffect(() => {
    if (phase !== 'shown') return
    const timer = window.setTimeout(() => setPhase('leaving'), SPLASH_MS)
    const dismiss = (e: Event) => {
      if (e instanceof KeyboardEvent) {
        if (e.ctrlKey || e.metaKey || e.altKey) return
        e.preventDefault()
        e.stopPropagation()
      }
      setPhase('leaving')
    }
    window.addEventListener('keydown', dismiss, true)
    window.addEventListener('pointerdown', dismiss, true)
    return () => {
      window.clearTimeout(timer)
      window.removeEventListener('keydown', dismiss, true)
      window.removeEventListener('pointerdown', dismiss, true)
    }
  }, [phase])

  useEffect(() => {
    if (phase !== 'leaving') return
    const timer = window.setTimeout(() => setPhase('gone'), LEAVE_MS)
    return () => window.clearTimeout(timer)
  }, [phase])

  if (phase === 'gone') return null

  return (
    <div
      role="dialog"
      aria-label="Lynx Touch — ЧПУ контроллер на базе LinuxCNC"
      className={cn(
        'absolute inset-0 z-50 flex select-none flex-col items-center justify-center overflow-hidden bg-splash text-hw-label transition-opacity ease-out',
        phase === 'leaving' ? 'pointer-events-none opacity-0' : 'opacity-100',
      )}
      style={{ transitionDuration: `${LEAVE_MS}ms` }}
    >
      <div
        aria-hidden
        className="absolute inset-0 opacity-[0.07]"
        style={{
          backgroundImage:
            'linear-gradient(var(--hw-label) 1px, transparent 1px), linear-gradient(90deg, var(--hw-label) 1px, transparent 1px)',
          backgroundSize: '32px 32px',
          maskImage: 'radial-gradient(ellipse 60% 55% at 50% 45%, black, transparent 80%)',
          WebkitMaskImage: 'radial-gradient(ellipse 60% 55% at 50% 45%, black, transparent 80%)',
        }}
      />

      <div className="relative flex flex-col items-center">
        {/* eslint-disable-next-line @next/next/no-img-element */}
        <img
          src="/lynx-logo.png"
          alt="Логотип Lynx Touch"
          width={1024}
          height={1024}
          className="splash-anim h-[300px] w-auto"
          style={{
            animation: 'splash-rise 1100ms cubic-bezier(0.16, 1, 0.3, 1) both',
            maskImage: 'radial-gradient(circle at 50% 50%, black 52%, transparent 71%)',
            WebkitMaskImage: 'radial-gradient(circle at 50% 50%, black 52%, transparent 71%)',
          }}
        />

        <h1
          className="splash-anim -mt-10 font-sans text-5xl font-semibold leading-none tracking-tight text-hw-key-foreground"
          style={{ animation: 'splash-rise 900ms 350ms cubic-bezier(0.16, 1, 0.3, 1) both' }}
        >
          Lynx <span className="font-light text-hw-blue">Touch</span>
        </h1>

        <p
          className="splash-anim mt-3 text-center font-sans text-sm tracking-[0.18em] text-hw-label uppercase"
          style={{ animation: 'splash-rise 900ms 600ms cubic-bezier(0.16, 1, 0.3, 1) both' }}
        >
          ЧПУ контроллер на базе LinuxCNC
        </p>
      </div>

      <div
        className="splash-anim absolute inset-x-0 bottom-6 flex flex-col items-center gap-2"
        style={{ animation: 'splash-fade 800ms 1200ms ease-out both' }}
      >
        <div className="h-px w-72 overflow-hidden bg-hw-label/20">
          <div
            className="h-full origin-left bg-hw-blue"
            style={{ animation: `splash-progress ${SPLASH_MS}ms linear both` }}
          />
        </div>
        <p
          className="font-mono text-xs tracking-widest text-hw-label/70 uppercase"
          style={{ animation: 'splash-blink 2.4s ease-in-out infinite' }}
        >
          Нажмите любую клавишу
        </p>
      </div>
    </div>
  )
}
