import { H } from './parts'

export function Gizmo({ side }: { side: boolean }) {
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
