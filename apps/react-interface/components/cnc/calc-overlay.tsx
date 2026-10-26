'use client'

import { Calculator, X } from 'lucide-react'
import { useState, type Dispatch } from 'react'
import { CALC_DEFAULTS, cuttingFeed, cuttingRpm } from '@/lib/cnc/calc'
import type { Action } from '@/lib/cnc/machine'
import { Overlay } from './overlay'

function NumField({
  label,
  unit,
  value,
  step,
  onChange,
}: {
  label: string
  unit: string
  value: number
  step: number
  onChange: (v: number) => void
}) {
  return (
    <label className="flex items-center justify-between gap-2 border-b px-4 py-2 last:border-b-0">
      <span className="min-w-0 flex-1 text-xs font-semibold uppercase tracking-wide text-muted-foreground">
        {label}
      </span>
      <input
        type="number"
        inputMode="decimal"
        value={String(value)}
        min={0}
        step={step}
        onChange={(e) => onChange(Math.max(0, Number(e.target.value)))}
        className="w-24 rounded-sm border bg-background px-2 py-1 text-right font-mono text-base font-bold tabular-nums outline-none focus:border-primary"
      />
      <span className="w-12 shrink-0 text-[10px] font-semibold uppercase text-muted-foreground">{unit}</span>
    </label>
  )
}

export function CalcOverlay({ dispatch }: { dispatch: Dispatch<Action> }) {
  const [vc, setVc] = useState<number>(CALC_DEFAULTS.vc)
  const [d, setD] = useState<number>(CALC_DEFAULTS.d)
  const [fz, setFz] = useState<number>(CALC_DEFAULTS.fz)
  const [flutes, setFlutes] = useState<number>(CALC_DEFAULTS.flutes)
  const n = cuttingRpm(vc, d)
  const feed = cuttingFeed(fz, flutes, n)

  return (
    <Overlay
      onClose={() => dispatch({ type: 'CALC', open: false })}
      label="Калькулятор режимов"
      className="w-full max-w-sm border-2 border-foreground/20"
    >
      <header className="flex items-center gap-2 border-b bg-muted px-4 py-2">
        <Calculator className="size-4 text-primary" aria-hidden="true" />
        <h2 className="text-xs font-bold uppercase tracking-wide">Калькулятор режимов</h2>
        <button
          type="button"
          tabIndex={-1}
          aria-label="Закрыть"
          className="ml-auto rounded p-1 hover:bg-accent"
          onClick={() => dispatch({ type: 'CALC', open: false })}
        >
          <X className="size-4" aria-hidden="true" />
        </button>
      </header>

      <div className="border-b">
        <NumField label="Скорость Vc" unit="мм/мин" value={vc} step={10} onChange={setVc} />
        <NumField label="Диаметр D" unit="мм" value={d} step={0.5} onChange={setD} />
        <NumField label="Подача на зуб fz" unit="мм/зуб" value={fz} step={0.01} onChange={setFz} />
        <NumField
          label="Число зубьев z"
          unit="шт"
          value={flutes}
          step={1}
          onChange={(v) => setFlutes(Math.max(1, Math.round(v)))}
        />
      </div>

      <div className="grid grid-cols-2 divide-x">
        <div className="px-4 py-3">
          <p className="text-[10px] font-semibold uppercase tracking-wide text-muted-foreground">Обороты S</p>
          <p className="font-mono text-2xl font-bold tabular-nums">{n.toLocaleString('ru-RU')}</p>
          <p className="text-[10px] text-muted-foreground">об/мин</p>
        </div>
        <div className="px-4 py-3">
          <p className="text-[10px] font-semibold uppercase tracking-wide text-muted-foreground">Подача F</p>
          <p className="font-mono text-2xl font-bold tabular-nums">{feed.toLocaleString('ru-RU')}</p>
          <p className="text-[10px] text-muted-foreground">мм/мин</p>
        </div>
      </div>

      <footer className="border-t bg-muted px-4 py-1.5 font-mono text-[10px] text-muted-foreground">
        n = 1000*Vc/(pi*D) | F = fz*z*n | Esc — закрыть
      </footer>
    </Overlay>
  )
}
