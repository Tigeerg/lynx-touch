import { useId } from 'react'
import { PROBE_BALL_R, probeLocalPosition, TOUCH_MS, type P3, type ProbeCycle } from '@/lib/cnc/probe'
import { cn } from '@/lib/utils'

const W = 400
const H = 300

type TouchState = 'pending' | 'active' | 'done'

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

function Grid() {
  const lines = []
  for (let x = 20; x < W; x += 20) lines.push(<line key={`x${x}`} x1={x} y1={0} x2={x} y2={H} />)
  for (let y = 20; y < H; y += 20) lines.push(<line key={`y${y}`} x1={0} y1={y} x2={W} y2={y} />)
  return (
    <g className="stroke-border" strokeWidth="0.5">
      {lines}
    </g>
  )
}

function Feature({
  cycle,
  rect,
  mini,
}: {
  cycle: ProbeCycle
  rect: { x: number; y: number; w: number; h: number }
  mini: boolean
}) {
  const v = cycle.view
  const sw = mini ? 8 : 4
  const cls = 'stroke-primary'
  const edge = (x1: number, y1: number, x2: number, y2: number) => (
    <g className={cls} strokeLinecap="round">
      {!mini && <line x1={x1} y1={y1} x2={x2} y2={y2} strokeWidth={sw * 3} className="stroke-primary/15" />}
      <line x1={x1} y1={y1} x2={x2} y2={y2} strokeWidth={sw} />
    </g>
  )
  switch (v.feature) {
    case 'edge-xp':
      return edge(rect.x, rect.y, rect.x, rect.y + rect.h)
    case 'edge-xn':
      return edge(rect.x + rect.w, rect.y, rect.x + rect.w, rect.y + rect.h)
    case 'edge-yp':
      return edge(rect.x, rect.y + rect.h, rect.x + rect.w, rect.y + rect.h)
    case 'edge-yn':
      return edge(rect.x, rect.y, rect.x + rect.w, rect.y)
    case 'top':
      return <line x1={rect.x} y1={rect.y} x2={rect.x + rect.w} y2={rect.y} className={cls} strokeWidth={sw} />
    case 'corner': {
      const cx = rect.x
      const cy = rect.y + rect.h
      return (
        <path
          d={`M${cx},${cy - 60} L${cx},${cy} L${cx + 60},${cy}`}
          className={cn(cls, 'fill-none')}
          strokeWidth={sw}
          strokeLinejoin="round"
        />
      )
    }
    case 'center': {
      const cx = v.hole ? v.ox + v.hole.cx * v.k : rect.x + rect.w / 2
      const cy = v.hole ? v.oy - v.hole.cy * v.k : rect.y + rect.h / 2
      const s = mini ? 30 : 16
      return (
        <g className={cls} strokeWidth={mini ? 6 : 2.5}>
          <line x1={cx - s} y1={cy} x2={cx + s} y2={cy} />
          <line x1={cx} y1={cy - s} x2={cx} y2={cy + s} />
          {!mini && <circle cx={cx} cy={cy} r="6" className="fill-none" />}
        </g>
      )
    }
  }
}

function Badge({
  x,
  y,
  n,
  label,
  state,
  offset,
}: {
  x: number
  y: number
  n: number
  label: string
  state: TouchState
  offset: number
}) {
  const bx = x - 14 - offset
  const by = y - 30
  const fill = {
    pending: 'fill-card stroke-muted-foreground',
    active: 'fill-primary stroke-primary',
    done: 'fill-success stroke-success',
  }[state]
  const text = state === 'pending' ? 'fill-foreground' : 'fill-primary-foreground'
  return (
    <g>
      <rect x={bx} y={by} width="44" height="20" rx="3" className={fill} strokeWidth="1.5" />
      <text x={bx + 22} y={by + 14} textAnchor="middle" className={cn(text, 'font-mono text-[12px] font-bold')}>
        {`${n}·${label}`}
      </text>
    </g>
  )
}

function Probe({
  x,
  y,
  r,
  side,
  touching,
  mini,
}: {
  x: number
  y: number
  r: number
  side: boolean
  touching: boolean
  mini: boolean
}) {
  if (side) {
    return (
      <g>
        <rect x={x - 3} y={y - 70} width="6" height={70 - r} className="fill-foreground/70" />
        <rect x={x - 22} y={y - 110} width="44" height="42" rx="4" className="fill-foreground/80" />
        <circle
          cx={x}
          cy={y}
          r={r}
          className={touching ? 'fill-warning stroke-foreground' : 'fill-destructive stroke-foreground'}
          strokeWidth="1.5"
        />
      </g>
    )
  }
  return (
    <g>
      <circle
        cx={x}
        cy={y}
        r={r + (mini ? 8 : 9)}
        className="fill-foreground/10 stroke-foreground/60"
        strokeWidth={mini ? 3 : 1.5}
      />
      <circle
        cx={x}
        cy={y}
        r={r}
        className={touching ? 'fill-warning stroke-foreground' : 'fill-destructive stroke-foreground'}
        strokeWidth="1.5"
      />
      {touching && !mini && <circle cx={x} cy={y} r={r + 16} className="fill-none stroke-warning" strokeWidth="2" />}
    </g>
  )
}

function Gizmo({ side }: { side: boolean }) {
  const ox = 26
  const oy = H - 22
  return (
    <g className="stroke-foreground/70 fill-foreground/70" strokeWidth="2">
      <line x1={ox} y1={oy} x2={ox + 34} y2={oy} />
      <path d={`M${ox + 34},${oy - 4} L${ox + 42},${oy} L${ox + 34},${oy + 4} z`} />
      <line x1={ox} y1={oy} x2={ox} y2={oy - 34} />
      <path d={`M${ox - 4},${oy - 34} L${ox},${oy - 42} L${ox + 4},${oy - 34} z`} />
      <text x={ox + 46} y={oy + 4} className="stroke-none font-mono text-[12px] font-bold">
        X
      </text>
      <text x={ox - 4} y={oy - 46} className="stroke-none font-mono text-[12px] font-bold">
        {side ? 'Z' : 'Y'}
      </text>
    </g>
  )
}
