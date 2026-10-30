// The common fields of a new operation (PLAN-QUICKPROG, 7.4).
//
// The fields every kind opens with: the tool, the rates of the table, the coolant, the material and
// the frame of the pass. Kept apart from `defaults.ts` so a kind that has its own defaults module
// (the probe) reads the same frame as the kinds of the switch there, and neither pulls the other.
import type { DrillBody, Material, ToolRef } from './ops'
import type { NewOperation } from './operation-init'
import { cuttingFor } from './feeds'
import { DEFAULT_DEPTH_MM, DEFAULT_STEP_DOWN_MM } from './defaults'

/** The fields every kind of a new operation shares, with the depth and step of the console. */
export const common = (init: NewOperation, tool: ToolRef, material: Material, body: DrillBody | null) => ({
  id: init.id,
  enabled: true,
  tool,
  cutting: cuttingFor(material, tool.diameterMm, body),
  coolant: 'flood' as const,
  material,
  topZ: init.topZ,
  depthMm: init.depthMm ?? DEFAULT_DEPTH_MM,
  stepDownMm: init.stepDownMm ?? DEFAULT_STEP_DOWN_MM,
  notes: '',
})
