'use client'

import { type ComponentType, type ReactNode, type WheelEvent } from 'react'
import { cn } from '@/lib/utils'

export type HwIcon = ComponentType<{ className?: string; 'aria-hidden'?: boolean | 'true' | 'false' }>

export function LubeIcon({ className, ...props }: { className?: string; 'aria-hidden'?: boolean | 'true' | 'false' }) {
  return (
    <svg
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth="2"
      strokeLinecap="round"
      strokeLinejoin="round"
      className={className}
      {...props}
    >
      <path d="M12 2.5c0 0-3 3.2-3 5.2a3 3 0 0 0 6 0c0-2-3-5.2-3-5.2Z" />
      <rect x="6" y="12.5" width="12" height="4" rx="1" />
      <path d="M3 20.5h18" />
      <path d="M3 18.5v2M21 18.5v2" />
    </svg>
  )
}

export type KeyTone = 'neutral' | 'success' | 'warning' | 'primary' | 'destructive'

const LED_ON: Record<KeyTone, string> = {
  neutral: 'bg-hw-green',
  success: 'bg-hw-green',
  warning: 'bg-hw-amber',
  primary: 'bg-hw-blue',
  destructive: 'bg-hw-red',
}

const LED_GLOW: Record<KeyTone, string> = {
  neutral: 'shadow-[0_0_6px_1px_var(--hw-green)]',
  success: 'shadow-[0_0_6px_1px_var(--hw-green)]',
  warning: 'shadow-[0_0_6px_1px_var(--hw-amber)]',
  primary: 'shadow-[0_0_6px_1px_var(--hw-blue)]',
  destructive: 'shadow-[0_0_6px_1px_var(--hw-red)]',
}

const keepFocus = (e: { preventDefault: () => void }) => e.preventDefault()

export const KEY_ROW = 'shrink-0'
export const SMALL_KEY = 'gap-0 px-1 py-1 text-[12px] leading-tight [&_svg]:size-5'

export function HwGroup({ title, children, className }: { title: string; children: ReactNode; className?: string }) {
  return (
    <section aria-label={title} className={cn('flex min-w-0 flex-col gap-1.5', className)}>
      {children}
    </section>
  )
}

export function HwKey({
  label,
  sub,
  icon: Icon,
  lit,
  tone = 'neutral',
  cap,
  disabled,
  title,
  large,
  onClick,
  className,
}: {
  label: ReactNode
  sub?: string
  icon?: HwIcon
  lit?: boolean
  tone?: KeyTone
  cap?: string
  disabled?: boolean
  title?: string
  large?: boolean
  onClick: () => void
  className?: string
}) {
  return (
    <button
      type="button"
      tabIndex={-1}
      disabled={disabled}
      title={title}
      aria-pressed={lit}
      onMouseDown={keepFocus}
      onClick={onClick}
      className={cn(
        'relative flex flex-col items-center justify-center gap-1 rounded-lg border-[4px] border-b-[6px] border-hw-base px-1 text-[13px] font-bold leading-none shadow-[0_1px_2px_oklch(0_0_0/0.55),inset_0_0_0_1px_oklch(1_0_0/0.5),inset_0_2px_3px_oklch(1_0_0/0.9),inset_0_-3px_4px_oklch(0.3_0.02_255/0.2)] transition-[transform,filter] hover:brightness-105 active:translate-y-px active:border-b-[4px] disabled:cursor-not-allowed disabled:opacity-40 disabled:hover:brightness-100',
        !large && 'text-[12px]',
        !large && 'h-[72px] w-[72px] shrink-0',
        cap ?? 'bg-hw-cap text-hw-ink',
        className,
      )}
    >
      {lit !== undefined && (
        <span
          aria-hidden="true"
          className={cn(
            'pointer-events-none absolute -top-4 left-1/2 h-2 w-5 -translate-x-1/2 rounded-full border border-hw-base',
            lit ? cn(LED_ON[tone], LED_GLOW[tone]) : 'bg-hw-base/60 shadow-none',
          )}
        />
      )}
      {Icon && <Icon className={cn('shrink-0', large ? 'mb-1 size-10' : 'size-6')} aria-hidden="true" />}
      <span className={cn('whitespace-nowrap', large && 'text-xl font-extrabold uppercase tracking-wide')}>
        {label}
      </span>
      {sub && (
        <span
          className={cn(
            'font-medium opacity-80',
            large ? 'mt-1 font-mono text-[13px] tracking-normal' : 'text-[12px] opacity-70',
          )}
        >
          {sub}
        </span>
      )}
    </button>
  )
}

export function EmergencyStop({ pressed, onPress }: { pressed: boolean; onPress: () => void }) {
  return (
    <div className="relative flex flex-col items-center">
      <button
        type="button"
        tabIndex={-1}
        aria-pressed={pressed}
        aria-label="Аварийный стоп"
        onMouseDown={keepFocus}
        onClick={onPress}
        className="group flex size-[168px] items-center justify-center rounded-full border-[8px] border-hw-edge bg-hw-amber"
      >
        <span
          className={cn(
            'relative block size-[126px] rounded-full border-b-[10px] border-hw-edge bg-hw-red transition-[transform,filter,border-width]',
            pressed
              ? 'translate-y-[6px] border-b-[3px] brightness-75'
              : 'group-hover:brightness-110 group-active:translate-y-[6px] group-active:border-b-[3px]',
          )}
        >
          <span
            aria-hidden="true"
            className="absolute left-4 top-3 h-3.5 w-10 -rotate-12 rounded-full bg-hw-key-foreground/25"
          />
        </span>
      </button>
      <p className="absolute left-1/2 top-full mt-1.5 w-[180px] -translate-x-1/2 text-center text-[13px] font-medium leading-tight text-hw-label">
        {pressed ? 'Нажата — повторно отпустить' : 'Нажать для останова'}
      </p>
    </div>
  )
}

export const KNOB_STEP_DEG = 15

export function Knob({
  label,
  readout,
  offset,
  minOffset,
  maxOffset,
  onStep,
}: {
  label: string
  readout: string
  /** Шагов от 100% (вверх по центру): 0 — 100%, отрицательные — влево, положительные — вправо */
  offset: number
  minOffset: number
  maxOffset: number
  onStep: (dir: 1 | -1) => void
}) {
  const angle = offset * KNOB_STEP_DEG
  const marks = Array.from({ length: maxOffset - minOffset + 1 }, (_, i) => {
    const step = minOffset + i
    const a = (step * KNOB_STEP_DEG * Math.PI) / 180
    const inner = step === 0 ? 40 : 42
    return {
      key: step,
      major: step === 0 || step === minOffset || step === maxOffset,
      x1: 50 + inner * Math.sin(a),
      y1: 50 - inner * Math.cos(a),
      x2: 50 + 47 * Math.sin(a),
      y2: 50 - 47 * Math.cos(a),
    }
  })

  const onClick = (e: React.MouseEvent<HTMLButtonElement>) => {
    const r = e.currentTarget.getBoundingClientRect()
    onStep(e.clientX < r.left + r.width / 2 ? -1 : 1)
  }
  const onWheel = (e: WheelEvent<HTMLButtonElement>) => onStep(e.deltaY < 0 ? 1 : -1)

  return (
    <div className="flex min-w-0 flex-1 flex-col items-center gap-1.5">
      <p className="text-[13px] font-semibold uppercase tracking-wider text-hw-label">{label}</p>
      <button
        type="button"
        tabIndex={-1}
        aria-label={`${label}: ${readout}. Нажатие слева уменьшает, справа увеличивает`}
        onMouseDown={keepFocus}
        onClick={onClick}
        onWheel={onWheel}
        className="size-[132px] rounded-full"
      >
        <svg viewBox="0 0 100 100" className="size-full" aria-hidden="true">
          {marks.map(({ key, major, ...m }) => (
            <line key={key} {...m} stroke="var(--hw-label)" strokeWidth={major ? 2.5 : 1.5} strokeLinecap="round" />
          ))}
          <circle cx="50" cy="50" r="33" fill="var(--hw-edge)" />
          <circle
            cx="50"
            cy="50"
            r="30"
            fill="var(--hw-key)"
            stroke="var(--hw-label)"
            strokeOpacity="0.35"
            strokeWidth="1"
          />
          <circle cx="50" cy="50" r="22" fill="none" stroke="var(--hw-edge)" strokeOpacity="0.5" strokeWidth="1" />
          <g style={{ transform: `rotate(${angle}deg)`, transformOrigin: '50px 50px', transition: 'transform 150ms' }}>
            <line
              x1="50"
              y1="50"
              x2="50"
              y2="24"
              stroke="var(--hw-key-foreground)"
              strokeWidth="4.5"
              strokeLinecap="round"
            />
          </g>
        </svg>
      </button>
      <span className="text-center font-mono text-base font-bold tabular-nums text-hw-label">{readout}</span>
    </div>
  )
}
