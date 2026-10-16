'use client'

import { ArrowBigUp, Delete, Eraser, Space } from 'lucide-react'
import { useEffect, useRef, useState } from 'react'
import type { Action } from '@/lib/cnc/machine'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'
import { HwKey } from './hw-parts'

type Shift = 'off' | 'once' | 'lock'

type KeyDef = { letter: string; alt: string; block: 'address' | 'numeric' }

const address = (pairs: string): KeyDef[] =>
  pairs.split(' ').map(([letter, alt]) => ({ letter, alt, block: 'address' }))
const numeric = (pairs: string): KeyDef[] =>
  pairs.split(' ').map(([letter, alt]) => ({ letter, alt, block: 'numeric' }))

// Левый блок: адреса G-кода и знаки. Правый блок: буквы с цифрами в раскладке цифровой клавиатуры.
const ROWS: KeyDef[][] = [
  [...address('O# N% G('), ...numeric('A7 B8 C9')],
  [...address('X) Y[ Z]'), ...numeric('D4 H5 L6')],
  [...address('M= S* T+'), ...numeric('P1 Q2 R3')],
  [...address('F/ I, J;'), ...numeric('U- V0 W.')],
]
const LAST_ADDRESS_ROW = address('K: E_')

function noFieldHint(state: MachineState): string {
  if (state.mode === 'AUTO' && state.screen === 'prog') return 'Нет поля ввода: включите правку и выберите кадр'
  return 'Ввод доступен в режиме MDI (клавиша M) или при правке кадра программы'
}

const SELECTABLE_INPUT_TYPES = ['text', 'search', 'url', 'tel', 'password']

function activeField(): HTMLInputElement | HTMLTextAreaElement | null {
  const el = document.activeElement
  // number/color/range inputs throw on setSelectionRange -> panel keyboard skips them
  if (el instanceof HTMLTextAreaElement) return el
  if (el instanceof HTMLInputElement) return SELECTABLE_INPUT_TYPES.includes(el.type) ? el : null
  return null
}

function replaceRange(el: HTMLInputElement | HTMLTextAreaElement, from: number, to: number, text: string) {
  const proto = el instanceof HTMLInputElement ? HTMLInputElement.prototype : HTMLTextAreaElement.prototype
  const setValue = Object.getOwnPropertyDescriptor(proto, 'value')?.set
  if (!setValue) return
  setValue.call(el, el.value.slice(0, from) + text + el.value.slice(to))
  el.setSelectionRange(from + text.length, from + text.length)
  el.dispatchEvent(new Event('input', { bubbles: true }))
}

export function HardwareKeyboard({
  state,
  dispatch,
  className,
}: {
  state: MachineState
  dispatch: (a: Action) => void
  className?: string
}) {
  const [hint, setHint] = useState<string | null>(null)
  const [shift, setShift] = useState<Shift>('off')
  const timer = useRef<number | undefined>(undefined)
  const editor = state.editor

  useEffect(() => () => window.clearTimeout(timer.current), [])

  const flash = (text: string) => {
    setHint(text)
    window.clearTimeout(timer.current)
    timer.current = window.setTimeout(() => setHint(null), 2500)
  }

  const mdi = state.mode === 'MDI'
  const mdiBlocked = !mdi
    ? null
    : state.run === 'RUN' || state.run === 'HOLD'
      ? 'Станок занят: ввод MDI недоступен'
      : state.screen !== 'pos'
        ? 'Откройте вкладку «Позиция» для ввода MDI'
        : null

  const mdiEdit = (next: (buffer: string) => string) => {
    if (mdiBlocked) return flash(mdiBlocked)
    dispatch({ type: 'MDI_BUFFER', value: next(state.mdi.buffer) })
  }

  const type = (text: string) => {
    const el = activeField()
    if (el) {
      const from = el.selectionStart ?? el.value.length
      replaceRange(el, from, el.selectionEnd ?? from, text)
    } else if (editor) {
      dispatch({ type: 'EDIT_BUFFER', value: editor.buffer + text })
    } else if (mdi) {
      mdiEdit((b) => b + text)
    } else {
      flash(noFieldHint(state))
    }
  }

  const press = (key: KeyDef) => {
    type(shift === 'off' ? key.letter : key.alt)
    if (shift === 'once') setShift('off')
  }

  const cycleShift = () => setShift((s) => (s === 'off' ? 'once' : s === 'once' ? 'lock' : 'off'))

  const erase = () => {
    const el = activeField()
    if (el) {
      const from = el.selectionStart ?? el.value.length
      const to = el.selectionEnd ?? from
      if (from !== to) replaceRange(el, from, to, '')
      else if (from > 0) replaceRange(el, from - 1, from, '')
    } else if (editor) {
      dispatch({ type: 'EDIT_BUFFER', value: editor.buffer.slice(0, -1) })
    } else if (mdi) {
      mdiEdit((b) => b.slice(0, -1))
    } else {
      flash(noFieldHint(state))
    }
  }

  const clearLine = () => {
    const el = activeField()
    if (el) replaceRange(el, 0, el.value.length, '')
    else if (editor) dispatch({ type: 'EDIT_BUFFER', value: '' })
    else if (mdi) mdiEdit(() => '')
    else flash(noFieldHint(state))
  }

  const shifted = shift !== 'off'
  const legend = (key: KeyDef) => <Legend key={key.letter} def={key} shifted={shifted} onPress={() => press(key)} />

  return (
    <section
      aria-label="Клавиатура ЧПУ"
      className={cn(
        'relative flex flex-none flex-col justify-center rounded-lg border-2 border-hw-edge bg-hw-plate p-4 shadow-inner',
        className,
      )}
    >
      <p
        role="status"
        aria-live="polite"
        className={cn(
          'absolute inset-x-3 top-1 z-10 truncate rounded bg-hw-edge px-2 py-0.5 text-center text-[14px] font-medium text-hw-amber',
          !hint && 'sr-only',
        )}
      >
        {hint}
      </p>

      <div className="grid grid-cols-[repeat(6,4.5rem)] justify-center gap-x-2 gap-y-4">
        {ROWS.flat().map(legend)}

        <HwKey
          label="Shift"
          sub={shift === 'lock' ? 'фикс.' : shift === 'once' ? '1 раз' : undefined}
          icon={ArrowBigUp}
          title="Shift: цифры и символы. Нажмите дважды для фиксации"
          lit={shifted}
          tone="warning"
          cap="bg-hw-blue text-hw-ink"
          onClick={cycleShift}
        />
        {LAST_ADDRESS_ROW.map(legend)}
        <HwKey icon={Space} label="Пробел" title="Пробел" onClick={() => type(' ')} />
        <HwKey icon={Eraser} label="Очист." title="Очистить строку ввода" onClick={clearLine} />
        <HwKey
          label={<Delete className="size-5" aria-label="Стереть символ" />}
          title="Стереть символ"
          cap="bg-hw-amber text-hw-ink"
          onClick={erase}
        />
      </div>
    </section>
  )
}

function Legend({ def, shifted, onPress }: { def: KeyDef; shifted: boolean; onPress: () => void }) {
  const main = shifted ? def.alt : def.letter
  const small = shifted ? def.letter : def.alt
  return (
    <HwKey
      title={`${def.letter} / ${def.alt}`}
      onClick={onPress}
      cap={def.block === 'address' ? 'bg-hw-cap-alt text-hw-ink' : undefined}
      className="font-mono"
      label={
        <>
          <span className="sr-only">{main}</span>
          <span
            aria-hidden="true"
            className="absolute right-1 top-0.5 text-[13px] font-semibold leading-none opacity-75"
          >
            {small}
          </span>
          <span aria-hidden="true" className="text-[30px] font-bold leading-none">
            {main}
          </span>
        </>
      }
    />
  )
}
