import { INI_FILE, MESA_BOARD_PARAM, MESA_IPADDR_PARAM, MESA_PARAMS, paramValue } from '@/lib/cnc/service-data'
import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'

const SYSTEM_INFO = [
  ['LinuxCNC', '2.9.4'],
  ['Ядро RT', '6.1.0-rt PREEMPT'],
  ['Servo-поток', '1.000 мс'],
  ['Джиттер', '11.8 мкс'],
  ['Интерфейс', 'Mesa 7i96S'],
  ['IP-адрес', '192.168.1.121'],
]

export function SystemInfo() {
  return <InfoStrip items={SYSTEM_INFO.map(([label, value]) => ({ label, value }))} />
}

type InfoItem = { label: string; value: string; tone?: 'ok' | 'warn' }

export function InfoStrip({ items }: { items: InfoItem[] }) {
  return (
    <dl className="grid shrink-0 grid-cols-3 gap-px xl:grid-cols-6 border-b bg-border">
      {items.map(({ label, value, tone }) => (
        <div key={label} className="bg-card px-3 py-2">
          <dt className="text-[10px] font-semibold uppercase tracking-wider text-muted-foreground">{label}</dt>
          <dd className="flex items-center gap-1.5 font-mono text-sm font-bold">
            {tone && (
              <span
                aria-hidden="true"
                className={cn('size-2 rounded-full', tone === 'ok' ? 'bg-success' : 'bg-warning')}
              />
            )}
            {value}
          </dd>
        </div>
      ))}
    </dl>
  )
}

export function linuxcncInfo(s: MachineState): InfoItem[] {
  const on = s.run !== 'ESTOP'
  return [
    { label: 'Конфигурация', value: INI_FILE },
    { label: 'Task', value: 'milltask' },
    { label: 'Интерпретатор', value: 'rs274ngc' },
    { label: 'Motion', value: on ? 'ENABLED' : 'DISABLED', tone: on ? 'ok' : 'warn' },
    { label: 'Файл параметров', value: 'linuxcnc.var' },
    { label: 'Состояние task', value: s.run === 'RUN' ? 'EXEC' : s.run === 'HOLD' ? 'PAUSED' : 'IDLE' },
  ]
}

/** The text of an option row of the catalogue, looked up by key rather than by row position. */
function optionText(s: MachineState, id: string): string {
  const def = MESA_PARAMS.find((d) => d.id === id)
  return def?.options?.[paramValue(s.params, id)] ?? '—'
}

export function mesaInfo(s: MachineState): InfoItem[] {
  const linked = s.run !== 'ESTOP'
  return [
    { label: 'Плата', value: optionText(s, MESA_BOARD_PARAM) },
    { label: 'Драйвер', value: 'hm2_eth' },
    { label: 'IP-адрес', value: optionText(s, MESA_IPADDR_PARAM) },
    { label: 'Связь', value: linked ? '100 Мбит/с' : 'Нет', tone: linked ? 'ok' : 'warn' },
    { label: 'Прошивка', value: '7i96s_d.bin' },
    { label: 'Watchdog', value: linked ? 'OK · 0 сраб.' : 'BITE', tone: linked ? 'ok' : 'warn' },
  ]
}
