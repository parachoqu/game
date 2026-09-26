// Áreas funcionais da região: onde a vegetação não pode entrar e onde ela deve compor a paisagem
// em volta.
//
// Tudo o que o jogador usa — estradas, pontes, construções, praças, serviços, pontos de encontro,
// portais, abrigo e nós de coleta — vira uma forma (círculo ou retângulo) com três faixas:
//   núcleo sem sólidos   nenhuma árvore, rocha, tronco ou toco;
//   núcleo sem vegetação nem arbusto, nem grama (pisos, pegadas, pontos de interação);
//   anel de composição   logo fora do núcleo: arbustos, flores e grama reforçados, árvores rareando
//                        aos poucos — a borda vira orla de mata, e não um recorte.
// As posições vêm do manifesto e de `game/layout.js`, as mesmas que o jogo usa para montar os
// objetos. A rasterização numa grade de 2 m deixa cada consulta O(1), como o campo de rotas.
import { PLACEMENTS, REGION } from './runtime-manifest.js';
import { PLAN_Z_SIGN } from './coordinates.js';
import { REGION_ROUTE_LINES, initRouteField } from './world-routes.js';
import { isWaterCell, biomeAt } from './heightfield.js';
import { isClearGround } from './world-colliders.js';
import { initTerrainData } from '../engine/terrain.js';
import {
  LOC, HALF, PORTAL_SPOTS, CANTEIRO, PASSAGE_PROPS, ENCOUNTERS, GUARD_POSTS, CAMP_DISPLACED,
} from '../game/layout.js';
import { SHELTER } from '../game/zones.js';
import { rng } from './noise.js';

const STEP = 2, N = 513, FAR = 60;
const solidGap = new Float32Array(N * N);   // distância até sair do núcleo sem sólidos (≤ 0: dentro)
const lowGap = new Float32Array(N * N);     // idem para o núcleo sem vegetação
const ring = new Uint8Array(N * N);         // força do anel de composição (0–255)
const thin = new Uint8Array(N * N);         // multiplicador de densidade de árvores (255 = sem efeito)
const routeEdge = new Float32Array(N * N);  // distância até a borda da estrada mais próxima
const routeTrail = new Uint8Array(N * N);   // 1 quando a estrada mais próxima é trilha (≤ 3 m)

export const FUNCTIONAL = { areas: [], nodes: [], ready: false };

// As seis montanhas do kit (45 m, raio de 27 m) ficam em cotas altas, longe das rotas e dos marcos.
// Moram aqui porque os nós de coleta precisam desviar delas antes de a vegetação ser planejada.
export const MOUNTAINS = [
  { x: 340, z: -240 }, { x: -260, z: -300 }, { x: -300, z: -140 },
  { x: 440, z: -200 }, { x: 240, z: -260 }, { x: 420, z: -320 },
];

const cellOf = (x, z) => {
  const i = Math.round((x + 512) / STEP), j = Math.round((z + 512) / STEP);
  return (i < 0 || j < 0 || i >= N || j >= N) ? -1 : j * N + i;
};

// distância assinada até a forma (negativa por dentro)
function sdf(a, x, z) {
  if (a.r != null) return Math.hypot(x - a.x, z - a.z) - a.r;
  const dx = Math.abs(x - a.x) - a.hw, dz = Math.abs(z - a.z) - a.hd;
  const ox = Math.max(dx, 0), oz = Math.max(dz, 0);
  return Math.hypot(ox, oz) + Math.min(Math.max(dx, dz), 0);
}

function stamp(a) {
  const reach = Math.max(a.solid, a.low) + a.ring + (a.thinR || 0) + 2;
  const ext = a.r != null ? a.r : Math.max(a.hw, a.hd) * 1.42;
  const i0 = Math.max(0, Math.floor((a.x - ext - reach + 512) / STEP)), i1 = Math.min(N - 1, Math.ceil((a.x + ext + reach + 512) / STEP));
  const j0 = Math.max(0, Math.floor((a.z - ext - reach + 512) / STEP)), j1 = Math.min(N - 1, Math.ceil((a.z + ext + reach + 512) / STEP));
  for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) {
    const x = -512 + i * STEP, z = -512 + j * STEP, k = j * N + i;
    const d = sdf(a, x, z);
    const s = d - a.solid, l = d - a.low;
    if (s < solidGap[k]) solidGap[k] = s;
    if (l < lowGap[k]) lowGap[k] = l;
    if (a.ring > 0 && s > -1 && s < a.ring) {
      const t = s <= 0 ? 1 : 1 - s / a.ring;
      const v = Math.round(255 * t * t * (3 - 2 * t) * (a.ringK ?? 1));
      if (v > ring[k]) ring[k] = v;
    }
    if (a.thinR && d < a.thinR) {
      const v = Math.round(255 * (a.thinK + (1 - a.thinK) * Math.max(0, d / a.thinR) ** 2));
      if (v < thin[k]) thin[k] = v;
    }
  }
}

function rasterRoutes() {
  for (const line of Object.values(REGION_ROUTE_LINES)) {
    const half = line.width / 2, trail = line.width <= 3 ? 1 : 0;
    const pts = line.pts, pad = 20;
    for (let s = 0; s < pts.length - 1; s++) {
      const [ax, az] = pts[s], [bx, bz] = pts[s + 1];
      const i0 = Math.max(0, Math.floor((Math.min(ax, bx) - pad + 512) / STEP)), i1 = Math.min(N - 1, Math.ceil((Math.max(ax, bx) + pad + 512) / STEP));
      const j0 = Math.max(0, Math.floor((Math.min(az, bz) - pad + 512) / STEP)), j1 = Math.min(N - 1, Math.ceil((Math.max(az, bz) + pad + 512) / STEP));
      const dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
      for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) {
        const x = -512 + i * STEP, z = -512 + j * STEP, k = j * N + i;
        let t = l2 ? ((x - ax) * dx + (z - az) * dz) / l2 : 0;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        const e = Math.hypot(ax + dx * t - x, az + dz * t - z) - half;
        if (e < routeEdge[k]) { routeEdge[k] = e; routeTrail[k] = trail; }
      }
    }
  }
}

// ---------------------------------------------------------------- nós de coleta
// Um só gerador para a vegetação e para `game/world.js`: os nós são escolhidos antes da floresta,
// em chão livre de construções e longe dos pontos de jogo, e a floresta abre espaço em volta deles.
function planResourceNodes() {
  const D = rng(4242);
  const want = { minerio: 22, madeira: 24, erva: 24, cristal: 12 };
  const got = { minerio: 0, madeira: 0, erva: 0, cristal: 0 };
  const e = LOC.ermos;
  const near = (p, x, z, r) => Math.hypot(p.x - x, p.z - z) < r;
  const nodes = [];
  for (let i = 0; i < 26000; i++) {
    if (got.minerio >= want.minerio && got.madeira >= want.madeira && got.erva >= want.erva && got.cristal >= want.cristal) break;
    const x = -HALF + D() * HALF * 2, z = -HALF + D() * HALF * 2;
    if (isWaterCell(x, z) || !isClearGround(x, z, 1.4, 5)) continue;
    if (solidGapAt(x, z) < 2) continue;                       // longe de serviços, tendas e caixas
    if (MOUNTAINS.some((m) => Math.hypot(m.x - x, m.z - z) < 20)) continue;
    if (nodes.some((n) => Math.hypot(n.x - x, n.z - z) < 16)) continue;
    const ex = (x - e.x) / e.rx, ez = (z - e.z) / e.rz;
    const inWastes = ex * ex + ez * ez < 1;
    const biome = biomeAt(x, z);
    let kind = null;
    if (inWastes) kind = got.cristal < want.cristal ? 'cristal' : got.minerio < want.minerio ? 'minerio' : null;
    else if (biome === 2 || near(LOC.mina, x, z, 90) || near(LOC.caverna, x, z, 80)) kind = got.minerio < want.minerio ? 'minerio' : null;
    else if (biome === 3 || biome === 1) kind = got.madeira < want.madeira ? 'madeira' : got.erva < want.erva ? 'erva' : null;
    else kind = got.erva < want.erva ? 'erva' : null;
    if (!kind) continue;
    nodes.push({ kind, x, z, extra: inWastes ? 1 : 0 });
    got[kind]++;
  }
  return nodes;
}

// ---------------------------------------------------------------- montagem
export function buildFunctionalAreas() {
  if (FUNCTIONAL.ready) return FUNCTIONAL;
  initTerrainData();
  initRouteField();
  solidGap.fill(FAR); lowGap.fill(FAR); ring.fill(0); thin.fill(255); routeEdge.fill(FAR); routeTrail.fill(0);
  const areas = FUNCTIONAL.areas;
  areas.length = 0;
  // `solid` e `low` são margens a partir da borda da forma; GRASS desliga o núcleo sem vegetação
  const GRASS = -999;
  const circle = (tag, x, z, r, o) => areas.push({ tag, x, z, r, solid: 0, low: GRASS, ring: 0, ...o });
  const rect = (tag, x, z, hw, hd, o) => areas.push({ tag, x, z, hw, hd, solid: 0, low: GRASS, ring: 0, ...o });

  rasterRoutes();

  // pontes, com as rampas: nada em cima, nem grama
  for (const p of PLACEMENTS.filter((q) => q.bridge)) {
    rect('ponte', p.plan[0], PLAN_Z_SIGN * p.plan[1], p.half[0] + 2, p.half[1] + 14, { solid: 0.5, low: 0.5, ring: 6, ringK: 0.7 });
  }
  // construções e marcos do Blender
  for (const p of PLACEMENTS) {
    if (p.bridge) continue;
    const x = p.plan[0], z = PLAN_Z_SIGN * p.plan[1], hw = p.half[0], hd = p.half[1];
    if (p.kind === 'field') { rect('campo', x, z, hw, hd, { solid: 1.5, ring: 7, ringK: 0.8 }); continue; }
    const big = ['camp', 'mine', 'cave', 'portal', 'observatory'].includes(p.kind);
    rect(p.kind, x, z, hw, hd, { solid: big ? 9 : 3.5, low: 0.4, ring: big ? 10 : 8 });
  }
  // assentamentos e praças
  for (const [loc, k] of [[LOC.vale, 0.72], [LOC.mercado, 1], [LOC.alto, 0.72]]) {
    circle('assentamento', loc.x, loc.z, loc.r * k, { ring: 16 });
  }
  const A = REGION.anchors;
  if (A.market) circle('praça', A.market[0], PLAN_Z_SIGN * A.market[1], 9, { low: 0 });
  // serviços e peças do jogo
  circle('canteiro', CANTEIRO.x + 1, CANTEIRO.z + 2, 13, { low: -5, ring: 9 });
  circle('vigia', LOC.vigia.x + 1.5, LOC.vigia.z, 6, { low: -2, ring: 7 });
  for (const b of PASSAGE_PROPS.banners) circle('estandarte', b.x, b.z, 2.5, { low: -1.5, ring: 4 });
  circle('carroça', PASSAGE_PROPS.cart.x, PASSAGE_PROPS.cart.z, 3.5, { low: -1, ring: 5 });
  circle('paliçada', PASSAGE_PROPS.palisade.x, PASSAGE_PROPS.palisade.z, 4.5, { low: -1, ring: 5 });
  circle('acampamento', LOC.acampamento.x, LOC.acampamento.z, 21, { low: -8, ring: 10 });
  circle('acampamento deslocado', CAMP_DISPLACED.x, CAMP_DISPLACED.z, 10, { ring: 8 });
  for (const g of GUARD_POSTS) circle('posto', g.x, g.z, 3, { ring: 4 });
  circle('abrigo', SHELTER.x, SHELTER.z, 5, { low: -3 });
  for (const [x, z] of PORTAL_SPOTS) circle('portal', x, z, 8, { ring: 7 });
  if (A.portal_turbulent) circle('portal', A.portal_turbulent[0], PLAN_Z_SIGN * A.portal_turbulent[1], 8.5, { ring: 8 });
  // encontros: clareira para lutar e mata mais rala até metade da coleira
  for (const e of ENCOUNTERS) circle('encontro', e.x, e.z, 12, { ring: 8, thinR: e.leash * 0.5, thinK: 0.35 });

  for (const a of areas) stamp(a);
  LOW_AREAS.length = 0;
  LOW_AREAS.push(...areas.filter((a) => a.low > -900));

  // nós de coleta: escolhidos depois dos pontos de jogo, e então somados às áreas
  FUNCTIONAL.nodes = planResourceNodes();
  for (const n of FUNCTIONAL.nodes) {
    const a = { tag: 'coleta', x: n.x, z: n.z, r: 0, solid: 4.3, low: 1.5, ring: 5, ringK: 0.8 };
    areas.push(a);
    stamp(a);
    LOW_AREAS.push(a);
  }
  FUNCTIONAL.ready = true;
  return FUNCTIONAL;
}

// ---------------------------------------------------------------- consultas
// Distância até sair do núcleo sem sólidos: um objeto de raio r cabe quando o valor é ≥ r.
export function solidGapAt(x, z) { const k = cellOf(x, z); return k < 0 ? FAR : solidGap[k]; }
export function lowGapAt(x, z) { const k = cellOf(x, z); return k < 0 ? FAR : lowGap[k]; }
// Versão exata perto das bordas: a grade de 2 m erra até 1,4 m, o que põe grama dentro de uma
// parede. Longe de tudo responde a grade; perto, a forma de verdade.
export function lowGapExact(x, z) {
  const g = lowGapAt(x, z);
  if (g > 2.5) return g;
  let best = FAR;
  for (const a of LOW_AREAS) best = Math.min(best, sdf(a, x, z) - a.low);
  return best;
}
const LOW_AREAS = [];
export function ringAt(x, z) { const k = cellOf(x, z); return k < 0 ? 0 : ring[k] / 255; }
export function thinAt(x, z) { const k = cellOf(x, z); return k < 0 ? 1 : thin[k] / 255; }
// Distância até a borda da estrada mais próxima (negativa sobre a pista) e se ela é trilha.
export function routeEdgeAt(x, z) { const k = cellOf(x, z); return k < 0 ? FAR : routeEdge[k]; }
export function routeIsTrailAt(x, z) { const k = cellOf(x, z); return k >= 0 && routeTrail[k] === 1; }

// Área dos biomas medida na máscara já com o rio refeito, em hectares.
export function measureBiomeHa() {
  const ha = [0, 0, 0, 0, 0, 0];
  for (let z = -512; z < 512; z += 2) for (let x = -512; x < 512; x += 2) {
    ha[isWaterCell(x, z) ? 5 : biomeAt(x, z)] += 4;
  }
  return ha.map((m2) => m2 / 10000);
}
