import type { ProbeCycle } from '@/lib/cnc/probe'
import { cn } from '@/lib/utils'

export const W = 400
export const H = 300

export type TouchState = 'pending' | 'active' | 'done'

export function Grid() {
  const lines = []
  for (let x = 20; x < W; x += 20) lines.push(<line key={`x${x}`} x1={x} y1={0} x2={x} y2={H} />)
  for (let y = 20; y < H; y += 20) lines.push(<line key={`y${y}`} x1={0} y1={y} x2={W} y2={y} />)
  return (
    <g className="stroke-border" strokeWidth="0.5">
      {lines}
    </g>
  )
}

export function Feature({
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

export function Badge({
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
