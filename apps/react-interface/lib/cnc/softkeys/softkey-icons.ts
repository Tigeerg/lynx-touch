// The icons the softkeys and the service screen draw. Kept beside the label tables so the icon
// imports do not drown the slot logic in softkey-slots.ts (CLAUDE.md, section 2).
import {
  CircuitBoard,
  Cable,
  CircleHelp,
  FileCog,
  MonitorCog,
  Route,
  SlidersHorizontal,
  Axis3d,
  Workflow,
  type LucideIcon,
} from 'lucide-react'
import type { ServiceSection } from '../types'

export const SERVICE_ICON: Record<ServiceSection, LucideIcon> = {
  system: MonitorCog,
  linuxcnc: FileCog,
  motion: Route,
  joints: Axis3d,
  pid: SlidersHorizontal,
  mesa: CircuitBoard,
  hal: Cable,
  ladder: Workflow,
  help: CircleHelp,
}
