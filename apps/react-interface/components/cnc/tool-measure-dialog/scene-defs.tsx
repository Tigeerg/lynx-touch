// The gradients and the direction marker of the sensor scene: the metal of the spindle and holder,
// the gold of the probe tip, the dark grip and the arrow that marks the tool length. `uid` keeps
// the ids of one scene apart from another in the same document.
export function SceneDefs({ uid }: { uid: string }) {
  return (
    <defs>
      <linearGradient id={`metal-${uid}`} x1="0" x2="1" y1="0" y2="0">
        <stop offset="0" style={{ stopColor: 'var(--muted-foreground)', stopOpacity: 0.55 }} />
        <stop offset="0.35" style={{ stopColor: 'var(--card)', stopOpacity: 1 }} />
        <stop offset="0.6" style={{ stopColor: 'var(--muted-foreground)', stopOpacity: 0.8 }} />
        <stop offset="1" style={{ stopColor: 'var(--muted-foreground)', stopOpacity: 0.4 }} />
      </linearGradient>
      <linearGradient id={`gold-${uid}`} x1="0" x2="1" y1="0" y2="0">
        <stop offset="0" style={{ stopColor: 'var(--warning)', stopOpacity: 0.7 }} />
        <stop offset="0.45" style={{ stopColor: 'var(--warning)', stopOpacity: 1 }} />
        <stop offset="1" style={{ stopColor: 'var(--warning)', stopOpacity: 0.55 }} />
      </linearGradient>
      <linearGradient id={`dark-${uid}`} x1="0" x2="1" y1="0" y2="0">
        <stop offset="0" style={{ stopColor: 'var(--foreground)', stopOpacity: 0.55 }} />
        <stop offset="0.5" style={{ stopColor: 'var(--foreground)', stopOpacity: 0.85 }} />
        <stop offset="1" style={{ stopColor: 'var(--foreground)', stopOpacity: 0.5 }} />
      </linearGradient>
      <marker
        id={`arr-${uid}`}
        viewBox="0 0 10 10"
        refX="8"
        refY="5"
        markerWidth="6"
        markerHeight="6"
        orient="auto-start-reverse"
      >
        <path d="M0,0 L10,5 L0,10 z" className="fill-foreground" />
      </marker>
    </defs>
  )
}
