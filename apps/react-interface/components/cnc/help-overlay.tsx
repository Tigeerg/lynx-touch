import { CircleHelp } from 'lucide-react'
import { cn } from '@/lib/utils'
import { Overlay } from './overlay'
import { Keycap } from './primitives'

const GROUPS: { title: string; items: [string[], string][] }[] = [
  {
    title: 'Управление циклом',
    items: [
      [['Enter'], 'Пуск цикла / продолжить'],
      [['Space'], 'Стоп подачи'],
      [['Esc'], 'Сброс (RESET)'],
      [['Shift', 'Esc'], 'Аварийный стоп (вкл / отпустить)'],
    ],
  },
  {
    title: 'Режимы и экраны',
    items: [
      [['M'], 'Следующий режим (Shift+M — предыдущий)'],
      [['1', '…', '5'], 'Вкладки: Работа, Смещения, Инструмент, Сообщения, Сервис'],
      [['V'], 'Вид траектории: ISO / XY / XZ'],
      [['P'], 'Показать / скрыть траекторию'],
      [['F1', '…', 'F8'], 'Функциональные клавиши текущего экрана'],
      [['H'], 'Открыть / закрыть справку (также Сервис → Справка)'],
    ],
  },
  {
    title: 'Перемещение осей',
    items: [
      [['X', 'Y', 'Z', 'A'], 'Выбор оси'],
      [['←', '→'], 'Шаг по оси −/+ (РУЧНОЙ, МАХОВИК)'],
      [['↑', '↓'], 'Курсор в таблицах и программе'],
      [['PgUp', 'PgDn'], 'Курсор на 10 строк'],
    ],
  },
  {
    title: 'Коррекции',
    items: [
      [['−', '='], 'Подача F: −10% / +10%'],
      [['[', ']'], 'Шпиндель S: −10% / +10%'],
      [['9', '0'], 'Ускоренный ход: 5 / 25 / 50 / 100%'],
    ],
  },
]

export function HelpOverlay({ onClose }: { onClose: () => void }) {
  return (
    <Overlay onClose={onClose} labelledBy="help-title" className="max-h-full w-full max-w-4xl overflow-y-auto border">
      <header className="flex items-center gap-2 border-b bg-muted px-4 py-3">
        <CircleHelp className="size-5 text-primary" aria-hidden="true" />
        <h2 id="help-title" className="text-base font-bold">
          Раскладка пульта
        </h2>
        <span className="ml-auto flex items-center gap-1.5 text-xs text-muted-foreground">
          Закрыть <Keycap>H</Keycap> <Keycap>Esc</Keycap>
        </span>
      </header>
      <HelpGroups />
    </Overlay>
  )
}

export function HelpGroups({ className }: { className?: string }) {
  return (
    <div className={cn('grid grid-cols-2 gap-4 p-4', className)}>
      {GROUPS.map((g) => (
        <section key={g.title}>
          <h3 className="mb-2 text-xs font-semibold uppercase tracking-wider text-muted-foreground">{g.title}</h3>
          <ul className="flex flex-col gap-2">
            {g.items.map(([keys, desc]) => (
              <li key={desc} className="flex items-center gap-3 text-sm">
                <span className="flex w-28 shrink-0 flex-wrap items-center gap-1">
                  {keys.map((k) =>
                    k === '…' ? (
                      <span key={k} className="text-muted-foreground">
                        …
                      </span>
                    ) : (
                      <Keycap key={k}>{k}</Keycap>
                    ),
                  )}
                </span>
                <span>{desc}</span>
              </li>
            ))}
          </ul>
        </section>
      ))}
    </div>
  )
}
