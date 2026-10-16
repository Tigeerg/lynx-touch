'use client'

import { useEffect, useState, type ReactNode } from 'react'

export function FhdStage({ width, height, children }: { width: number; height: number; children: ReactNode }) {
  const [scale, setScale] = useState<number | null>(null)

  useEffect(() => {
    const fit = () => setScale(Math.min(window.innerWidth / width, window.innerHeight / height))
    fit()
    window.addEventListener('resize', fit)
    return () => window.removeEventListener('resize', fit)
  }, [width, height])

  return (
    <div className="fixed inset-0 overflow-hidden bg-hw-edge">
      <div
        className="absolute left-1/2 top-1/2 origin-center"
        style={{
          width,
          height,
          transform: `translate(-50%, -50%) scale(${scale ?? 1})`,
          visibility: scale === null ? 'hidden' : 'visible',
        }}
      >
        {children}
      </div>
    </div>
  )
}
