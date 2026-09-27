// Montagem visual do mundo preparado: terreno em blocos com LOD, lâmina de água do rio,
// estradas, arquitetura e a cena da Região Turbulenta.
//
// O relevo desenhado vem do mesmo heightfield que responde `groundHeight()`; a diferença entre a
// malha de 2 m e a amostragem de 1 m fica em 7 cm no pior ponto da região, medida em
// `tools/build-world-runtime.mjs`. Os 16 blocos de 256 m do manifesto são grupos independentes,
// e cada um se divide em sub-blocos de 64 m que alternam entre dois níveis de detalhe.
import * as THREE from 'three';
import { terrainMat, waterMat } from '../engine/materials.js';
import { REGION, TURBULENT, PLACEMENTS } from './runtime-manifest.js';
import { TURB_RUNTIME_OFFSET, PLAN_Z_SIGN } from './coordinates.js';
import {
  REGION_FIELD, regionHeightAt, turbulentHeightAt,
  biomeAt, isWaterCell, waterLevelAt, WATER_LEVEL, riverCenterAt, riverHalfWidthAt, wetnessAt,
} from './heightfield.js';
import { initRouteField, routeDistance, buildRouteMeshes } from './world-routes.js';
import { resolveWorldMaterial } from './world-materials.js';
import { vnoise } from './noise.js';
import { forestDensity } from './vegetation-fields.js';
import { loadWorldGLB } from './runtime-loader.js';
import { replacedVolume, loadExtraReplacements, updateExtraDetail, EXTRA_STATUS } from './extra-props.js';
import regionPropsBin from '../../assets/world-runtime/region-props.glb';
import regionWaterBin from '../../assets/world-runtime/region-water.glb';
import turbulentPropsBin from '../../assets/world-runtime/turbulent-props.glb';

export const WORLD_SCENE = {
  terrain: null, blocks: [], tiles: [], water: null, routes: null, props: null, waterworks: null,
  turbTerrain: null, turbTiles: [], turbProps: null, turbRoutes: null, debug: null,
};

const SUB = 64;                       // lado do sub-bloco, em metros
const LOD = [2, 8];                   // passo da malha: perto, longe
const TURB_LOD = [1, 4];

// ---------------------------------------------------------------- pesos das camadas do terreno
// 0 grama · 1 grama seca · 2 rocha · 3 terra · 4 areia · 5 neve · 6 cinza · 7 calçamento
// 8 solo de bosque · 9 seixos  (mesmo contrato do material de splatting da demo)
const W10 = new Float32Array(10);
const toward = (k, f) => {
  if (f <= 0) return;
  f = f > 1 ? 1 : f;
  for (let i = 0; i < 10; i++) W10[i] *= 1 - f;
  W10[k] += f;
};
const sstep = (e0, e1, x) => { const t = Math.min(1, Math.max(0, (x - e0) / (e1 - e0))); return t * t * (3 - 2 * t); };

// Centros usados só para o acabamento do chão (praças e ermos); as posições vêm das âncoras.
const A = REGION.anchors;
// Praça calçada só no mercado; os entrepostos e o acampamento ficam com chão batido.
const PLAZAS = [
  ...(A.market ? [{ x: A.market[0], z: PLAN_Z_SIGN * A.market[1], r0: 9, r1: 20, layer: 7, k: 0.85 }] : []),
  ...(A.outpost_south ? [{ x: A.outpost_south[0], z: PLAN_Z_SIGN * A.outpost_south[1], r0: 14, r1: 34, layer: 3, k: 0.6 }] : []),
  ...(A.outpost_north ? [{ x: A.outpost_north[0], z: PLAN_Z_SIGN * A.outpost_north[1], r0: 12, r1: 30, layer: 3, k: 0.6 }] : []),
  ...(A.camp ? [{ x: A.camp[0], z: PLAN_Z_SIGN * A.camp[1], r0: 8, r1: 18, layer: 3, k: 0.55 }] : []),
];
const WASTES = A.wastes ? { x: A.wastes[0], z: PLAN_Z_SIGN * A.wastes[1] } : null;
const FIELDS = PLACEMENTS.filter((p) => p.kind === 'field')
  .map((p) => ({ x: p.plan[0], z: PLAN_Z_SIGN * p.plan[1], hw: p.half[0], hd: p.half[1] }));

function regionWeights(x, z, h, ny) {
  W10.fill(0);
  const biome = biomeAt(x, z);
  // bioma da fonte: 0 base · 1 encostas médias · 2 rocha/altitude · 3 mata úmida · 4 planalto seco · 5 água
  if (biome === 2) { W10[2] = 1; } else if (biome === 3) { W10[8] = 1; } else if (biome === 4) { W10[1] = 1; } else if (biome === 5) { W10[9] = 1; } else { W10[0] = 1; }
  if (biome === 1) toward(8, 0.45);

  // sob a mata fechada o chão puxa para solo de bosque (agulhas e folhas), sem perder o verde
  const forest = forestDensity(x, z);
  if (forest > 0.5) toward(8, 0.26 * sstep(0.5, 0.95, forest));

  // relevo: rocha nas encostas e nos cumes. O mundo inteiro fica entre 7 e 116 m — não há
  // altitude para neve, e o limiar antigo pintava de branco a colina do observatório.
  if (ny < 0.88) toward(2, (0.88 - ny) * 5);
  if (h > 86) toward(2, (h - 86) / 16);

  // rio: seixos no leito; logo acima da água, praia de seixos e areia; na margem úmida e nas
  // baixadas do leito antigo, solo escuro com manchas de cascalho
  if (isWaterCell(x, z)) {
    toward(9, 0.85);
  } else {
    const wet = wetnessAt(x, z);
    if (wet > 0) {
      const level = waterLevelAt(x);
      const beach = 1 - sstep(level + 0.05, level + 0.75, h);
      const spots = vnoise(x / 5.5, z / 5.5, 71);
      toward(9, beach * (0.55 + 0.3 * spots));
      toward(4, beach * 0.22 * (1 - spots));
      toward(8, wet * 0.6 * (1 - beach));
      if (spots > 0.62) toward(9, wet * 0.45 * (1 - beach));
    }
  }

  // Ermos: cinza quebrado
  if (WASTES) {
    const d = Math.hypot(x - WASTES.x, z - WASTES.z);
    if (d < 150) toward(6, (1 - sstep(70, 150, d)) * 0.85);
  }

  // campos cultivados do entreposto sul
  for (const f of FIELDS) {
    if (Math.abs(x - f.x) > f.hw + 12 || Math.abs(z - f.z) > f.hd + 12) continue;
    const t = (1 - sstep(f.hw - 6, f.hw + 12, Math.abs(x - f.x))) * (1 - sstep(f.hd - 6, f.hd + 12, Math.abs(z - f.z)));
    toward(1, t * 0.75);
  }

  // estradas e praças: chão batido e calçamento
  const rd = routeDistance(x, z);
  if (rd < 7) toward(3, 0.92 * (1 - sstep(2.6, 7, rd)));
  for (const p of PLAZAS) {
    const d = Math.hypot(x - p.x, z - p.z);
    if (d < p.r1) toward(p.layer, (1 - sstep(p.r0, p.r1, d)) * p.k);
  }
}

// Quanto do chão em (x, z) é pintado de grama viva (camada 0), 0…1. A grama de campo só nasce
// onde o terreno já é verde: fora das estradas, praças, campos cultivados, rocha, margem e Ermos.
const NRM = [0, 0, 0];
export function greenGroundAt(x, z) {
  regionWeights(x, z, regionHeightAt(x, z), normalAt(regionHeightAt, x, z, NRM));
  return W10[0];
}

const TW10 = new Float32Array(10);
function turbWeights(x, z, h, ny) {
  W10.fill(0);
  W10[6] = 1;
  toward(3, sstep(0.1, 0.55, (h % 7) / 7) * 0.45);
  if (ny < 0.86) toward(2, (0.86 - ny) * 5);
}

// ---------------------------------------------------------------- construção da malha
function packWeights(wa, wb, wc, k) {
  let total = 0, dominant = 0;
  for (let c = 0; c < 10; c++) if (W10[c] > W10[dominant]) dominant = c;
  const packed = TW10;
  for (let c = 0; c < 10; c++) { packed[c] = Math.round(W10[c] * 255); total += packed[c]; }
  packed[dominant] += 255 - total;
  for (let c = 0; c < 4; c++) { wa[k * 4 + c] = packed[c]; wb[k * 4 + c] = packed[c + 4]; }
  wc[k * 2] = packed[8]; wc[k * 2 + 1] = packed[9];
}

// Normais sempre a 1 m, independentes do passo do LOD: os níveis casam sem costura visível.
function normalAt(heightAt, x, z, out) {
  const l = heightAt(x - 1, z), r = heightAt(x + 1, z);
  const d = heightAt(x, z - 1), u = heightAt(x, z + 1);
  const nx = l - r, nz = d - u, ny = 2;
  const inv = 1 / Math.hypot(nx, ny, nz);
  out[0] = nx * inv; out[1] = ny * inv; out[2] = nz * inv;
  return out[1];
}

function tileGeometry(x0, z0, size, step, heightAt, weightFn) {
  const n = size / step + 1;
  const count = n * n;
  const pos = new Float32Array(count * 3), nor = new Float32Array(count * 3);
  const wa = new Uint8Array(count * 4), wb = new Uint8Array(count * 4), wc = new Uint8Array(count * 2);
  const nrm = [0, 0, 0];
  let y0 = Infinity, y1 = -Infinity;
  for (let j = 0; j < n; j++) for (let i = 0; i < n; i++) {
    const k = j * n + i;
    const x = x0 + i * step, z = z0 + j * step;
    const h = heightAt(x, z);
    if (h < y0) y0 = h; if (h > y1) y1 = h;
    pos[k * 3] = x; pos[k * 3 + 1] = h; pos[k * 3 + 2] = z;
    const ny = normalAt(heightAt, x, z, nrm);
    nor[k * 3] = nrm[0]; nor[k * 3 + 1] = nrm[1]; nor[k * 3 + 2] = nrm[2];
    weightFn(x, z, h, ny);
    packWeights(wa, wb, wc, k);
  }
  const idx = new Uint32Array((n - 1) * (n - 1) * 6);
  let p = 0;
  for (let j = 0; j < n - 1; j++) for (let i = 0; i < n - 1; i++) {
    const a = j * n + i, b = a + 1, c = a + n, d = c + 1;   // diagonal a-d, igual à amostragem
    idx[p++] = a; idx[p++] = c; idx[p++] = d; idx[p++] = a; idx[p++] = d; idx[p++] = b;
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  g.setAttribute('normal', new THREE.BufferAttribute(nor, 3));
  g.setAttribute('splatA', new THREE.BufferAttribute(wa, 4, true));
  g.setAttribute('splatB', new THREE.BufferAttribute(wb, 4, true));
  g.setAttribute('splatC', new THREE.BufferAttribute(wc, 2, true));
  g.setIndex(new THREE.BufferAttribute(idx, 1));
  g.boundingBox = new THREE.Box3(new THREE.Vector3(x0, y0, z0), new THREE.Vector3(x0 + size, y1, z0 + size));
  g.boundingSphere = g.boundingBox.getBoundingSphere(new THREE.Sphere());
  return g;
}

function buildTerrainGroup({ bounds, blocks, heightAt, weightFn, tint, lods, onProgress }) {
  const material = terrainMat(tint);
  const group = new THREE.Group();
  group.name = 'world-terrain';
  const tiles = [];
  const blockGroups = [];
  for (const block of blocks) {
    const bg = new THREE.Group();
    bg.name = `block-${block.id}`;
    bg.matrixAutoUpdate = false;
    // plano (x, y) → cena (x, z = -y): o bloco ocupa z de -y1 a -y0
    const bx0 = block.bounds_m[0], bx1 = block.bounds_m[2];
    const bz0 = PLAN_Z_SIGN * block.bounds_m[3], bz1 = PLAN_Z_SIGN * block.bounds_m[1];
    for (let z0 = bz0; z0 < bz1 - 1e-6; z0 += SUB) for (let x0 = bx0; x0 < bx1 - 1e-6; x0 += SUB) {
      const size = Math.min(SUB, bx1 - x0);
      const near = new THREE.Mesh(tileGeometry(x0, z0, size, lods[0], heightAt, weightFn), material);
      const far = new THREE.Mesh(tileGeometry(x0, z0, size, lods[1], heightAt, weightFn), material);
      for (const m of [near, far]) { m.receiveShadow = true; m.castShadow = false; m.matrixAutoUpdate = false; m.updateMatrix(); }
      far.visible = false;
      bg.add(near, far);
      tiles.push({ near, far, x: x0 + size / 2, z: z0 + size / 2, radius: size * 0.75, isNear: true });
    }
    bg.userData.block = block;
    bg.userData.center = { x: (bx0 + bx1) / 2, z: (bz0 + bz1) / 2 };
    bg.userData.radius = (bx1 - bx0) * 0.75;
    group.add(bg);
    blockGroups.push(bg);
    onProgress?.(block.id);
  }
  group.updateMatrixWorld(true);
  return { group, tiles, blocks: blockGroups, bounds };
}

export function buildRegionTerrain(scene, onProgress) {
  initRouteField();
  const built = buildTerrainGroup({
    bounds: REGION.bounds_m, blocks: REGION.chunks,
    heightAt: regionHeightAt, weightFn: regionWeights, tint: '#ffffff', lods: LOD, onProgress,
  });
  scene.add(built.group);
  WORLD_SCENE.terrain = built.group;
  WORLD_SCENE.tiles = built.tiles;
  WORLD_SCENE.blocks = built.blocks;
  return built;
}

export function buildTurbulentTerrain(scene) {
  const b = TURBULENT.bounds_m;
  const blocks = [];
  for (let row = 0; row < 2; row++) for (let col = 0; col < 2; col++) {
    blocks.push({
      id: `turb_${col}_${row}`, row, col,
      bounds_m: [b[0] + col * 128, b[1] + row * 128, b[0] + (col + 1) * 128, b[1] + (row + 1) * 128]
        .map((v, i) => v + (i % 2 === 0 ? TURB_RUNTIME_OFFSET.x : 0)),
    });
  }
  // o deslocamento em z é aplicado ao converter o plano, então entra aqui pelas bordas do bloco
  for (const blk of blocks) {
    blk.bounds_m[1] -= PLAN_Z_SIGN * TURB_RUNTIME_OFFSET.z;
    blk.bounds_m[3] -= PLAN_Z_SIGN * TURB_RUNTIME_OFFSET.z;
  }
  const built = buildTerrainGroup({
    bounds: b, blocks,
    heightAt: turbulentHeightAt, weightFn: turbWeights, tint: '#b8a0dc', lods: TURB_LOD,
  });
  built.group.name = 'turbulent-terrain';
  scene.add(built.group);
  WORLD_SCENE.turbTerrain = built.group;
  WORLD_SCENE.turbTiles = built.tiles;
  return built;
}

// ---------------------------------------------------------------- água
// Fita que segue o eixo do rio refeito (`river.js`): seções perpendiculares ao curso a cada 1,5 m,
// da margem de um lado à do outro com 1,2 m de sobra por baixo dos barrancos. A lâmina fica no
// nível da coluna e cada vértice leva a profundidade local (`aDepth`), que o material usa para
// clarear e abrir o raso: a margem visível é a linha onde o terreno encontra a água, sem recorte.
export function buildRegionWater(scene) {
  const STEP = 1.5, SEGS = 8, OVER = 1.2;
  const xs = [];
  for (let x = REGION_FIELD.x0; x < REGION_FIELD.x1; x += STEP) xs.push(x);
  xs.push(REGION_FIELD.x1);
  const n = xs.length, per = SEGS + 1;
  const pos = new Float32Array(n * per * 3), uvs = new Float32Array(n * per * 2), dep = new Float32Array(n * per);
  let along = 0;
  for (let i = 0; i < n; i++) {
    const x = xs[i];
    const c = riverCenterAt(x), w = riverHalfWidthAt(x) + 0.6 + OVER;
    const slope = (riverCenterAt(x + 0.75) - riverCenterAt(x - 0.75)) / 1.5;
    const inv = 1 / Math.hypot(1, slope);
    const nx = -slope * inv, nz = inv;                    // normal ao curso, no plano
    if (i) along += Math.hypot(STEP, c - riverCenterAt(xs[i - 1]));
    for (let s = 0; s <= SEGS; s++) {
      const off = -w + (2 * w * s) / SEGS;
      const vx = x + nx * off, vz = c + nz * off;
      const y = waterLevelAt(vx);
      const k = i * per + s;
      pos[k * 3] = vx; pos[k * 3 + 1] = y; pos[k * 3 + 2] = vz;
      uvs[k * 2] = along * 0.05; uvs[k * 2 + 1] = (off + w) * 0.05;
      dep[k] = Math.max(-1, Math.min(2.5, y - regionHeightAt(vx, vz)));
    }
  }
  const idx = new Uint32Array((n - 1) * SEGS * 6);
  let p = 0;
  for (let i = 0; i < n - 1; i++) for (let s = 0; s < SEGS; s++) {
    const a = i * per + s, b = a + 1, c = a + per, d = c + 1;
    idx[p++] = a; idx[p++] = b; idx[p++] = d; idx[p++] = a; idx[p++] = d; idx[p++] = c;
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.BufferAttribute(uvs, 2));
  g.setAttribute('aDepth', new THREE.BufferAttribute(dep, 1));
  g.setIndex(new THREE.BufferAttribute(idx, 1));
  g.computeVertexNormals();
  // normais para cima: a lâmina é plana em cada seção, e as curvas não podem virar a normal
  const nor = g.attributes.normal;
  for (let k = 0; k < nor.count; k++) nor.setXYZ(k, 0, 1, 0);
  g.computeBoundingSphere();

  const group = new THREE.Group();
  group.name = 'region-water';
  const m = new THREE.Mesh(g, waterMat({ shore: true }));
  m.receiveShadow = true;
  m.matrixAutoUpdate = false;
  m.renderOrder = 1;
  group.add(m);
  scene.add(group);
  WORLD_SCENE.water = group;
  return group;
}

// ---------------------------------------------------------------- estradas
export function buildRegionRoutes(scene) {
  const g = buildRouteMeshes('region');
  scene.add(g);
  WORLD_SCENE.routes = g;
  return g;
}
export function buildTurbulentRoutes(scene) {
  const g = buildRouteMeshes('turbulent');
  scene.add(g);
  WORLD_SCENE.turbRoutes = g;
  return g;
}

// ---------------------------------------------------------------- arquitetura e pontos de interesse
// Correções de assentamento registradas pelo pipeline (hoje: a ponte secundária, que a fonte
// deixou 5 m abaixo do leito). A exportação original não é tocada.
const DROPPED_WATER = /^REGION_(MainRiver|IrrigationCanal_\d+)$/;
const LIFTS = new Map(PLACEMENTS.filter((p) => p.lift).map((p) => [p.name, p.lift]));

export async function loadRegionProps(scene) {
  const { root } = await loadWorldGLB(regionPropsBin, {
    resolveMaterial: resolveWorldMaterial,
    onNode: (name, node) => {
      const lift = LIFTS.get(name); if (lift) node.position.y += lift;
      // troca só visual (`extra-props.js`): o nó some, a colisão do manifesto continua
      if (replacedVolume(name)) { node.visible = false; EXTRA_STATUS.hidden++; }
    },
  });
  root.name = 'region-props';
  root.updateMatrixWorld(true);
  scene.add(root);
  WORLD_SCENE.props = root;
  await loadExtraReplacements(scene);
  const water = await loadWorldGLB(regionWaterBin, { resolveMaterial: resolveWorldMaterial, cast: false });
  // A lâmina plana de 1 km do rio (a 10,1 m, duplicada sob o rio refeito) e os dois canais retos de
  // irrigação, que flutuavam sobre o relevo e entravam numa casa do Vale, saem da cena. A exportação
  // continua intacta; o rio desenhado é a fita de `buildRegionWater`.
  for (const node of [...water.root.children]) {
    if (DROPPED_WATER.test(node.name)) { water.root.remove(node); node.traverse((o) => o.geometry?.dispose()); }
  }
  water.root.name = 'region-waterworks';
  scene.add(water.root);
  WORLD_SCENE.waterworks = water.root;
  return root;
}

export async function loadTurbulentProps(scene) {
  const { root, nodes } = await loadWorldGLB(turbulentPropsBin, { resolveMaterial: resolveWorldMaterial });
  root.name = 'turbulent-props';
  root.position.set(TURB_RUNTIME_OFFSET.x, TURB_RUNTIME_OFFSET.y, TURB_RUNTIME_OFFSET.z);
  root.updateMatrixWorld(true);
  scene.add(root);
  WORLD_SCENE.turbProps = root;
  WORLD_SCENE.turbNodes = Object.fromEntries(nodes.map((n) => [n.name, n]));
  return root;
}

// Fragmentos suspensos e raízes da Turbulenta: são os elementos mais caros daquela cena, então
// a quantidade visível acompanha o nível de qualidade.
export function setTurbulentFx(amount = 1) {
  const nodes = WORLD_SCENE.turbNodes;
  if (!nodes) return;
  const floating = Object.keys(nodes).filter((n) => /Fragment|Root/.test(n)).sort();
  const keep = Math.ceil(floating.length * Math.max(0, Math.min(1, amount)));
  floating.forEach((name, i) => { nodes[name].visible = i < keep; });
}

// ---------------------------------------------------------------- detalhe por distância
// Alternância exclusiva entre os dois níveis, com histerese, e corte dos blocos distantes.
const DEFAULT_DETAIL = { tileNear: 260, blockFar: 1400, shadowFar: 90 };
export function updateWorldDetail(cam, detail = DEFAULT_DETAIL) {
  const near = detail.tileNear ?? DEFAULT_DETAIL.tileNear;
  const blockFar = detail.blockFar ?? DEFAULT_DETAIL.blockFar;
  updateExtraDetail(cam, detail.extraFar ?? 900);
  for (const b of WORLD_SCENE.blocks) {
    const c = b.userData.center;
    b.visible = Math.hypot(c.x - cam.x, c.z - cam.z) - b.userData.radius < blockFar;
  }
  for (const t of WORLD_SCENE.tiles) {
    const d = Math.hypot(t.x - cam.x, t.z - cam.z) - t.radius;
    const isNear = d < near + (t.isNear ? 20 : -20);
    if (isNear !== t.isNear) { t.isNear = isNear; t.near.visible = isNear; t.far.visible = !isNear; }
  }
  // A Turbulenta fica a 760 m da borda leste da região: fora de alcance ela sai da cena inteira,
  // e não só troca de nível de detalhe.
  const tCenter = WORLD_SCENE.turbTerrain;
  const turbNear = tCenter ? Math.hypot(TURB_RUNTIME_OFFSET.x - cam.x, TURB_RUNTIME_OFFSET.z - cam.z) < blockFar * 0.5 + 200 : false;
  for (const g of [WORLD_SCENE.turbTerrain, WORLD_SCENE.turbProps, WORLD_SCENE.turbRoutes]) if (g) g.visible = turbNear;
  if (!turbNear) return;
  for (const t of WORLD_SCENE.turbTiles) {
    const d = Math.hypot(t.x - cam.x, t.z - cam.z) - t.radius;
    const isNear = d < near + (t.isNear ? 20 : -20);
    if (isNear !== t.isNear) { t.isNear = isNear; t.near.visible = isNear; t.far.visible = !isNear; }
  }
}

// ---------------------------------------------------------------- depuração (?debug=1)
export function buildDebugOverlay(scene, { colliders = [], bridges = [] } = {}) {
  const group = new THREE.Group();
  group.name = 'world-debug';
  const lineMat = new THREE.LineBasicMaterial({ color: '#7ef0ff', depthTest: false, transparent: true, opacity: 0.8 });
  const routeMat = new THREE.LineBasicMaterial({ color: '#ffd166', depthTest: false, transparent: true, opacity: 0.9 });
  const anchorMat = new THREE.LineBasicMaterial({ color: '#ff7ad9', depthTest: false });

  for (const c of colliders) {
    const g = new THREE.BufferGeometry();
    const pts = [];
    if (c.r != null) {
      for (let i = 0; i <= 16; i++) {
        const a = i / 16 * Math.PI * 2;
        pts.push(c.x + Math.cos(a) * c.r, regionHeightAt(c.x, c.z) + 0.4, c.z + Math.sin(a) * c.r);
      }
    } else {
      const co = Math.cos(c.rot || 0), si = Math.sin(c.rot || 0);
      for (const [sx, sz] of [[-1, -1], [1, -1], [1, 1], [-1, 1], [-1, -1]]) {
        const lx = sx * c.hw, lz = sz * c.hd;
        pts.push(c.x + lx * co - lz * si, regionHeightAt(c.x, c.z) + 0.4, c.z + lx * si + lz * co);
      }
    }
    g.setAttribute('position', new THREE.Float32BufferAttribute(pts, 3));
    group.add(new THREE.Line(g, lineMat));
  }
  for (const b of bridges) {
    const g = new THREE.BufferGeometry();
    const pts = [];
    for (const [sx, sz] of [[-1, -1], [1, -1], [1, 1], [-1, 1], [-1, -1]]) pts.push(b.x + sx * b.hw, b.y, b.z + sz * b.hd);
    g.setAttribute('position', new THREE.Float32BufferAttribute(pts, 3));
    group.add(new THREE.Line(g, routeMat));
  }
  for (const [id, a] of Object.entries(REGION.anchors)) {
    const g = new THREE.BufferGeometry();
    const x = a[0], z = PLAN_Z_SIGN * a[1], y = regionHeightAt(x, z);
    g.setAttribute('position', new THREE.Float32BufferAttribute([x, y, z, x, y + 26, z], 3));
    const line = new THREE.Line(g, anchorMat);
    line.name = `anchor-${id}`;
    group.add(line);
  }
  group.renderOrder = 999;
  scene.add(group);
  WORLD_SCENE.debug = group;
  return group;
}

export { WATER_LEVEL };
