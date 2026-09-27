// Áreas funcionais da região: onde a vegetação não pode entrar e onde ela deve compor a paisagem
// em volta.
//
// Tudo o que o jogador usa — estradas, pontes, construções, praças, serviços, pontos de encontro,
// portais, abrigo e nós de coleta — vira uma forma (círculo, retângulo ou polígono convexo) com três
// faixas:
//   núcleo sem sólidos   nenhuma árvore, rocha, tronco ou toco;
//   núcleo sem vegetação nem arbusto, nem grama (pisos, pegadas, pontos de interação);
//   anel de composição   logo fora do núcleo: arbustos, flores e grama reforçados, árvores rareando
//                        aos poucos — a borda vira orla de mata, e não um recorte.
// As posições vêm do manifesto e de `game/layout.js`, as mesmas que o jogo usa para montar os
// objetos. A rasterização numa grade de 2 m deixa cada consulta O(1), como o campo de rotas.
//
// Nas construções trocadas (`extra-seat.js`) a caixa do manifesto — o colisor — só afasta árvores e
// rochas; o núcleo sem grama é a pegada do modelo novo, girada e na escala final, com margem curta.
// A borda da grama em volta vem de `ground-cover.js`, que rareia a cobertura numa faixa irregular.
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
import { planExtraSpots } from './extra-spots.js';
import { REPLACEMENTS, replacementFit, additionFit, footprintWorld, roadwayFootprint } from './extra-seat.js';

const STEP = 2, N = 513, FAR = 60;
const solidGap = new Float32Array(N * N);   // distância até sair do núcleo sem sólidos (≤ 0: dentro)
const lowGap = new Float32Array(N * N);     // idem para o núcleo sem vegetação (plantio de arbustos e rochas)
const coverGap = new Float32Array(N * N);   // idem para a cobertura do chão (grama, flores): sem as áreas `lowFor: 'inst'`
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

// distância assinada até um polígono convexo (negativa por dentro)
function sdfPoly(a, x, z) {
  const P = a.poly, n = P.length;
  let dmin = Infinity, inside = true;
  for (let i = 0; i < n; i++) {
    const [ax, az] = P[i], [bx, bz] = P[(i + 1) % n];
    const ex = bx - ax, ez = bz - az, l2 = ex * ex + ez * ez || 1;
    let t = ((x - ax) * ex + (z - az) * ez) / l2;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    dmin = Math.min(dmin, Math.hypot(ax + ex * t - x, az + ez * t - z));
    if ((ex * (z - az) - ez * (x - ax)) * a.turn < 0) inside = false;
  }
  return inside ? -dmin : dmin;
}
function polyArea(tag, poly, o) {
  let s2 = 0, x = 0, z = 0;
  for (let i = 0; i < poly.length; i++) {
    const [ax, az] = poly[i], [bx, bz] = poly[(i + 1) % poly.length];
    s2 += ax * bz - bx * az; x += ax / poly.length; z += az / poly.length;
  }
  return { tag, poly, x, z, turn: Math.sign(s2) || 1, solid: -999, low: 0.15, ring: 0, ...o };
}

// distância assinada até a forma (negativa por dentro)
function sdf(a, x, z) {
  if (a.poly) return sdfPoly(a, x, z);
  if (a.r != null) return Math.hypot(x - a.x, z - a.z) - a.r;
  const dx = Math.abs(x - a.x) - a.hw, dz = Math.abs(z - a.z) - a.hd;
  const ox = Math.max(dx, 0), oz = Math.max(dz, 0);
  return Math.hypot(ox, oz) + Math.min(Math.max(dx, dz), 0);
}

function stamp(a) {
  const reach = Math.max(a.solid, a.low) + a.ring + (a.thinR || 0) + 2;
  const ext = a.poly ? Math.max(...a.poly.map(([px, pz]) => Math.hypot(px - a.x, pz - a.z))) : a.r != null ? a.r : Math.max(a.hw, a.hd) * 1.42;
  const i0 = Math.max(0, Math.floor((a.x - ext - reach + 512) / STEP)), i1 = Math.min(N - 1, Math.ceil((a.x + ext + reach + 512) / STEP));
  const j0 = Math.max(0, Math.floor((a.z - ext - reach + 512) / STEP)), j1 = Math.min(N - 1, Math.ceil((a.z + ext + reach + 512) / STEP));
  for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) {
    const x = -512 + i * STEP, z = -512 + j * STEP, k = j * N + i;
    const d = sdf(a, x, z);
    const s = d - a.solid, l = d - a.low;
    if (s < solidGap[k]) solidGap[k] = s;
    if (l < lowGap[k]) lowGap[k] = l;
    if (a.lowFor !== 'inst' && l < coverGap[k]) coverGap[k] = l;
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
  solidGap.fill(FAR); lowGap.fill(FAR); coverGap.fill(FAR); ring.fill(0); thin.fill(255); routeEdge.fill(FAR); routeTrail.fill(0);
  const areas = FUNCTIONAL.areas;
  areas.length = 0;
  // `solid` e `low` são margens a partir da borda da forma; GRASS desliga o núcleo sem vegetação
  const GRASS = -999;
  const circle = (tag, x, z, r, o) => areas.push({ tag, x, z, r, solid: 0, low: GRASS, ring: 0, ...o });
  const rect = (tag, x, z, hw, hd, o) => areas.push({ tag, x, z, hw, hd, solid: 0, low: GRASS, ring: 0, ...o });

  rasterRoutes();

  // pegada visual de cada troca (a caixa do manifesto continua afastando árvores e rochas)
  const swapped = new Map();
  for (const r of REPLACEMENTS) {
    const poly = r.fit === 'ponte' ? roadwayFootprint(r.volume) : footprintWorld(replacementFit(r) || { meta: { extra: {} } });
    if (poly && poly.length >= 3) swapped.set(r.volume, poly);
  }
  // pontes, com as rampas: nem árvore nem arbusto em cima das rampas e aproximações; a grama só sai
  // do tabuleiro e das paredes (pegada do tabuleiro), com borda irregular
  for (const p of PLACEMENTS.filter((q) => q.bridge)) {
    const poly = swapped.get(p.name);
    rect('ponte', p.plan[0], PLAN_Z_SIGN * p.plan[1], p.half[0] + 2, p.half[1] + 14, { solid: 0.5, low: 0.5, ring: 6, ringK: 0.7, lowFor: poly ? 'inst' : 'all' });
    if (poly) areas.push(polyArea(`ponte:${p.name}`, poly, { low: 0.2 }));
  }
  // construções e marcos do Blender
  for (const p of PLACEMENTS) {
    if (p.bridge) continue;
    const x = p.plan[0], z = PLAN_Z_SIGN * p.plan[1], hw = p.half[0], hd = p.half[1];
    if (p.kind === 'field') { rect('campo', x, z, hw, hd, { solid: 1.5, ring: 7, ringK: 0.8 }); continue; }
    const big = ['camp', 'mine', 'cave', 'portal', 'observatory'].includes(p.kind);
    const poly = swapped.get(p.name);
    rect(p.kind, x, z, hw, hd, { solid: big ? 9 : 3.5, low: poly ? GRASS : 0.4, ring: big ? 10 : 8 });
    if (poly) areas.push(polyArea(`pegada:${p.name}`, poly));
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
  // acréscimos de cena (poços, moinho, árvores-marco, ponte em ruína): escolhidos depois dos nós de
  // coleta, que assim não mudam, e antes da vegetação, que passa a desviar deles
  for (const s of planExtraSpots({ solidGapAt, nodes: FUNCTIONAL.nodes })) {
    // o raio do ponto afasta árvores e rochas; a grama só sai da base do modelo
    const a = { tag: `extra:${s.id}`, x: s.x, z: s.z, r: s.r, solid: 1.2, low: GRASS, ring: 4, ringK: 0.6 };
    areas.push(a);
    stamp(a);
    const poly = footprintWorld(additionFit(s) || { meta: { extra: {} } });
    if (poly && poly.length >= 3) {
      const b = polyArea(`base:${s.id}`, poly);
      areas.push(b);
      stamp(b);
      LOW_AREAS.push(b);
    }
  }
  EXACT.clear();
  FUNCTIONAL.ready = true;
  return FUNCTIONAL;
}

// ---------------------------------------------------------------- consultas
// Distância até sair do núcleo sem sólidos: um objeto de raio r cabe quando o valor é ≥ r.
export function solidGapAt(x, z) { const k = cellOf(x, z); return k < 0 ? FAR : solidGap[k]; }
export function lowGapAt(x, z) { const k = cellOf(x, z); return k < 0 ? FAR : lowGap[k]; }
// Versão exata perto das bordas: a grade de 2 m erra até 1,4 m, o que põe grama dentro de uma
// parede. Longe de tudo responde a grade; perto, a forma de verdade.
// É a consulta da cobertura do chão: não vê as áreas só de plantio (`lowFor: 'inst'`). As formas
// ficam num índice de 16 m, e cada consulta só mede as que estão perto.
export function lowGapExact(x, z) {
  const k = cellOf(x, z), g = k < 0 ? FAR : coverGap[k];
  if (g > 2.5) return g;
  if (!EXACT.size) indexExact();
  const list = EXACT.get(Math.floor((z + 512) / 16) * 64 + Math.floor((x + 512) / 16));
  let best = FAR;
  if (list) for (const a of list) best = Math.min(best, sdf(a, x, z) - a.low);
  return best;
}
const LOW_AREAS = [];
const EXACT = new Map();
function indexExact() {
  for (const a of LOW_AREAS) {
    if (a.lowFor === 'inst') continue;
    const ext = (a.poly ? Math.max(...a.poly.map(([px, pz]) => Math.hypot(px - a.x, pz - a.z))) : a.r != null ? a.r : Math.hypot(a.hw, a.hd)) + a.low + 4;
    const i0 = Math.floor((a.x - ext + 512) / 16), i1 = Math.floor((a.x + ext + 512) / 16);
    const j0 = Math.floor((a.z - ext + 512) / 16), j1 = Math.floor((a.z + ext + 512) / 16);
    for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) {
      const key = j * 64 + i;
      if (!EXACT.has(key)) EXACT.set(key, []);
      EXACT.get(key).push(a);
    }
  }
}
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
