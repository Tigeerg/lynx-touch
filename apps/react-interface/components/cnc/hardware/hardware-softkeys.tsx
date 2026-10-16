'use client'

import type { Softkey } from '@/lib/cnc/softkeys'
import { HwKey } from './hw-parts'

export function HardwareSoftkeys({ keys, onPress }: { keys: (Softkey | null)[]; onPress: (k: Softkey) => void }) {
  return (
    <div
      role="toolbar"
      aria-label="Физические функциональные клавиши F1–F8"
      className="grid w-full shrink-0 grid-cols-8 justify-items-center gap-2 rounded-md bg-hw-plate px-2 pb-2 pt-4 shadow-inner ring-2 ring-hw-edge"
    >
      {keys.map((k, i) => (
        <HwKey
          key={i}
          label={`F${i + 1}`}
          title={k ? k.label : `F${i + 1} — не назначена`}
          lit={k?.active ?? false}
          tone="success"
          disabled={!k || k.disabled}
          onClick={() => k && onPress(k)}
          className="font-mono"
        />
      ))}
    </div>
  )
}
