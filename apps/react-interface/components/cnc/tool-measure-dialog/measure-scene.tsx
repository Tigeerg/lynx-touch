// The sensor scene: the spindle with the tool above the TOOL SETTER, drawn to the frame the
// reducer produces (depth, trigger, phase). Pure geometry, no machine state of its own.
import { useId } from 'react'
import type { MeasureFrame } from '@/lib/cnc/tool-measure'
import { cn } from '@/lib/utils'
import { SceneDefs } from './scene-defs'

const TRAVEL = 92

export function MeasureScene({ frame, length }: { frame: MeasureFrame; length: number | null }) {
  const uid = useId().replace(/:/g, '')
  const dy = frame.depth * TRAVEL
  const moving = frame.phase === 'rapid' || frame.phase === 'fine' || frame.phase === 'retract'
  const down = frame.phase === 'rapid' || frame.phase === 'fine'
  const plunge = frame.triggered ? 3 : 0
  const tipRest = 172
  const contactY = tipRest + TRAVEL

  return (
    <svg
      viewBox="0 0 360 340"
      className="block h-full max-h-[22rem] w-full"
      role="img"
      aria-label="Шпиндель с инструментом и измерительный датчик"
    >
      <SceneDefs uid={uid} />

      <g className="stroke-border" strokeWidth="0.5">
        {Array.from({ length: 17 }, (_, i) => (
          <line key={`gx${i}`} x1={i * 22.5} y1="0" x2={i * 22.5} y2="340" />
        ))}
        {Array.from({ length: 16 }, (_, i) => (
          <line key={`gy${i}`} x1="0" y1={i * 22.5} x2="360" y2={i * 22.5} />
        ))}
      </g>

      <line
        x1="180"
        y1="6"
        x2="180"
        y2="334"
        className="stroke-foreground/40"
        strokeWidth="1"
        strokeDasharray="8 4 2 4"
      />

      <g>
        <line
          x1="40"
          y1={contactY}
          x2="132"
          y2={contactY}
          className="stroke-foreground/60"
          strokeWidth="1"
          strokeDasharray="4 3"
        />
        <text x="40" y={contactY - 6} className="fill-muted-foreground font-sans text-[10px]">
          Точка срабатывания
        </text>
      </g>

      <g transform={`translate(0 ${dy})`} style={{ transition: 'transform 60ms linear' }}>
        <rect
          x="132"
          y="6"
          width="96"
          height="46"
          fill={`url(#metal-${uid})`}
          className="stroke-foreground/70"
          strokeWidth="1.5"
        />
        <rect x="132" y="6" width="96" height="9" className="fill-foreground/30" />
        <line x1="140" y1="34" x2="220" y2="34" className="stroke-foreground/30" strokeWidth="1" />
        <path
          d="M148,52 L212,52 L202,84 L158,84 Z"
          fill={`url(#metal-${uid})`}
          className="stroke-foreground/70"
          strokeWidth="1.5"
        />
        <rect
          x="162"
          y="84"
          width="36"
          height="22"
          fill={`url(#dark-${uid})`}
          className="stroke-foreground/80"
          strokeWidth="1.5"
        />
        <g className="stroke-background/50" strokeWidth="1.5">
          <line x1="170" y1="86" x2="170" y2="104" />
          <line x1="180" y1="86" x2="180" y2="104" />
          <line x1="190" y1="86" x2="190" y2="104" />
        </g>
        <rect
          x="173"
          y="106"
          width="14"
          height="26"
          fill={`url(#metal-${uid})`}
          className="stroke-foreground/70"
          strokeWidth="1.2"
        />
        <path
          d={`M167,132 L193,132 L193,168 L180,${tipRest} L167,168 Z`}
          fill={`url(#gold-${uid})`}
          className="stroke-foreground/80"
          strokeWidth="1.5"
        />
        <g className="stroke-warning-foreground/60" strokeWidth="1.5">
          <line x1="167" y1="140" x2="193" y2="150" />
          <line x1="167" y1="150" x2="193" y2="160" />
          <line x1="167" y1="160" x2="193" y2="170" />
        </g>

        <g className="stroke-foreground" strokeWidth="1">
          <line x1="232" y1="106" x2="262" y2="106" strokeDasharray="3 2" />
          <line x1="232" y1={tipRest} x2="262" y2={tipRest} strokeDasharray="3 2" />
          <line
            x1="256"
            y1="108"
            x2="256"
            y2={tipRest - 2}
            markerStart={`url(#arr-${uid})`}
            markerEnd={`url(#arr-${uid})`}
          />
        </g>
        <text x="262" y="142" className="fill-foreground font-sans text-[11px] font-semibold">
          Длина
        </text>
        <text x="262" y="156" className="fill-foreground font-sans text-[11px] font-semibold">
          инструмента
        </text>
        {length !== null && (
          <text x="262" y="172" className="fill-success font-mono text-[12px] font-bold">
            {length.toFixed(3)}
          </text>
        )}
      </g>

      {moving && (
        <g className={down ? 'fill-primary' : 'fill-success'} transform={`translate(104 ${100 + dy * 0.6})`}>
          {down ? (
            <path d="M0,0 L14,0 L14,16 L22,16 L7,32 L-8,16 L0,16 Z" />
          ) : (
            <path d="M7,0 L22,16 L14,16 L14,32 L0,32 L0,16 L-8,16 Z" />
          )}
        </g>
      )}

      <g>
        <rect
          x="140"
          y="312"
          width="80"
          height="14"
          className="fill-foreground/70 stroke-foreground"
          strokeWidth="1.5"
        />
        <path
          d="M150,312 L150,296 L210,296 L210,312 Z"
          fill={`url(#dark-${uid})`}
          className="stroke-foreground"
          strokeWidth="1.5"
        />
        <rect
          x="168"
          y="278"
          width="24"
          height="18"
          fill={`url(#metal-${uid})`}
          className="stroke-foreground"
          strokeWidth="1.5"
        />
        <rect
          x="160"
          y={contactY + plunge}
          width="40"
          height="8"
          className={cn('stroke-foreground', frame.triggered ? 'fill-warning' : 'fill-muted-foreground')}
          strokeWidth="1.5"
        />
        <rect
          x="224"
          y="300"
          width="22"
          height="12"
          className="fill-foreground/60 stroke-foreground"
          strokeWidth="1.2"
        />
        <circle cx="235" cy="306" r="3" className={frame.triggered ? 'fill-warning' : 'fill-foreground/30'} />
      </g>

      {frame.triggered && (
        <g>
          <circle
            cx="180"
            cy={contactY + 4}
            r="22"
            className="fill-none stroke-warning"
            strokeWidth="2.5"
            strokeDasharray="5 4"
          />
          <circle cx="180" cy={contactY + 4} r="34" className="fill-none stroke-warning/50" strokeWidth="1.5" />
          <text
            x="180"
            y={contactY + 52}
            textAnchor="middle"
            className="fill-warning font-mono text-[12px] font-bold uppercase"
          >
            Датчик сработал
          </text>
        </g>
      )}

      <text
        x="180"
        y="334"
        textAnchor="middle"
        className="fill-muted-foreground font-sans text-[10px] uppercase tracking-wider"
      >
        Измерительный датчик
      </text>
    </svg>
  )
}
