import type { ReactNode } from 'react'
import { cn } from '@/lib/utils'

// Общая оболочка модальных окон панели: затемнение, центрирование и закрытие по
// клику вне окна. Все диалоги рисуются через неё, иначе поведение (z-слой,
// закрытие по фону) расходится от копии к копии.
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
