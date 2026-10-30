'use client'

// The fifteen kinds of the quick program as tiles (PLAN-QUICKPROG, 7.4, C5 and 7.6): a word and a
// lucide icon, never an icon alone, so P1 (keys and touch, no guessing from a picture) reads the
// same as P3. The list is `TILES` here and exported, so a test counts the tiles without a DOM
// renderer.
//
// Fifteen tiles in one two-column grid would be eight rows: 8 * 44 + 7 * 6 = 394 px of the 439 px
// panel body, but 8 * 64 + 7 * 6 = 554 px on the touch, where the column is 439 px tall: the touch
// would scroll the whole set every time. So the picker is four categories of three or four tiles
// (`CATEGORIES`): two rows of 44 px are 94 px on the panel and two rows of 64 px are 134 px on the
// touch, and the active tab starts on the kind of the spot the operator came for. The grid still
// scrolls inside the column, so a grown category can never grow the card.
import {
  CircleDashed,
  Crosshair,
  Drill,
  FileUp,
  Grid3x3,
  Layers,
  Minus,
  PenLine,
  Slice,
  Terminal,
  Wrench,
  type LucideIcon,
} from 'lucide-react'
import { useState, type Dispatch } from 'react'
import { cn } from '@/lib/utils'
import type { CamAction } from '@/lib/cnc/cam/cam'
import type { OperationKind } from '@/lib/cnc/cam/ops'
import { useUiMode } from '../ui-mode-context'
import { KIND_LABEL } from './cam-labels'

/** One tile of the picker: the kind, its icon and the one line under the word. */
export interface Tile {
  kind: OperationKind
  icon: LucideIcon
  hint: string
}

/** One category of tiles: the word of the segment and its three or four tiles. */
export interface Category {
  id: string
  label: string
  tiles: Tile[]
}

/**
 * The four categories in the order of the work: the pockets, the holes, the outline and the
 * special kinds. A category holds at most four tiles, so its grid is two rows on both variants.
 */
export const CATEGORIES: Category[] = [
  {
    id: 'pockets',
    label: 'Карманы',
    tiles: [
      { kind: 'face', icon: Layers, hint: 'Снять слой с верха заготовки' },
      { kind: 'pocket', icon: Grid3x3, hint: 'Прямоугольный карман зигзагом' },
      { kind: 'circle-pocket', icon: CircleDashed, hint: 'Круглый карман спиралью' },
      { kind: 'pocket-island', icon: Grid3x3, hint: 'Обход острова внутри кармана' },
    ],
  },
  {
    id: 'holes',
    label: 'Сверление',
    tiles: [
      { kind: 'drill', icon: Drill, hint: 'Отверстие, сетка или круг' },
      { kind: 'counterbore', icon: CircleDashed, hint: 'Гнездо под головку крепежа, G82 с паузой' },
      { kind: 'tap', icon: Wrench, hint: 'Резьба G84, станок должен держать жёсткое нарезание' },
      { kind: 'thread', icon: Wrench, hint: 'Внутренняя или наружная резьба фрезой по спирали' },
    ],
  },
  {
    id: 'outline',
    label: 'Контур',
    tiles: [
      { kind: 'contour', icon: PenLine, hint: 'Профиль по точкам, замкнутый или открытый' },
      { kind: 'slot', icon: Minus, hint: 'Прямой паз с врезанием по ширине' },
      { kind: 'chamfer', icon: Slice, hint: 'Снятие кромки по прямой' },
      { kind: 'engrave', icon: PenLine, hint: 'Надпись встроенным шрифтом по контуру' },
    ],
  },
  {
    id: 'special',
    label: 'Спец',
    tiles: [
      { kind: 'rest', icon: Layers, hint: 'Снять то, что не взяла предыдущая операция' },
      { kind: 'custom', icon: Terminal, hint: 'Готовый блок G-кода без проверки геометрии' },
      { kind: 'probe', icon: Crosshair, hint: 'Обмер между операциями щупом' },
    ],
  },
]

/** Every tile in category order: the order of the words in the tests and in the Q4 list. */
export const TILES: Tile[] = CATEGORIES.flatMap((category) => category.tiles)

/** The category of a kind, for the segment the picker opens on after a cancel. */
export const categoryOf = (kind: OperationKind): string =>
  CATEGORIES.find((category) => category.tiles.some((tile) => tile.kind === kind))?.id ?? CATEGORIES[0].id

export function OperationPicker({
  dispatch,
  onDxf,
  start,
}: {
  dispatch: Dispatch<CamAction>
  /** The DXF import button; the dialog lives in `dxf-dialog.tsx`. */
  onDxf: () => void
  /** The category the picker opens on: the last kind the operator worked with. */
  start?: OperationKind
}) {
  const touch = useUiMode()?.mode === 'touch'
  const [tab, setTab] = useState(() => (start === undefined ? CATEGORIES[0].id : categoryOf(start)))
  const active = CATEGORIES.find((category) => category.id === tab) ?? CATEGORIES[0]
  return (
    <div className="flex min-h-0 flex-1 flex-col gap-1.5">
      <div role="tablist" aria-label="Раздел операций" className="flex shrink-0 overflow-hidden rounded-md border">
        {CATEGORIES.map((category) => (
          <button
            key={category.id}
            type="button"
            role="tab"
            tabIndex={-1}
            aria-selected={category.id === active.id}
            onClick={() => setTab(category.id)}
            className={cn(
              'flex-1 text-[11px] font-semibold transition-colors',
              touch ? 'min-h-11' : 'min-h-8',
              category.id === active.id
                ? 'bg-primary text-primary-foreground'
                : 'bg-card text-muted-foreground hover:bg-accent',
            )}
          >
            {category.label}
          </button>
        ))}
      </div>
      <div
        className="grid min-h-0 flex-1 auto-rows-min grid-cols-2 gap-1.5 overflow-y-auto pr-0.5"
        role="group"
        aria-label="Вид операции"
      >
        {active.tiles.map(({ kind, icon: Icon, hint }) => (
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
      <button
        type="button"
        tabIndex={-1}
        onClick={onDxf}
        className={cn(
          'flex shrink-0 items-center justify-center gap-1.5 rounded-md border-2 bg-card text-xs font-bold transition-colors hover:bg-accent',
          touch ? 'min-h-11' : 'min-h-9',
        )}
      >
        <FileUp className="size-4 shrink-0 text-primary" aria-hidden="true" />
        Импорт DXF
      </button>
    </div>
  )
}
