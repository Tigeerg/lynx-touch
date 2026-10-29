// The probe cycle table: the wizard view geometry, the approach start points, the touches and the
// feature formula of every built-in cycle. Pure data, no JSX and no run loop: the run
// (probe.ts: probeLocalPosition, computeProbeResult) and the wizard read it through probe.ts.
import type { ProbeCycle, ProbeResultLine, P3 } from './probe'
import { PROBE_BALL_R } from './probe'

export const R = PROBE_BALL_R
export const p = (x: number, y: number, z = 0): P3 => ({ x, y, z })
export const mm = (v: number) => `${v.toFixed(3)} мм`

export const BOSS = { cx: 0.412, cy: -0.237, w: 60.018, h: 39.992 }
export const BORE = { cx: -0.318, cy: 0.274, d: 30.012 }

export const PROBE_CYCLES: ProbeCycle[] = [
  {
    id: 'edge-xp',
    group: 'edge',
    name: 'Кромка X+',
    short: 'Одно касание: щуп идёт в +X, заготовка справа от щупа',
    axes: ['X'],
    start: p(-10.384, 0),
    startHint: 'Слева от грани на 5–15 мм, шарик ниже верха заготовки на 3–5 мм',
    view: { plane: 'XY', ox: 165, oy: 150, k: 3, stock: [0, 65, -38, 38], feature: 'edge-xp' },
    touches: [{ axis: 'X', sign: 1, from: p(-10.384, 0), to: p(-R, 0) }],
    compute: (c) => ({
      feature: { x: c[0].x + R },
      info: [{ label: 'Контакт X', value: mm(c[0].x) }],
    }),
  },
  {
    id: 'edge-xn',
    group: 'edge',
    name: 'Кромка X−',
    short: 'Одно касание: щуп идёт в −X, заготовка слева от щупа',
    axes: ['X'],
    start: p(10.384, 0),
    startHint: 'Справа от грани на 5–15 мм, шарик ниже верха заготовки на 3–5 мм',
    view: { plane: 'XY', ox: 235, oy: 150, k: 3, stock: [-65, 0, -38, 38], feature: 'edge-xn' },
    touches: [{ axis: 'X', sign: -1, from: p(10.384, 0), to: p(R, 0) }],
    compute: (c) => ({
      feature: { x: c[0].x - R },
      info: [{ label: 'Контакт X', value: mm(c[0].x) }],
    }),
  },
  {
    id: 'edge-yp',
    group: 'edge',
    name: 'Кромка Y+',
    short: 'Одно касание: щуп идёт в +Y, заготовка за щупом',
    axes: ['Y'],
    start: p(0, -10.217),
    startHint: 'Перед гранью на 5–15 мм, шарик ниже верха заготовки на 3–5 мм',
    view: { plane: 'XY', ox: 200, oy: 205, k: 3, stock: [-55, 55, 0, 45], feature: 'edge-yp' },
    touches: [{ axis: 'Y', sign: 1, from: p(0, -10.217), to: p(0, -R) }],
    compute: (c) => ({
      feature: { y: c[0].y + R },
      info: [{ label: 'Контакт Y', value: mm(c[0].y) }],
    }),
  },
  {
    id: 'edge-yn',
    group: 'edge',
    name: 'Кромка Y−',
    short: 'Одно касание: щуп идёт в −Y, заготовка перед щупом',
    axes: ['Y'],
    start: p(0, 10.217),
    startHint: 'За гранью на 5–15 мм, шарик ниже верха заготовки на 3–5 мм',
    view: { plane: 'XY', ox: 200, oy: 90, k: 3, stock: [-55, 55, -45, 0], feature: 'edge-yn' },
    touches: [{ axis: 'Y', sign: -1, from: p(0, 10.217), to: p(0, R) }],
    compute: (c) => ({
      feature: { y: c[0].y - R },
      info: [{ label: 'Контакт Y', value: mm(c[0].y) }],
    }),
  },
  {
    id: 'top-z',
    group: 'surface',
    name: 'Поверхность Z',
    short: 'Касание верхней плоскости по Z−',
    axes: ['Z'],
    start: p(0, 0, 12.153),
    startHint: 'Над плоскостью на 5–15 мм, вдали от кромок',
    view: { plane: 'XZ', ox: 200, oy: 175, k: 3, stock: [-50, 50, -30, 0], feature: 'top' },
    touches: [{ axis: 'Z', sign: -1, from: p(0, 0, 12.153), to: p(0, 0, R) }],
    compute: (c) => ({
      feature: { z: c[0].z - R },
      info: [{ label: 'Контакт Z', value: mm(c[0].z) }],
    }),
  },
  {
    id: 'corner',
    group: 'feature',
    name: 'Наружный угол',
    short: 'X+, Y+ и Z− — ноль в углу заготовки',
    axes: ['X', 'Y', 'Z'],
    start: p(-10.271, -10.442, 6.118),
    startHint: 'Снаружи угла по диагонали, на 10 мм от обеих граней, над верхом',
    view: { plane: 'XY', ox: 130, oy: 215, k: 3, stock: [0, 65, 0, 48], feature: 'corner' },
    touches: [
      { axis: 'Z', sign: -1, from: p(18, 16, 6.118), to: p(18, 16, R) },
      { axis: 'X', sign: 1, from: p(-10.271, 32, -3), to: p(-R, 32, -3) },
      { axis: 'Y', sign: 1, from: p(36, -10.442, -3), to: p(36, -R, -3) },
    ],
    compute: (c) => ({
      feature: { x: c[1].x + R, y: c[2].y + R, z: c[0].z - R },
      info: [
        { label: 'Контакт Z', value: mm(c[0].z) },
        { label: 'Контакт X', value: mm(c[1].x) },
        { label: 'Контакт Y', value: mm(c[2].y) },
      ],
    }),
  },
  {
    id: 'boss',
    group: 'feature',
    name: 'Центр бобышки',
    short: '4 касания снаружи — центр и размеры',
    axes: ['X', 'Y'],
    start: p(0, 0, 6),
    startHint: 'Над центром бобышки на 5–10 мм выше верха',
    view: {
      plane: 'XY',
      ox: 200,
      oy: 150,
      k: 2.6,
      stock: [BOSS.cx - BOSS.w / 2, BOSS.cx + BOSS.w / 2, BOSS.cy - BOSS.h / 2, BOSS.cy + BOSS.h / 2],
      feature: 'center',
    },
    touches: [
      { axis: 'X', sign: 1, from: p(-BOSS.w / 2 - 10, 0, -4), to: p(BOSS.cx - BOSS.w / 2 - R, 0, -4) },
      { axis: 'X', sign: -1, from: p(BOSS.w / 2 + 10, 0, -4), to: p(BOSS.cx + BOSS.w / 2 + R, 0, -4) },
      { axis: 'Y', sign: 1, from: p(0, -BOSS.h / 2 - 10, -4), to: p(0, BOSS.cy - BOSS.h / 2 - R, -4) },
      { axis: 'Y', sign: -1, from: p(0, BOSS.h / 2 + 10, -4), to: p(0, BOSS.cy + BOSS.h / 2 + R, -4) },
    ],
    compute: (c) => ({
      feature: { x: (c[0].x + c[1].x) / 2, y: (c[2].y + c[3].y) / 2 },
      info: [
        { label: 'Ширина X', value: mm(c[1].x - c[0].x - 2 * R) },
        { label: 'Ширина Y', value: mm(c[3].y - c[2].y - 2 * R) },
      ],
    }),
  },
  {
    id: 'bore',
    group: 'feature',
    name: 'Центр отверстия',
    short: '4 касания изнутри — центр и диаметр',
    axes: ['X', 'Y'],
    start: p(0, 0, -5),
    startHint: 'Внутри отверстия примерно по центру, на 3–5 мм ниже кромки',
    view: {
      plane: 'XY',
      ox: 200,
      oy: 150,
      k: 4,
      stock: [-44, 44, -32, 32],
      hole: { cx: BORE.cx, cy: BORE.cy, r: BORE.d / 2 },
      feature: 'center',
    },
    touches: [
      { axis: 'X', sign: 1, from: p(0, BORE.cy, -5), to: p(BORE.cx + BORE.d / 2 - R, BORE.cy, -5) },
      { axis: 'X', sign: -1, from: p(0, BORE.cy, -5), to: p(BORE.cx - BORE.d / 2 + R, BORE.cy, -5) },
      { axis: 'Y', sign: 1, from: p(BORE.cx, 0, -5), to: p(BORE.cx, BORE.cy + BORE.d / 2 - R, -5) },
      { axis: 'Y', sign: -1, from: p(BORE.cx, 0, -5), to: p(BORE.cx, BORE.cy - BORE.d / 2 + R, -5) },
    ],
    compute: (c) => ({
      feature: { x: (c[0].x + c[1].x) / 2, y: (c[2].y + c[3].y) / 2 },
      info: [
        { label: 'Диаметр по X', value: mm(c[0].x - c[1].x + 2 * R) },
        { label: 'Диаметр по Y', value: mm(c[2].y - c[3].y + 2 * R) },
      ],
    }),
  },
]
