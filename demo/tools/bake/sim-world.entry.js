// Entrada empacotada por `bake-sim-world.mjs`. Roda no Node a mesma sequência de boot de main.js
// (texturas → modelos → mundo → camada de jogo → Turbulenta), sem desenhar nada, e expõe:
//
//   snapshot()   — o estado estático que a simulação em C++ precisa (relevo já escavado, rio,
//                  colisores na ordem de inserção, pontes, campo de rotas, lugares e zonas);
//   fixtures()   — amostras das consultas do JS (terreno, zonas, colisão e sequências de
//                  moveEntity) para os testes de paridade do C++.
//
// Os símbolos `__…` não existem em demo/src: o plugin do bake os acrescenta só neste pacote.
import * as THREE from 'three';
import { G } from '../../src/state.js';
import { generateTextures } from '../../src/engine/textures.js';
import { loadModels } from '../../src/engine/models.js';
import { loadWorldScene } from '../../src/world/index.js';
import { buildWorld } from '../../src/game/world.js';
import { initTurbulent } from '../../src/game/turbulent.js';
import { KIT_STATUS } from '../../src/world/nature-kit.js';
import { WORLD } from '../../src/world/runtime-manifest.js';
import { PLAN_Z_SIGN, TURB_RUNTIME_OFFSET, TURB_GATE_X, isTurbulentSpace } from '../../src/world/coordinates.js';
import {
  REGION_FIELD, TURB_FIELD, RIVER, BIOME_WATER, biomeAt, isWaterCell, slopeAt, riverCenterAt, __MASK, __MASK_W,
} from '../../src/world/heightfield.js';
import {
  REGION_ROUTE_LINES, TURB_ROUTE_LINES, routeDistance, __routeField, __RF_STEP, __RF_N, __RF_MAX,
} from '../../src/world/world-routes.js';
import { bridges, groundHeight, terrainHeight, waterAt } from '../../src/engine/terrain.js';
import { __inserted, resolve, testPoint, moveEntity } from '../../src/game/collide.js';
import { LOC, HALF, TURB, zoneAt, placeAt, __PROTECTED, __NEVER_PROTECTED } from '../../src/game/layout.js';
import { hash2, vnoise, fbm2 } from '../../src/world/noise.js';
import { G as GAME } from '../../src/state.js';
import { NODES } from '../../src/game/world.js';
import { NPCS, TRAVELERS } from '../../src/game/npcs.js';
import { groups } from '../../src/game/enemies.js';
import { SHELTER } from '../../src/game/zones.js';
import { GUARD_POSTS, CAMP_DISPLACED, CAMP_PATROL, PORTAL_SPOTS, ROUTES } from '../../src/game/layout.js';
import { PORTAL_CORE, __SHRINE_POS, __EXIT_POS, __START_POS } from '../../src/game/turbulent.js';
import { __DISC } from '../../src/game/discovery.js';
import { mulberry as harnessRng } from './sim-harness.js';

export { runScenario, aimTarget, mulberry } from './sim-harness.js';

// Semente do cenário `boot` do harness: vale para os sorteios de buildWorld (espera dos viajantes e
// criação dos grupos) em diante. Nada do pacote de mundo depende dela.
export const BOOT_SEED = 424242;

export async function loadWorld(log = () => {}) {
  const scene = new THREE.Scene();
  G.scene = scene;
  await generateTextures(() => {});
  log('texturas');
  await loadModels(() => {});
  log('modelos');
  await loadWorldScene(scene, (t) => log(`mundo: ${t}`), { density: 1, debug: false, tier: 'alta', renderer: null });
  // As variantes e os colisores das árvores vêm do kit; sem ele o conjunto de colisores seria outro.
  if (!KIT_STATUS.loaded) throw new Error(`o kit de natureza não carregou: ${KIT_STATUS.error}`);
  globalThis.__visRand = harnessRng(99);
  Math.random = harnessRng(BOOT_SEED);
  buildWorld();
  log('camada de jogo');
  initTurbulent();
  log('Turbulenta');
}

const pt = (p) => ({ x: p.x, z: p.z });
const circle = (p) => ({ x: p.x, z: p.z, r: p.r });

export function snapshot() {
  const field = (f) => ({ w: f.w, h: f.h, step: f.step, x0: f.x0, y0: f.y0, min: f.min, scale: f.scale, data: f.data });
  const lines = (table) => Object.fromEntries(Object.entries(table).map(([id, l]) => [id, { width: l.width, risk: l.risk, pts: l.pts }]));
  return {
    meta: {
      worldLayoutVersion: WORLD.world_layout_version,
      runtimeSchema: WORLD.runtime_schema,
      kit: { loaded: KIT_STATUS.loaded, types: KIT_STATUS.types },
    },
    coordinates: { planZSign: PLAN_Z_SIGN, turbulentOffset: { ...TURB_RUNTIME_OFFSET }, turbulentGateX: TURB_GATE_X },
    region: {
      half: HALF,
      height: field(REGION_FIELD),
      mask: { w: __MASK_W, waterBiome: BIOME_WATER, data: __MASK },
      river: { x0: RIVER.x0, n: RIVER.n, level: RIVER.level, center: RIVER.center, half: RIVER.half },
      routeField: { n: __RF_N, step: __RF_STEP, max: __RF_MAX, origin: -512, data: __routeField },
      routes: lines(REGION_ROUTE_LINES),
    },
    turbulent: {
      half: TURB.half,
      height: field(TURB_FIELD),
      routes: lines(TURB_ROUTE_LINES),
    },
    // Na mesma ordem de terrain.js: a altura final é o máximo percorrendo esta lista.
    bridges: bridges.map((b) => ({
      name: b.name, x: b.x, z: b.z, hw: b.hw, hd: b.hd, y: b.y, rot: b.rot,
      c: Math.cos(-b.rot), s: Math.sin(-b.rot), apron: b.apron, apronSide: b.apronSide,
    })),
    colliders: __inserted,
    places: Object.fromEntries(Object.entries(LOC).map(([k, l]) => [k, {
      x: l.x, z: l.z, r: l.r, ...(l.rx != null ? { rx: l.rx, rz: l.rz } : {}), name: l.name,
    }])),
    zones: { protected: __PROTECTED.map(circle), neverProtected: __NEVER_PROTECTED.map(circle) },
  };
}

// ---------------------------------------------------------------- camada de jogo
// Posições que world.js, layout.js, turbulent.js, npcs.js e discovery.js montam, na ordem de criação.
// `texts` vai para data/text/pt-BR (só o cliente lê).
export function gameplayLayout() {
  const labelKey = (it) => it.kind + (typeof it.data === 'string' ? '.' + it.data : '');
  const labels = {};
  const interactables = GAME.interactables.map((it) => {
    const o = { kind: it.kind, x: it.x, z: it.z, r: it.r };
    if (typeof it.data === 'string') o.data = it.data;
    if (it.kind === 'node') o.node = NODES.indexOf(it.data);
    else { o.label = labelKey(it); labels[o.label] = it.label; }
    return o;
  });
  const nodes = NODES.map((n) => ({ kind: n.kind, x: n.x, z: n.z, yieldBonus: n.yieldBonus, rot: n.mesh.rotation.y }));
  const travelerKeys = TRAVELERS.map((t, i) => `viajante${i + 1}`);
  const npcs = NPCS.map((n, i) => {
    const o = { x: n.x, z: n.z, yaw: n.yaw || 0, role: n.kind === 'traveler' ? 'traveler' : n.role };
    if (n.face) o.face = true;
    if (n.work) o.work = true;
    for (const k of ['race', 'cloth', 'trim', 'hood', 'weapon', 'name']) if (n[k]) o[k] = n[k];
    o.model = n.race === 'elfo' ? 'eve' : i % 2 ? 'eve' : 'kachujin';
    if (n.kind === 'traveler') o.traveler = TRAVELERS.indexOf(n.travel);
    return o;
  });
  const travelers = TRAVELERS.map((t, i) => ({ key: travelerKeys[i], path: t.path }));
  const travelerTexts = Object.fromEntries(TRAVELERS.map((t, i) => [travelerKeys[i], { name: t.name, role: t.role, gear: t.gear, public: t.public }]));
  const groupList = groups.map((g) => ({
    id: g.id, x: g.center.x, z: g.center.z, leash: g.leash ?? null, respawn: g.respawn,
    gorge: g.active.toString().includes('EVT'), members: g.members,
  }));
  const discoveries = __DISC.map(([key, c, r]) => ({ key, x: c.x, z: c.z, r, ...(key === 'ermos' ? { byZone: true } : {}) }));
  const discoveryTexts = Object.fromEntries(__DISC.map(([key, , , title, text]) => [key, { title, text }]));
  return {
    layout: {
      schema: 1,
      interactables, nodes, npcs, travelers, groups: groupList,
      canteiroGuard: { x: LOC.canteiro.x - 7, z: LOC.canteiro.z - 6, yaw: Math.PI },
      guardPosts: GUARD_POSTS,
      camp: { center: { x: LOC.acampamento.x, z: LOC.acampamento.z }, displaced: CAMP_DISPLACED, patrol: CAMP_PATROL },
      portalSpots: PORTAL_SPOTS,
      shelter: { x: SHELTER.x, z: SHELTER.z },
      turbulent: { shrines: __SHRINE_POS, exits: __EXIT_POS, starts: __START_POS, portalCore: PORTAL_CORE, center: { x: TURB.x, z: TURB.z } },
      discoveries,
      shortGorge: ROUTES.short_gorge.pts,
    },
    texts: { interactables: labels, travelers: travelerTexts, discoveries: discoveryTexts },
  };
}

export { riverCenterAt };

// ---------------------------------------------------------------- fixtures de paridade
function mulberry(seed) {
  return () => {
    seed |= 0; seed = seed + 0x6D2B79F5 | 0;
    let t = Math.imul(seed ^ seed >>> 15, 1 | seed);
    t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
    return ((t ^ t >>> 14) >>> 0) / 4294967296;
  };
}
const num = (v) => (Number.isFinite(v) ? v : null);   // JSON não tem ±Infinity

export function fixtures(seed = 20260927) {
  const R = mulberry(seed);
  const rr = (a, b) => a + R() * (b - a);
  const TX = TURB_RUNTIME_OFFSET.x;

  // pontos: região inteira (com borda de fora), margens do rio, pontes e Turbulenta
  const points = [];
  for (let i = 0; i < 1500; i++) points.push([rr(-530, 530), rr(-530, 530)]);
  for (let i = 0; i < 300; i++) { const x = rr(-510, 510); points.push([x, riverCenterAt(x) + rr(-22, 22)]); }
  for (const b of bridges) for (let i = 0; i < 80; i++) {
    points.push([b.x + rr(-(b.hw + b.apronSide + 2), b.hw + b.apronSide + 2), b.z + rr(-(b.hd + b.apron + 2), b.hd + b.apron + 2)]);
  }
  for (let i = 0; i < 500; i++) points.push([TX + rr(-140, 140), rr(-140, 140)]);
  for (const k of Object.keys(LOC)) for (let i = 0; i < 12; i++) points.push([LOC[k].x + rr(-60, 60), LOC[k].z + rr(-60, 60)]);
  // bordas exatas de arredondamento da máscara e do campo de rotas (x.5)
  for (let i = 0; i < 100; i++) points.push([Math.floor(rr(-510, 510)) + 0.5, Math.floor(rr(-510, 510)) + 0.5]);

  const terrain = points.map(([x, z]) => {
    const turb = isTurbulentSpace(x);
    return {
      x, z,
      terrain: terrainHeight(x, z), ground: groundHeight(x, z), water: num(waterAt(x, z)),
      zone: zoneAt(x, z), place: placeAt(x, z),
      ...(turb ? {} : { route: routeDistance(x, z), biome: biomeAt(x, z), wet: isWaterCell(x, z), slope: slopeAt(x, z) }),
    };
  });

  // colisão: perto de colisores reais (e ao acaso), com os raios usados pelo jogo
  const radii = [0.28, 0.42, 0.45, 0.7, 1];
  const collision = [];
  for (let i = 0; i < 2500; i++) {
    let x, z;
    if (i % 5 === 4) { x = rr(-520, 520); z = rr(-520, 520); } else {
      const o = __inserted[Math.floor(R() * __inserted.length)];
      x = o.x + rr(-4, 4); z = o.z + rr(-4, 4);
    }
    const r = radii[Math.floor(R() * radii.length)];
    const [rx, rz] = resolve(x, z, r);
    collision.push({ x, z, r, hit: testPoint(x, z, r), rx, rz });
  }

  // movimento: caminhantes com passos de andar, correr, montar e investida
  const starts = [];
  for (const k of Object.keys(LOC)) starts.push([LOC[k].x, LOC[k].z]);
  for (const b of bridges) starts.push([b.x, b.z + b.hd + b.apron * 0.5], [b.x, b.z - b.hd - b.apron * 0.5]);
  for (const id of Object.keys(REGION_ROUTE_LINES)) {
    const p = REGION_ROUTE_LINES[id].pts;
    for (let i = 0; i < 6; i++) starts.push(p[Math.floor(R() * p.length)]);
  }
  for (let i = 0; i < 20; i++) { const x = rr(-500, 500); starts.push([x, riverCenterAt(x) + rr(-10, 10)]); }
  for (let i = 0; i < 16; i++) starts.push([TX + rr(-120, 120), rr(-120, 120)]);
  for (let i = 0; i < 12; i++) starts.push([rr(-515, 515), R() < 0.5 ? rr(-515, -495) : rr(495, 515)]);   // limites
  const speeds = [3, 7.2, 12.5, 26];
  const movement = starts.map(([sx, sz]) => {
    const ent = { pos: { x: sx, y: 0, z: sz } };
    const r = [0.45, 1, 0.7][Math.floor(R() * 3)];
    let dir = R() * Math.PI * 2, speed = speeds[Math.floor(R() * speeds.length)];
    const steps = [];
    for (let s = 0; s < 45; s++) {
      if (R() < 0.12) dir += rr(-1.2, 1.2);
      if (R() < 0.05) speed = speeds[Math.floor(R() * speeds.length)];
      const lift = R() < 0.15 ? rr(0, 1.4) : 0;
      const dx = Math.sin(dir) * speed / 30, dz = Math.cos(dir) * speed / 30;
      moveEntity(ent, dx, dz, r, lift);
      steps.push({ dx, dz, lift, x: ent.pos.x, z: ent.pos.z });
    }
    return { x: sx, z: sz, r, steps };
  });

  // ruído determinístico usado pelo mundo e pela cobertura do chão
  const noise = [];
  for (let i = 0; i < 300; i++) {
    const x = rr(-600, 600), z = rr(-600, 600), sd = Math.floor(rr(0, 50));
    noise.push({ x, z, seed: sd, hash: hash2(Math.floor(x), Math.floor(z), sd), v: vnoise(x, z, sd), fbm: fbm2(x * 0.01, z * 0.01, 3, sd) });
  }

  return { seed, terrain, collision, movement, noise };
}
