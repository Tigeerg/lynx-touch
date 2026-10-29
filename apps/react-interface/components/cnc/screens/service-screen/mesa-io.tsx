import type { MachineState } from '@/lib/cnc/types'
import { cn } from '@/lib/utils'

/** The Mesa terminal wiring: `addr: null` is an unwired terminal, not an empty address. */
type MesaTerminal = { terminal: string; addr: string | null }

const MESA_INPUTS: MesaTerminal[] = [
  { terminal: 'IN0', addr: 'I0.0' },
  { terminal: 'IN1', addr: 'I0.1' },
  { terminal: 'IN2', addr: 'I0.2' },
  { terminal: 'IN3', addr: 'I0.3' },
  { terminal: 'IN4', addr: 'I0.4' },
  { terminal: 'IN5', addr: 'I0.5' },
  { terminal: 'IN6', addr: null },
  { terminal: 'IN7', addr: null },
  { terminal: 'IN8', addr: null },
  { terminal: 'IN9', addr: null },
  { terminal: 'IN10', addr: null },
]

const MESA_OUTPUTS: MesaTerminal[] = [
  { terminal: 'OUT0', addr: 'Q0.0' },
  { terminal: 'OUT1', addr: 'Q0.1' },
  { terminal: 'OUT2', addr: 'Q0.2' },
  { terminal: 'OUT3', addr: 'Q0.3' },
  { terminal: 'OUT4', addr: 'Q0.4' },
  { terminal: 'OUT5', addr: 'Q0.5' },
]

function IoLed({ name, on }: { name: string; on: boolean }) {
  return (
    <span className="flex flex-col items-center gap-0.5">
      <span
        aria-label={`${name}: ${on ? 'вкл' : 'выкл'}`}
        className={cn('size-3.5 rounded-sm border', on ? 'border-success bg-success' : 'bg-muted')}
      />
      <span className="font-mono text-[10px] text-muted-foreground">{name}</span>
    </span>
  )
}

export function MesaIo({ state }: { state: MachineState }) {
  // The signals come from the last TICK scan (state.ladder.signals): scanning again here was a
  // second evaluateLadder in the same render as the one in the cycle reducer.
  const signals = state.ladder.signals
  return (
    <div className="flex shrink-0 items-center gap-6 border-b px-3 py-2">
      <div className="flex items-center gap-3">
        <span className="text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">Входы TB3</span>
        <div className="flex gap-2">
          {MESA_INPUTS.map(({ terminal, addr }) => (
            <IoLed key={terminal} name={terminal} on={addr !== null && !!signals[addr]} />
          ))}
        </div>
      </div>
      <div className="flex items-center gap-3">
        <span className="text-[11px] font-semibold uppercase tracking-wider text-muted-foreground">Выходы TB4</span>
        <div className="flex gap-2">
          {MESA_OUTPUTS.map(({ terminal, addr }) => (
            <IoLed key={terminal} name={terminal} on={addr !== null && !!signals[addr]} />
          ))}
        </div>
      </div>
      <span className="ml-auto font-mono text-[11px] text-muted-foreground">hm2_7i96s.0 · цикл чтения 142 мкс</span>
    </div>
  )
}
