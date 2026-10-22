import type { ReactNode } from 'react'
import { cn } from '@/lib/utils'

// The common shell for the panel's modal windows: dimming, centring and closing on a click
// outside. Every dialog is drawn through it, otherwise the behaviour (z-layer, closing on the
// backdrop) drifts from copy to copy.
export function Overlay({
  onClose,
  label,
  labelledBy,
  className,
  children,
}: {
  onClose: () => void
  label?: string
  labelledBy?: string
  className: string
  children: ReactNode
}) {
  return (
    <div className="absolute inset-0 z-50 flex items-center justify-center bg-foreground/30 p-3" onClick={onClose}>
      <section
        role="dialog"
        aria-modal="true"
        aria-label={label}
        aria-labelledby={labelledBy}
        className={cn('overflow-hidden rounded-md bg-card shadow-xl', className)}
        onClick={(e) => e.stopPropagation()}
      >
        {children}
      </section>
    </div>
  )
}
