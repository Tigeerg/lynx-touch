import { useId } from 'react'
import { PROBE_BALL_R, probeLocalPosition, TOUCH_MS, type P3, type ProbeCycle } from '@/lib/cnc/probe'
import { cn } from '@/lib/utils'
import { H, W, Badge, Feature, Grid, type TouchState } from './parts'
import { Probe } from './probe'
import { Gizmo } from './gizmo'

export function ProbeSchematic({
  cycle,
  elapsed = 0,
  running = false,
  done = false,
  mini = false,
  className,
}: {
  cycle: ProbeCycle
  elapsed?: number
  running?: boolean
  done?: boolean
  mini?: boolean
  className?: string
}) {
  const uid = useId().replace(/:/g, '')
  const v = cycle.view
  const side = v.plane === 'XZ'
  const sx = (q: P3) => v.ox + q.x * v.k
  const sy = (q: P3) => v.oy - (side ? q.z : q.y) * v.k

  const [a0, a1, b0, b1] = v.stock
  const rect = { x: v.ox + a0 * v.k, y: v.oy - b1 * v.k, w: (a1 - a0) * v.k, h: (b1 - b0) * v.k }

  const firstTouch = cycle.touches[0]
  const labelRight = firstTouch.axis === 'X' && firstTouch.sign < 0

  const live = running ? probeLocalPosition(cycle, elapsed) : null
  const probePos = live?.pos ?? cycle.start
  const touchState = (i: number): TouchState => {
    if (done) return 'done'
    if (!running) return 'pending'
    const current = Math.floor(elapsed / TOUCH_MS)
    return i < current ? 'done' : i === current ? 'active' : 'pending'
  }
  const ballR = Math.max(mini ? 9 : 7, PROBE_BALL_R * v.k * 2)

  const tone: Record<TouchState, string> = {
    pending: 'stroke-muted-foreground fill-muted-foreground',
    active: 'stroke-primary fill-primary',
    done: 'stroke-success fill-success',
  }

  return (
    <svg
      viewBox={`0 0 ${W} ${H}`}
      className={cn('block h-full w-full', className)}
      preserveAspectRatio="xMidYMid meet"
      role={mini ? 'presentation' : 'img'}
      aria-label={mini ? undefined : `Схема цикла «${cycle.name}»`}
    >
      <defs>
        <pattern id={`hatch-${uid}`} width="10" height="10" patternUnits="userSpaceOnUse" patternTransform="rotate(45)">
          <line x1="0" y1="0" x2="0" y2="10" className="stroke-foreground/15" strokeWidth="3" />
        </pattern>
        {(['pending', 'active', 'done'] as const).map((st) => (
          <marker
            key={st}
            id={`arrow-${st}-${uid}`}
            viewBox="0 0 10 10"
            refX="8"
            refY="5"
            markerWidth="5"
            markerHeight="5"
            orient="auto-start-reverse"
          >
            <path d="M0,0 L10,5 L0,10 z" className={tone[st]} />
          </marker>
        ))}
      </defs>

      {!mini && <Grid />}

      <rect
        x={rect.x}
        y={rect.y}
        width={rect.w}
        height={rect.h}
        className="fill-muted stroke-foreground"
        strokeWidth={mini ? 4 : 2}
      />
      <rect x={rect.x} y={rect.y} width={rect.w} height={rect.h} fill={`url(#hatch-${uid})`} />
      {v.hole && (
        <circle
          cx={v.ox + v.hole.cx * v.k}
          cy={v.oy - v.hole.cy * v.k}
          r={v.hole.r * v.k}
          className="fill-card stroke-foreground"
          strokeWidth={mini ? 4 : 2}
        />
      )}

      <Feature cycle={cycle} rect={rect} mini={mini} />

      {cycle.touches.map((t, i) => {
        const st = touchState(i)
        const zTouch = t.axis === 'Z' && !side
        const fx = sx(t.from)
        const fy = sy(t.from)
        const tx = sx(t.to)
        const ty = sy(t.to)
        return (
          <g key={i}>
            {zTouch ? (
              <circle
                cx={fx}
                cy={fy}
                r={mini ? 14 : 11}
                className={cn(tone[st], 'fill-none')}
                strokeWidth={mini ? 5 : 2.5}
                strokeDasharray="4 3"
              />
            ) : (
              <line
                x1={fx}
                y1={fy}
                x2={tx}
                y2={ty}
                className={tone[st]}
                strokeWidth={mini ? 6 : 2.5}
                strokeDasharray={st === 'pending' && !mini ? '6 4' : undefined}
                markerEnd={`url(#arrow-${st}-${uid})`}
              />
            )}
            {!mini && (
              <Badge
                x={fx}
                y={fy}
                n={i + 1}
                label={`${t.axis}${t.sign > 0 ? '+' : '−'}`}
                state={st}
                offset={zTouch ? 18 : 0}
              />
            )}
          </g>
        )
      })}

      {!mini && (
        <g>
          <circle
            cx={sx(cycle.start)}
            cy={sy(cycle.start)}
            r="5"
            className="fill-none stroke-foreground/50"
            strokeWidth="1.5"
            strokeDasharray="2 2"
          />
          <text
            x={sx(cycle.start) + (labelRight ? ballR + 10 : -(ballR + 10))}
            y={sy(cycle.start) + 4}
            textAnchor={labelRight ? 'start' : 'end'}
            className="fill-muted-foreground font-mono text-[11px]"
          >
            {'старт'}
          </text>
        </g>
      )}

      <Probe x={sx(probePos)} y={sy(probePos)} r={ballR} side={side} touching={!!live?.touching} mini={mini} />

      {!mini && <Gizmo side={side} />}
      {!mini && (
        <text
          x="14"
          y="22"
          className="fill-muted-foreground font-sans text-[12px] font-semibold uppercase tracking-wider"
        >
          {side ? 'Вид сбоку · XZ' : 'Вид сверху · XY'}
        </text>
      )}
    </svg>
  )
}
