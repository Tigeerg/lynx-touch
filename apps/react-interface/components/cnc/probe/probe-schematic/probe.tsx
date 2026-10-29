export function Probe({
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
