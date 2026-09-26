// Onde ficam os acréscimos de cena (poços, moinho, árvores-marco e a ponte em ruína).
//
// Nenhuma posição é digitada: cada acréscimo procura, perto de uma âncora do mundo, o primeiro ponto
// (numa varredura fixa, sempre a mesma) que esteja em chão livre, fora do corredor das estradas,
// fora da água, longe dos volumes do Blender, dos pontos de coleta e das áreas de jogo. O cálculo roda
// dentro de `buildFunctionalAreas()`, depois do sorteio dos pontos de coleta (que por isso não mudam)
// e antes do plantio da vegetação (que passa a desviar de cada acréscimo).
import { addBox, addCircle } from '../game/collide.js';
import { LOC } from '../game/layout.js';
import { PLACEMENTS, REGION } from './runtime-manifest.js';
import { PLAN_Z_SIGN } from './coordinates.js';
import { routeDistance } from './world-routes.js';
import { isClearGround, WORLD_COLLIDERS } from './world-colliders.js';
import { regionHeightAt, isWaterCell, wetnessAt } from './heightfield.js';
import { forestDensity } from './vegetation-fields.js';

// r: raio que a vegetação e os outros acréscimos deixam livre; foot: pegada real (chão livre e
// declive); col: colisor. near: âncora; ring: [raio mínimo, raio máximo] da busca; road: folga da
// estrada além de `foot`; gap: distância mínima para fora das áreas de jogo (nulo nas vilas, onde o
// poço é justamente um objeto de vila — lá valem os pontos de `villagePoints`).
export const EXTRA_DEFS = [
  { id: 'poco-vale', asset: 'poco', r: 2.2, foot: 1.6, col: { r: 1.25 }, road: 3, gap: null, near: () => anchor('market'), ring: [10, 26] },
  { id: 'poco-alto', asset: 'poco', r: 2.2, foot: 1.6, col: { r: 1.25 }, road: 3, gap: null, near: () => anchor('outpost_north'), ring: [10, 30] },
  { id: 'moinho-sul', asset: 'moinho', r: 6, foot: 3.2, col: { r: 2.8 }, road: 5, gap: 4, near: fieldsCenter, ring: [16, 60] },
  { id: 'ponte-ruina', asset: 'ponte_quebrada', r: 8, foot: 6, col: { box: [6.2, 2.4] }, road: 6, gap: 6, near: () => ({ x: LOC.ermos.x, z: LOC.ermos.z }), ring: [24, 90] },
  { id: 'arvore-bosque', asset: 'arvore_marco', r: 4.5, foot: 1.2, col: { r: 0.6 }, road: 7, gap: 3, near: () => ({ x: LOC.bosque.x, z: LOC.bosque.z }), ring: [6, 70], score: (x, z) => forestDensity(x, z) },
  { id: 'arvore-rio', asset: 'arvore_marco', r: 4.5, foot: 1.2, col: { r: 0.6 }, road: 7, gap: 3, near: () => ({ x: LOC.ponte.x, z: LOC.ponte.z }), ring: [30, 150], want: (x, z) => { const w = wetnessAt(x, z); return w > 0.2 && w < 0.75; } },
];

// Objetos que `game/world.js` monta no Vale e no Alto (vendedores, bancada, instrutor, quadro,
// guardas, estandartes, barracas), pelas mesmas fórmulas de lá, a partir dos mesmos volumes.
function villagePoints() {
  const at = (name) => { const p = PLACEMENTS.find((q) => q.name === name); return p && { x: p.plan[0], z: PLAN_Z_SIGN * p.plan[1], hw: p.half[0], hd: p.half[1] }; };
  const out = [];
  const m1 = at('SouthMarket_01'), m2 = at('SouthMarket_02'), ws = at('SouthWorkshop'), tw = at('SouthOutpost_Tower');
  const wh = at('SouthWarehouse'), st = at('SouthStable'), gate = at('SouthHouse_01');
  for (const m of [m1, m2]) if (m) out.push([m.x, m.z + m.hd + 1.6]);
  if (m1 && m2) out.push([(m1.x + m2.x) / 2, (m1.z + m2.z) / 2 + 4]);
  if (m1) out.push([m1.x - 9, m1.z + 6], [m1.x - 9, m1.z + 7.5], [m1.x - 16, m1.z + 12], [m1.x + 16, m1.z + 12]);
  if (ws) out.push([ws.x + ws.hw + 2.6, ws.z], [ws.x + ws.hw + 4.2, ws.z + 1.6]);
  if (tw) out.push([tw.x + 6, tw.z + 5], [tw.x + 9, tw.z + 8], [tw.x + 4, tw.z + 7]);
  if (wh) out.push([wh.x, wh.z + wh.hd + 2]);
  if (st) out.push([st.x - st.hw - 2.4, st.z + 1.5]);
  if (gate) for (const [dx, dz] of [[-5, -6], [5, -6], [-6, 6], [6, 6]]) out.push([gate.x + dx, gate.z + dz]);
  const nt = at('NorthOutpost_Tower'), nh = at('NorthHouse_01');
  if (nt && nh) {
    const b = { x: (nt.x + nh.x) / 2, z: (nt.z + nh.z) / 2 };
    out.push([b.x - 5.5, b.z + 7], [b.x + 5.5, b.z + 7], [b.x, b.z + 10], [b.x - 7, b.z + 14], [b.x + 7, b.z + 14], [b.x - 13, b.z + 16], [b.x + 13, b.z + 16], [nt.x - 6, nt.z + 4]);
  }
  return out;
}

export const EXTRA_SPOTS = [];

function anchor(id) {
  const a = REGION.anchors[id];
  return { x: a[0], z: PLAN_Z_SIGN * a[1] };
}
function fieldsCenter() {
  const f = PLACEMENTS.filter((p) => p.kind === 'field');
  return { x: f.reduce((s, p) => s + p.plan[0], 0) / f.length, z: f.reduce((s, p) => s + PLAN_Z_SIGN * p.plan[1], 0) / f.length };
}
// folga até a caixa de um volume do Blender (negativa dentro)
function boxGap(p, x, z) {
  const dx = Math.abs(x - p.plan[0]) - p.half[0], dz = Math.abs(z - PLAN_Z_SIGN * p.plan[1]) - p.half[1];
  return Math.max(dx, dz, Math.min(0, Math.max(dx, dz)));
}
function slopeOver(x, z, r) {
  let lo = Infinity, hi = -Infinity;
  for (let a = 0; a < 8; a++) for (const k of [0.5, 1]) {
    const h = regionHeightAt(x + Math.cos(a * Math.PI / 4) * r * k, z + Math.sin(a * Math.PI / 4) * r * k);
    lo = Math.min(lo, h); hi = Math.max(hi, h);
  }
  return hi - lo;
}

// `solidGapAt` e `nodes` chegam de `functional-areas.js` (evita a importação circular)
export function planExtraSpots({ solidGapAt, nodes }) {
  EXTRA_SPOTS.length = 0;
  const village = villagePoints();
  for (const d of EXTRA_DEFS) {
    const c = d.near();
    let best = null;
    // varredura fixa: anéis de 2 m, 24 direções por anel
    for (let rad = d.ring[0]; rad <= d.ring[1] && !best; rad += 2) {
      const cands = [];
      for (let i = 0; i < 24; i++) {
        const a = (i / 24) * Math.PI * 2 + rad * 0.37;
        const x = c.x + Math.cos(a) * rad, z = c.z + Math.sin(a) * rad;
        if (Math.abs(x) > 500 || Math.abs(z) > 500 || isWaterCell(x, z)) continue;
        if (routeDistance(x, z) < d.road + d.foot) continue;
        if (!isClearGround(x, z, d.foot)) continue;
        if (d.gap != null && solidGapAt(x, z) < d.gap) continue;
        if (slopeOver(x, z, d.foot) > Math.max(0.8, d.foot * 0.3)) continue;
        if (PLACEMENTS.some((p) => boxGap(p, x, z) < d.foot + 3)) continue;
        if (nodes.some((n) => Math.hypot(n.x - x, n.z - z) < d.r + 4)) continue;
        if (village.some(([vx, vz]) => Math.hypot(vx - x, vz - z) < d.foot + 4)) continue;
        if (EXTRA_SPOTS.some((s) => Math.hypot(s.x - x, s.z - z) < s.r + d.r + 6)) continue;
        if (d.want && !d.want(x, z)) continue;
        cands.push({ x, z, s: d.score ? d.score(x, z) : 0 });
      }
      if (cands.length) best = cands.sort((p, q) => p.s - q.s)[0];
    }
    if (!best) { console.warn(`acréscimo ${d.id}: nenhum ponto livre encontrado; fica de fora`); continue; }
    const yaw = Math.atan2(c.x - best.x, c.z - best.z);   // de frente para a âncora
    const spot = { id: d.id, asset: d.asset, x: best.x, z: best.z, r: d.r, yaw, col: d.col };
    if (d.col.r) {
      addCircle(spot.x, spot.z, d.col.r);
      WORLD_COLLIDERS.push({ x: spot.x, z: spot.z, r: d.col.r, name: d.id, kind: 'extra' });
    } else {
      // caixa alinhada ao eixo: a ponte em ruína (comprimento no x do modelo) fica paralela a x ou a z
      const turned = Math.abs(Math.sin(yaw)) > Math.SQRT1_2;
      const [hw, hd] = turned ? [d.col.box[1], d.col.box[0]] : d.col.box;
      spot.yaw = turned ? Math.PI / 2 : 0;
      addBox(spot.x, spot.z, hw, hd, 0);
      WORLD_COLLIDERS.push({ x: spot.x, z: spot.z, hw, hd, rot: 0, name: d.id, kind: 'extra' });
    }
    EXTRA_SPOTS.push(spot);
  }
  return EXTRA_SPOTS;
}
