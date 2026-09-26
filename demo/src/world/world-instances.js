// Vegetação e rochas do mundo, sempre instanciadas.
//
// Três camadas, todas determinísticas:
//   autoral      as 2.610 posições de `instances.json` do Blender (folhosas, pinheiros, rochas e
//                tufos de grama). Árvores e rochas são reproduzidas; os tufos viram sementes das
//                manchas de grama da cobertura do chão (`ground-cover.js`).
//   copa         floresta em aglomerados: o campo de densidade de `vegetation-fields.js` decide
//                onde a mata fecha, abre em campo ou deixa uma clareira; o espaçamento acompanha
//                a densidade (núcleo fechado, borda rala) e cada árvore ganha escala, altura,
//                inclinação e tom próprios.
//   sub-bosque   arbustos na orla, nas margens do rio e em volta das áreas funcionais; campos de
//                pedra; troncos caídos e tocos no miolo da mata; paredões nas encostas de rocha.
// Estradas, pontes, construções, serviços, encontros e nós de coleta ficam livres pelas regras de
// `functional-areas.js`. O desenho é por `engine/lod-field.js` (nível de detalhe por instância).
import * as THREE from 'three';
import * as PR from '../engine/props.js';
import { createLODField, setLODDensity, LOD_FIELDS } from '../engine/lod-field.js';
import { REGION, TURBULENT, WORLD } from './runtime-manifest.js';
import { PLAN_Z_SIGN, TURB_RUNTIME_OFFSET } from './coordinates.js';
import { regionHeightAt, turbulentHeightAt, biomeAt, isWaterCell, waterLevelAt, wetnessAt } from './heightfield.js';
import { initRouteField, nearestRouteInfo } from './world-routes.js';
import { addCircle } from '../game/collide.js';
import {
  buildFunctionalAreas, solidGapAt, lowGapAt, ringAt, thinAt, routeEdgeAt, measureBiomeHa, MOUNTAINS,
} from './functional-areas.js';
import { forestDensity, canopyHue, standAge, standClump, inWastes, slopeHere } from './vegetation-fields.js';
import { rng, vnoise, fbm2, smoothstep, lerp } from './noise.js';
import { initGroundCover } from './ground-cover.js';

// grupo do manifesto → tipo de protótipo da demo
const KIND_OF = {
  SCATTER_Broadleaf: 'broad',
  SCATTER_Pines: 'pine',
  SCATTER_Rocks: 'rock',
  SCATTER_Grass: 'grass',
  TURBULENT_ScatterRocks: 'rock',
};
const PROTO_OF = {
  broad: 'ASSET_BroadleafTree', pine: 'ASSET_PineTree',
  rock: 'ASSET_Rock', grass: 'ASSET_GrassClump',
};

export const INSTANCE_REPORT = { authored: 0, derived: 0, byKind: {}, byBiome: {}, byStratum: {}, mountains: 0 };

// ---------------------------------------------------------------- calibração de escala
const PROTO_HEIGHTS = { broad: 9.2, pine: 12.5, rock: 2.4, grass: 0.8 };
function prototypeHeight(kind) {
  if (PR.KIT.metrics && PR.KIT.metrics(kind)) return PR.KIT.metrics(kind).height || 1;
  return PROTO_HEIGHTS[kind] || 1;
}
const PROTO_METRICS = WORLD.nature_prototypes?.metrics || {};
const CAL_MAX = 1.35;
function calibration(kind) {
  if (PR.KIT.metrics && PR.KIT.metrics(kind)) return 1;
  const target = PROTO_METRICS[PROTO_OF[kind]]?.height_m;
  if (!target) return 1;
  return Math.min(CAL_MAX, target / prototypeHeight(kind));
}
const variantsOf = (kind) => (PR.KIT.variants ? PR.KIT.variants(kind) || 3 : 3);
const colliderOf = (kind, variant) => (PR.KIT.collider ? PR.KIT.collider(kind, variant) : null) ?? PR.natureCollider(kind, variant);

function groundContact(heightAt, x, z, radius) {
  let lo = heightAt(x, z), hi = lo;
  for (let i = 0; i < 6; i++) {
    const a = (i / 6) * Math.PI * 2;
    const h = heightAt(x + Math.cos(a) * radius, z + Math.sin(a) * radius);
    if (h < lo) lo = h;
    if (h > hi) hi = h;
  }
  return { y: lo - 0.05, slope: hi - lo };
}

const KINDS = ['pine', 'broad', 'dead', 'rock', 'cliff', 'bush', 'log', 'stump', 'mountain'];
const TREE_KINDS = new Set(['pine', 'broad', 'dead']);
const SHRUB_KINDS = ['bush', 'rock', 'cliff', 'log', 'stump'];
export const SOLID_KINDS = new Set(['pine', 'broad', 'dead', 'rock', 'cliff', 'log', 'stump', 'mountain']);
export const SOFT_KINDS = new Set(['bush', 'grass', 'flower', 'mushroom', 'pebble', 'branch']);

// Áreas dos biomas em hectares, medidas na máscara depois de o rio ser refeito.
export const BIOME_HA = [];

// Metas por hectare (copa e sub-bosque), contando as clareiras e as áreas funcionais.
// bioma: 0 baixada · 1 encostas · 2 rocha/altitude · 3 mata úmida · 4 planalto seco · 5 água
export const TARGET_RANGES = {
  0: { canopy: [180, 280], understory: [170, 330] },
  1: { canopy: [230, 350], understory: [150, 280] },
  2: { canopy: [80, 170], understory: [60, 140] },
  3: { canopy: [230, 380], understory: [240, 420] },
  4: { canopy: [100, 190], understory: [80, 180] },
  5: { canopy: [0, 0], understory: [0, 0] },
};

// ---------------------------------------------------------------- cores
// Copa: do verde-azulado ao verde-amarelado, em manchas; ±7 % por árvore. Tronco: só o tom.
const COOL = [0.84, 0.99, 0.95], WARM = [1.08, 1.03, 0.8], DEAD = [0.56, 0.5, 0.42];
function kitTint(kind) {
  const t = PR.KIT.tint ? PR.KIT.tint(kind) : null;
  if (!t) return [1, 1, 1];
  const m = Math.max(t.r, t.g, t.b) || 1;
  return [t.r / m * 0.95, t.g / m * 0.95, t.b / m * 0.95];
}
function foliageColor(kind, x, z, R, { dark = false, lush = 0 } = {}) {
  if (dark) { const k = 0.9 + R() * 0.2; return DEAD.map((c) => c * k); }
  const hue = Math.min(1, Math.max(0, canopyHue(x, z) + (R() - 0.5) * 0.3));
  const light = 0.93 + R() * 0.14;
  const base = kitTint(kind);
  return [0, 1, 2].map((i) => {
    let c = lerp(COOL[i], WARM[i], hue) * base[i] * light;
    if (lush) c *= [0.92, 1.06, 0.9][i] ** lush;
    return c;
  });
}

// ---------------------------------------------------------------- espaçamento
// Grade de 4 m com as posições sólidas: cada uma guarda o raio do colisor e o "espaço pessoal".
const G_CELL = 4, G_N = 256;
function solidGrid() {
  const cells = new Array(G_N * G_N);
  const key = (x, z) => {
    const i = Math.floor((x + 512) / G_CELL), j = Math.floor((z + 512) / G_CELL);
    return (i < 0 || j < 0 || i >= G_N || j >= G_N) ? -1 : j * G_N + i;
  };
  return {
    add(o) { const k = key(o.x, o.z); if (k < 0) return; (cells[k] ||= []).push(o); },
    // conflito quando alguém está a menos de (sp + o.sp) — ou do raio pedido, se maior
    clear(x, z, sp, minR = 0) {
      const i0 = Math.floor((x + 512) / G_CELL), j0 = Math.floor((z + 512) / G_CELL);
      for (let j = j0 - 2; j <= j0 + 2; j++) for (let i = i0 - 2; i <= i0 + 2; i++) {
        if (i < 0 || j < 0 || i >= G_N || j >= G_N) continue;
        const list = cells[j * G_N + i];
        if (!list) continue;
        for (const o of list) {
          const d = Math.hypot(o.x - x, o.z - z);
          if (d < sp + o.sp || d < minR + o.r) return false;
        }
      }
      return true;
    },
  };
}

// Folga das estradas: copa ≥ meia-largura + 3 m (2 m em trilhas), sub-bosque ≥ +1,25 (0,75).
// O campo em grade responde longe da pista; perto dela a conta é exata.
const ROUTE_MARGIN = { canopy: [3, 2], understory: [1.25, 0.75] };
function routeClear(x, z, stratum) {
  const e = routeEdgeAt(x, z);
  const [road, trail] = ROUTE_MARGIN[stratum];
  if (e > road + 3) return true;
  const info = nearestRouteInfo(x, z);
  if (!info) return true;
  return info.dist - info.width / 2 >= (info.width <= 3 ? trail : road) + 0.05;
}

// ---------------------------------------------------------------- planejamento
let cached = null;
export function planRegionInstances({ seed = 713337, fresh = false } = {}) {
  if (cached && !fresh && cached.seed === seed) return cached.plan;
  initRouteField();
  buildFunctionalAreas();
  BIOME_HA.length = 0;
  BIOME_HA.push(...measureBiomeHa());

  const R = rng(seed);
  const lists = Object.fromEntries(KINDS.map((k) => [k, []]));
  const solids = [];
  const grid = solidGrid();
  const counts = { canopy: [0, 0, 0, 0, 0, 0], understory: [0, 0, 0, 0, 0, 0] };
  const byBiome = { 0: [], 1: [], 2: [], 3: [], 4: [], 5: [] };
  const byStratum = { canopy: [], understory: [] };
  const grassSeeds = [];
  const rejected = { water: 0, bridge: 0, functional: 0, route: 0 };

  const push = (inst, solid) => {
    lists[inst.kind].push(inst);
    if (solid) {
      solids.push({ x: inst.x, z: inst.z, r: solid.r });
      grid.add({ x: inst.x, z: inst.z, r: solid.r, sp: solid.sp });
    }
    if (inst.stratum) {
      counts[inst.stratum][inst.biome]++;
      byBiome[inst.biome].push(inst);
      byStratum[inst.stratum].push(inst);
    }
  };
  const aboveWater = (x, y) => y >= waterLevelAt(x) - 0.05;

  // 1. Montanhas direcionadas (6 peças em altitude, fora de rotas e marcos)
  for (const m of MOUNTAINS) {
    const contact = groundContact(regionHeightAt, m.x, m.z, 8);
    push({ x: m.x, y: contact.y, z: m.z, s: 1, sy: 1, rot: R() * Math.PI * 2, variant: 0, kind: 'mountain', trunk: 1 },
      { r: 12, sp: 14 });
  }

  // 2. Camada autoral
  let authored = 0;
  for (const g of REGION.instances) {
    const kind = KIND_OF[g.id];
    if (!kind) continue;
    const [lo, hi] = g.scale_range || [1, 1];
    for (const p of g.points) {
      const x = p[0], z = PLAN_Z_SIGN * p[1];
      if (isWaterCell(x, z)) { rejected.water++; continue; }
      if (kind === 'grass') {
        if (lowGapAt(x, z) > 0.5 && routeEdgeAt(x, z) > 0.3) grassSeeds.push([x, z]);
        continue;
      }
      const s = (lo + R() * (hi - lo)) * calibration(kind);
      const isTree = TREE_KINDS.has(kind);
      const r = isTree ? 0.45 * s : colliderOf(kind, 0) * 0.85 * s;
      if (solidGapAt(x, z) < r + 0.5) { rejected.functional++; continue; }
      if (!routeClear(x, z, isTree ? 'canopy' : 'understory')) { rejected.route++; continue; }
      const contact = groundContact(regionHeightAt, x, z, isTree ? 0.45 : 0.3);
      if (!aboveWater(x, contact.y)) { rejected.water++; continue; }
      const variant = Math.floor(R() * variantsOf(kind));
      push({
        x, y: contact.y, z, s, sy: s * (0.92 + R() * 0.16), rot: R() * Math.PI * 2, tilt: (R() - 0.5) * 0.05,
        variant, kind, stratum: isTree ? 'canopy' : 'understory', biome: biomeAt(x, z),
        fol: isTree ? foliageColor(kind, x, z, R) : [1, 1, 1], trunk: 0.85 + R() * 0.25, authored: true,
      }, { r, sp: isTree ? 1.4 : r + 0.5 });
      authored++;
    }
  }

  // 3. Copa: grade com deslocamento livre, aceita pela densidade, espaçamento pela densidade
  let derived = 0;
  const TSTEP = 2.6;
  const NT = Math.ceil(1024 / TSTEP);
  for (let gz = 0; gz < NT; gz++) for (let gx = 0; gx < NT; gx++) {
    const x = -512 + (gx + R()) * TSTEP, z = -512 + (gz + R()) * TSTEP;
    const pick = R();
    if (Math.abs(x) > 509 || Math.abs(z) > 509 || isWaterCell(x, z)) continue;
    const gap = solidGapAt(x, z);
    if (gap < 0.6) continue;
    const wastes = inWastes(x, z);
    // nos Ermos a mata é morta e rala; no resto, o campo de densidade decide
    let F = wastes ? 0.18 : forestDensity(x, z);
    if (F <= 0.02) continue;
    // anel das áreas funcionais e beira de estrada: a mata rareia aos poucos até a borda, em vez de
    // parar numa linha reta na folga mínima
    F *= smoothstep(0.5, 9, gap) * thinAt(x, z) * lerp(0.3, 1, smoothstep(2.5, 10, routeEdgeAt(x, z)));
    if (pick > (wastes ? 0.05 : Math.pow(F, 1.3) * 0.9 * standClump(x, z))) continue;

    const b = biomeAt(x, z);
    const h = regionHeightAt(x, z);
    let kind;
    if (wastes) kind = R() < 0.82 ? 'dead' : 'pine';
    else {
      let pBroad = [0.42, 0.28, 0.14, 0.38, 0.5, 0][b] + 0.3 * (1 - F);
      if (h > 70) pBroad *= 0.5;
      kind = R() < pBroad ? 'broad' : 'pine';
    }
    // maturidade: árvores maiores no miolo, jovens na orla
    const age = standAge(x, z);
    const core = Math.pow(F, 0.7);
    const sBase = kind === 'broad' ? lerp(0.72, 1.12, core) : lerp(0.6, 1.04, core);
    const s = sBase * (0.86 + 0.3 * age + (R() - 0.5) * 0.16);
    const r = 0.45 * s;
    // espaçamento irregular: mais apertado nas touceiras, com folga sorteada árvore a árvore
    const clump = standClump(x, z);
    const sp = lerp(6.5, 3.1, F) * 0.5 * (kind === 'broad' ? 0.82 : 1) * (0.72 + 0.2 * s + 0.4 * R()) * (1.3 - 0.45 * clump);
    if (gap < r + 0.5) continue;
    if (!routeClear(x, z, 'canopy')) continue;
    if (!grid.clear(x, z, sp, r + 0.2)) continue;
    const contact = groundContact(regionHeightAt, x, z, 0.45);
    if (contact.slope > 1.0 || !aboveWater(x, contact.y)) continue;
    push({
      x, y: contact.y, z, s, sy: s * (0.88 + R() * 0.26), rot: R() * Math.PI * 2, tilt: (R() - 0.5) * 0.085,
      variant: Math.floor(R() * variantsOf(kind)), kind, stratum: 'canopy', biome: b,
      fol: foliageColor(kind, x, z, R, { dark: wastes && kind === 'dead' }), trunk: (wastes ? 0.72 : 0.82) + R() * 0.25,
    }, { r, sp });
    derived++;
  }

  // 4. Sub-bosque
  const USTEP = 2.2;
  const NU = Math.ceil(1024 / USTEP);
  for (let gz = 0; gz < NU; gz++) for (let gx = 0; gx < NU; gx++) {
    const x = -512 + (gx + R()) * USTEP, z = -512 + (gz + R()) * USTEP;
    const roll = R(), roll2 = R();
    if (Math.abs(x) > 509 || Math.abs(z) > 509 || isWaterCell(x, z)) continue;
    const low = lowGapAt(x, z);
    if (low < 0.4) continue;
    const gap = solidGapAt(x, z);
    const b = biomeAt(x, z);
    const F = forestDensity(x, z);
    const ring = ringAt(x, z);
    const wet = wetnessAt(x, z);
    const slope = slopeHere(x, z);
    const wastes = inWastes(x, z);
    const edge = 4 * F * (1 - F);
    const clump = vnoise(x / 9, z / 9, 61) > 0.5 ? 1.55 : 0.45;
    const riparian = wet > 0.12 && wet < 0.9 ? wet : 0;
    const bK = [1, 0.9, 0.35, 1.25, 0.6, 0][b];
    const rockField = smoothstep(0.58, 0.78, fbm2(x / 35, z / 35, 2, 63));

    const pBush = wastes ? 0.01 : bK * clump * (0.02 + 0.34 * edge + 0.09 * F + 0.24 * ring + 0.34 * riparian);
    const pRock = 0.002 + rockField * 0.045 * (b === 2 ? 2 : 1) + 0.006 * ring + (wet > 0.9 ? 0.022 : 0)
      + 0.015 * smoothstep(0.5, 1.0, slope) + (wastes ? 0.02 : 0);
    const pCliff = b === 2 && slope > 0.55 ? 0.02 * (0.4 + rockField) : 0;
    const pLog = 0.012 * smoothstep(0.5, 0.9, F) + (riparian > 0.3 ? 0.006 : 0);
    const pStump = 0.012 * F * F + (wastes ? 0.015 : 0);
    let kind = null, acc = 0;
    for (const [k, p] of [['bush', pBush], ['rock', pRock], ['cliff', pCliff], ['log', pLog], ['stump', pStump]]) {
      acc += p;
      if (roll < acc) { kind = k; break; }
    }
    if (!kind) continue;
    if (!routeClear(x, z, 'understory')) continue;

    const variant = Math.floor(roll2 * variantsOf(kind));
    let s, solid = null, trunk = 0.85 + R() * 0.25;
    if (kind === 'bush') {
      s = (0.55 + R() * 0.5) * (1 + 0.15 * ring) * (riparian ? 0.9 : 1);
      if (!grid.clear(x, z, 0, 0.7)) continue;
    } else {
      const waterline = kind === 'rock' && wet > 0.85;
      s = kind === 'rock' ? (waterline ? 0.35 + R() * 0.35 : 0.45 + R() * 0.8)
        : kind === 'cliff' ? 0.55 + R() * 0.45
          : kind === 'log' ? 0.8 + R() * 0.5 : 0.7 + R() * 0.5;
      const r = (kind === 'log' ? 0.5 : kind === 'stump' ? 0.55 : colliderOf(kind, variant) * (kind === 'cliff' ? 0.8 : 0.85)) * s;
      if (gap < r + 0.4) continue;
      if (!grid.clear(x, z, r + 0.5, r + 0.3)) continue;
      // pedras miúdas na linha d'água não travam o caminho
      if (!(waterline && s < 0.6)) solid = { r, sp: r + 0.5 };
      if (waterline) trunk *= 0.78;
    }
    const contact = groundContact(regionHeightAt, x, z, kind === 'cliff' ? 1.5 : 0.3);
    if (!aboveWater(x, contact.y)) continue;
    if (kind === 'cliff' && contact.slope > 3) continue;
    // rochas assentam um pouco no chão, mas nunca abaixo da lâmina do rio
    const sink = kind === 'rock' || kind === 'cliff' ? Math.min(0.08 * s, Math.max(0, contact.y - waterLevelAt(x) + 0.05)) : 0;
    push({
      x, y: contact.y - sink, z, s,
      sy: s * (0.85 + R() * 0.3), rot: R() * Math.PI * 2, tilt: kind === 'bush' ? 0 : (R() - 0.5) * 0.12,
      variant, kind, stratum: 'understory', biome: b,
      fol: kind === 'bush' ? foliageColor('bush', x, z, R, { lush: riparian }) : [1, 1, 1], trunk,
    }, solid);
    derived++;
  }

  const densityByBiome = {};
  for (let b = 0; b <= 5; b++) {
    const ha = BIOME_HA[b] || 1;
    densityByBiome[b] = {
      ha, canopy: counts.canopy[b], understory: counts.understory[b],
      canopyPerHa: counts.canopy[b] / ha, understoryPerHa: counts.understory[b] / ha,
    };
  }
  const byKind = {};
  for (const [k, arr] of Object.entries(lists)) byKind[k] = arr.length;
  const report = {
    authored, derived, total: authored + derived, byKind,
    byStratum: { canopy: byStratum.canopy.length, understory: byStratum.understory.length },
    byBiome: Object.fromEntries(Object.entries(byBiome).map(([b, l]) => [b, l.length])),
    mountains: lists.mountain.length, grassSeeds: grassSeeds.length, rejected,
  };
  const plan = { lists, byStratum, byBiome, solids, mountains: lists.mountain, densityByBiome, grassSeeds, report };
  cached = { seed, plan };
  return plan;
}

// ---------------------------------------------------------------- montagem em cena
// Peças por nível para cada tipo. Árvores: L0, L1, L2 e o impostor (quando houver renderer).
function treeLevels(kind, variant) {
  const lv = [0, 1, 2].map((l) => PR.levelParts(kind, variant, l));
  const imp = PR.KIT.impostor ? PR.KIT.impostor(kind, variant) : null;
  lv.push(imp || lv[2]);
  return lv;
}
const shrubLevels = (kind, variant) => [0, 1, 2].map((l) => PR.levelParts(kind, variant, l));
const HEIGHT = { pine: 12.5, broad: 5, dead: 12.5, bush: 1.6, rock: 2.2, cliff: 7, log: 0.6, stump: 1 };

export function buildRegionInstances(scene, { density = 1 } = {}) {
  initRouteField();
  const plan = planRegionInstances();
  for (const s of plan.solids) addCircle(s.x, s.z, s.r);

  const groups = {};
  for (const [kind, list] of Object.entries(plan.lists)) {
    if (!list.length) continue;
    if (kind === 'mountain') {
      const g = PR.instanced(kind, list, { cast: true, chunk: 128 });
      g.name = 'nature-mountain';
      scene.add(g);
      groups[kind] = g;
      continue;
    }
    const tree = TREE_KINDS.has(kind);
    const byVariant = new Map();
    for (const it of list) {
      const v = it.variant | 0;
      if (!byVariant.has(v)) byVariant.set(v, []);
      byVariant.get(v).push(it);
    }
    const g = new THREE.Group();
    g.name = `nature-${kind}`;
    for (const [v, items] of byVariant) {
      const levels = tree ? treeLevels(kind, v) : shrubLevels(kind, v);
      const shadowParts = kind === 'bush' ? null : PR.levelParts(kind, v, 2);
      const field = createLODField(`${kind}#${v}`, items, {
        levels, band: tree ? 'tree' : 'shrub', chunk: tree ? 128 : 96, shadowParts,
        secondary: SHRUB_KINDS.includes(kind) && kind !== 'cliff', height: HEIGHT[kind] || 1,
      });
      g.add(field.group);
    }
    scene.add(g);
    groups[kind] = g;
  }
  initGroundCover(scene, { seeds: plan.grassSeeds });
  if (density < 1) setSecondaryDensity(density);
  Object.assign(INSTANCE_REPORT, plan.report);
  return { groups, lists: plan.lists, report: plan.report, plan, fields: LOD_FIELDS };
}

export function buildTurbulentInstances(scene) {
  const lists = { rock: [], pine: [], broad: [], grass: [] };
  const R = rng(26092402);
  for (const g of TURBULENT.instances) {
    const kind = KIND_OF[g.id];
    if (!kind) continue;
    const isSolid = SOLID_KINDS.has(kind);
    const [lo, hi] = g.scale_range || [1, 1];
    for (const p of g.points) {
      const x = p[0] + TURB_RUNTIME_OFFSET.x;
      const z = PLAN_Z_SIGN * p[1] + TURB_RUNTIME_OFFSET.z;
      const s = lo + R() * (hi - lo);
      const contact = groundContact(turbulentHeightAt, x, z, 0.5);
      lists[kind].push({
        x, y: contact.y, z, s, sy: s,
        rot: R() * Math.PI * 2, variant: Math.floor(R() * variantsOf(kind)),
        dark: true, shade: 0.82 + R() * 0.14, solid: isSolid,
      });
      if (isSolid) addCircle(x, z, 0.6 * s);
    }
  }
  const groups = {};
  for (const [kind, list] of Object.entries(lists)) {
    if (!list.length) continue;
    const g = PR.instanced(kind, list, { cast: kind !== 'bush' && kind !== 'grass', chunk: 64 });
    g.name = `turb-nature-${kind}`;
    scene.add(g);
    groups[kind] = g;
  }
  return { groups, lists };
}

// Itens secundários (arbustos, rochas, troncos, tocos) rareiam com a qualidade; as árvores nunca.
export function setSecondaryDensity(factor) {
  setLODDensity(factor);
}
