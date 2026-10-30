'use client'

// The ten kinds of the quick program as tiles (PLAN-QUICKPROG, 7.4, C5 and 7.6): a word and a
// lucide icon, never an icon alone, so P1 (keys and touch, no guessing from a picture) reads the
// same as P3. The list is `TILES` here and exported, so a test counts the tiles without a DOM
// renderer.
//
// Ten tiles in a 276 px column are two columns by five rows: under the caption the panel free
// space is 439 px and the rows need 5 * 44 + 4 * 6 = 244 px, so they fit without a scrollbar; the
// group still scrolls inside the column rather than growing the card (the overlay clips it), and
// the touch variant keeps the same 2x5 grid with 64 px rows.
import {
  CircleDashed,
  Crosshair,
  Drill,
  Grid3x3,
  Layers,
  Minus,
  PenLine,
  Slice,
  Terminal,
  Wrench,
  type LucideIcon,
} from 'lucide-react'
import type { Dispatch } from 'react'
import { cn } from '@/lib/utils'
import type { CamAction } from '@/lib/cnc/cam/cam'
import type { OperationKind } from '@/lib/cnc/cam/ops'
import { useUiMode } from '../ui-mode-context'
import { KIND_LABEL } from './cam-labels'

/** The order of the tiles, the order of Q4: the MVP kinds first, then the added ones. */
export const TILES: { kind: OperationKind; icon: LucideIcon; hint: string }[] = [
  { kind: 'face', icon: Layers, hint: 'Снять слой с верха заготовки' },
  { kind: 'pocket', icon: Grid3x3, hint: 'Прямоугольный карман зигзагом' },
  { kind: 'circle-pocket', icon: CircleDashed, hint: 'Круглый карман спиралью' },
  { kind: 'drill', icon: Drill, hint: 'Отверстие, сетка или круг' },
  { kind: 'contour', icon: PenLine, hint: 'Профиль по точкам, замкнутый или открытый' },
  { kind: 'slot', icon: Minus, hint: 'Прямой паз с врезанием по ширине' },
  { kind: 'chamfer', icon: Slice, hint: 'Снятие кромки по прямой' },
  { kind: 'tap', icon: Wrench, hint: 'Резьба G84, станок должен держать жёсткое нарезание' },
  { kind: 'custom', icon: Terminal, hint: 'Готовый блок G-кода без проверки геометрии' },
  { kind: 'probe', icon: Crosshair, hint: 'Обмер между операциями щупом' },
]

export function OperationPicker({ dispatch }: { dispatch: Dispatch<CamAction> }) {
  const touch = useUiMode()?.mode === 'touch'
  return (
    <div className="flex min-h-0 flex-1 flex-col gap-1.5">
      <p className="shrink-0 text-[11px] text-muted-foreground">Выберите вид операции — форма откроется справа</p>
      <div
        className="grid min-h-0 flex-1 auto-rows-min grid-cols-2 gap-1.5 overflow-y-auto pr-0.5"
        role="group"
        aria-label="Вид операции"
      >
        {TILES.map(({ kind, icon: Icon, hint }) => (
          <button
            key={kind}
            type="button"
            tabIndex={-1}
            title={hint}
            onClick={() => dispatch({ type: 'CAM_OP', kind })}
            className={cn(
              'flex items-center gap-2 rounded-md border-2 border-b-[3px] bg-card px-2 text-left transition-colors hover:bg-accent active:translate-y-px active:border-b active:bg-accent',
              touch ? 'min-h-16' : 'min-h-11',
            )}
          >
            <Icon className="size-5 shrink-0 text-primary" aria-hidden="true" />
            <span className="flex min-w-0 flex-col">
              <span className="text-xs font-bold leading-tight">{KIND_LABEL[kind]}</span>
              <span className="line-clamp-2 text-[10px] leading-tight text-muted-foreground">{hint}</span>
            </span>
          </button>
        ))}
      </div>
    </div>
  )
}
