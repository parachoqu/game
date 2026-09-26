// Cobertura do chão gerada por célula em volta da câmera.
//
// Grama, flores, cogumelos, seixos e galhos só existem perto do jogador, então não precisam ser
// planejados para o quilômetro inteiro no boot. Cada célula de 32 m é gerada na hora com semente
// própria (a mesma célula dá sempre o mesmo chão), entra num InstancedMesh de um pool e é devolvida
// ao pool quando a câmera se afasta. Isso permite densidade de verdade onde o jogador está.
//
//   grama     manchas de tamanhos e densidades diferentes (campo distorcido em ~7, ~23 e ~61 m),
//             sempre em touceiras de 3–5 tufos, nunca um tufo solto; mais rala sob a mata fechada,
//             viçosa e alta na margem do rio, seca no planalto, faixa própria na beira da estrada
//   flores    colônias nos campos, na orla da mata e em volta das áreas funcionais
//   cogumelos colônias no chão da mata
//   seixos    margens, beira de estrada e chão de rocha
//   galhos    chão da mata e Ermos
//   campo     grama de campo (Poly Haven) no chão verde entre as manchas, onde antes só havia a
//             textura do terreno; touceiras altas com pendão em colônias
// Nada nasce na água, na pista, no tabuleiro e na rampa das pontes, na pegada das construções, no
// calçamento das praças nem nos pontos de interação e coleta.
import * as THREE from 'three';
import * as PR from '../engine/props.js';
import { U } from '../engine/materials.js';
import { regionHeightAt, biomeAt, isWaterCell, wetnessAt, waterLevelAt } from './heightfield.js';
import { lowGapExact, ringAt, routeEdgeAt } from './functional-areas.js';
import { forestDensity, grassPatch, inWastes, slopeHere } from './vegetation-fields.js';
import { greenGroundAt } from './scene-world.js';
import { rng, hash2, vnoise, fbm2, smoothstep, lerp } from './noise.js';

export const CELL = 32;
const NC = 1024 / CELL;
const KINDS = ['grass', 'flower', 'mushroom', 'pebble', 'branch', 'meadow', 'meadowTall'];
const CAP = { grass: 1500, flower: 260, mushroom: 110, pebble: 240, branch: 80, meadow: 1300, meadowTall: 220 };
// tipos com uma variante sorteada por célula (a vizinha usa outra)
const VARIANTS = { pebble: 5, meadow: 8, meadowTall: 3 };

export const COVER = { radius: 84, density: 1, scene: null, seeds: null, cells: new Map(), pool: [], generated: 0, ms: 0 };

// ---------------------------------------------------------------- sementes autorais
// Os 900 tufos do Blender viram reforço das manchas: onde a fonte pôs grama, a mancha engrossa.
let seedGrid = null;
function seedBoost(x, z) {
  if (!seedGrid) return 0;
  const i = Math.floor((x + 512) / 8), j = Math.floor((z + 512) / 8);
  let best = 0;
  for (let b = j - 1; b <= j + 1; b++) for (let a = i - 1; a <= i + 1; a++) {
    const l = seedGrid.get(b * 200 + a);
    if (!l) continue;
    for (const [sx, sz] of l) { const d = Math.hypot(sx - x, sz - z); if (d < 5) best = Math.max(best, 0.16 * (1 - d / 5)); }
  }
  return best;
}

// ---------------------------------------------------------------- geração de uma célula
// Devolve, por tipo, a lista de { x, y, z, s, sy, rot, c: [r,g,b] } em ordem aleatória (a contagem
// pode ser cortada do fim para rarear com a distância sem criar faixas).
export function generateCell(ci, cj) {
  const R = rng((hash2(ci, cj, 7771) * 4294967296) | 0);
  const x0 = -512 + ci * CELL, z0 = -512 + cj * CELL;
  const out = { grass: [], flower: [], mushroom: [], pebble: [], branch: [], meadow: [], meadowTall: [], variant: {} };
  for (const [k, n] of Object.entries(VARIANTS)) out.variant[k] = Math.floor(hash2(ci, cj, 91 + n) * n);
  // densidade de mata numa grade de 4 m (a mais cara das consultas), amostrada por bilinear
  const S = 4, M = CELL / S + 1;
  const Fg = new Float32Array(M * M);
  for (let j = 0; j < M; j++) for (let i = 0; i < M; i++) Fg[j * M + i] = forestDensity(Math.min(511, x0 + i * S), Math.min(511, z0 + j * S));
  const F = (x, z) => {
    const fx = (x - x0) / S, fz = (z - z0) / S;
    const i = Math.min(M - 2, Math.max(0, Math.floor(fx))), j = Math.min(M - 2, Math.max(0, Math.floor(fz)));
    const tx = fx - i, tz = fz - j;
    const a = Fg[j * M + i], b = Fg[j * M + i + 1], c = Fg[(j + 1) * M + i], d = Fg[(j + 1) * M + i + 1];
    return (a + (b - a) * tx) * (1 - tz) + (c + (d - c) * tx) * tz;
  };
  const inRegion = (x, z) => x > -511 && x < 511 && z > -511 && z < 511;
  const free = (x, z, road = 0.25, low = 0.3) => inRegion(x, z) && !isWaterCell(x, z) && routeEdgeAt(x, z) > road && lowGapExact(x, z) > low;
  const tone = (base, j) => base.map((v) => v * (1 + (R() - 0.5) * j));

  // limiar das manchas de grama: abaixo dele o campo de manchas não chega a formar touceira
  const patchT = (x, z, b, f, wet) => 0.37 + 0.05 * smoothstep(0.55, 0.95, f) - 0.07 * wet + (b === 4 ? 0.05 : 0)
    + (b === 2 ? 0.13 : 0) + (inWastes(x, z) ? 0.17 : 0) - 0.07 * ringAt(x, z) - (routeEdgeAt(x, z) < 2.5 ? 0.06 : 0);

  // grama: touceiras de 3–5 tufos
  const GS = 1.1;
  for (let gz = 0; gz < CELL / GS; gz++) for (let gx = 0; gx < CELL / GS; gx++) {
    const x = x0 + (gx + R()) * GS, z = z0 + (gz + R()) * GS;
    const roll = R();
    if (!free(x, z)) continue;
    const b = biomeAt(x, z), f = F(x, z), wet = wetnessAt(x, z);
    const dry = b === 4 || inWastes(x, z);
    const t = patchT(x, z, b, f, wet);
    const g = grassPatch(x, z) + seedBoost(x, z);
    // entre as manchas, uma franja esparsa de touceiras: o chão nunca fica liso
    if (g < t - 0.09) continue;
    const dens = g < t ? 0.14 : lerp(0.7, 1.2, smoothstep(t, t + 0.12, g)) * (1 - 0.25 * smoothstep(0.6, 1, f));
    if (roll > dens * GS * GS / 4) continue;
    if (slopeHere(x, z) > 1.15) continue;
    const lush = wet > 0.25 ? wet : 0;
    const base = dry ? [0.98, 0.9, 0.64] : lush ? [0.76, 0.94, 0.74] : [0.84 + 0.08 * vnoise(x / 30, z / 30, 97), 0.92, 0.8];
    const n = 3 + Math.floor(R() * 3);
    for (let k = 0; k < n; k++) {
      const a = R() * Math.PI * 2, r = 0.12 + R() * 0.42;
      const px = x + Math.cos(a) * r, pz = z + Math.sin(a) * r;
      if (!free(px, pz)) continue;
      const s = (0.6 + R() * 0.55) * (1 + 0.25 * lush);
      out.grass.push({
        x: px, y: regionHeightAt(px, pz) - 0.04, z: pz, s,
        sy: s * (lush > 0.5 ? 1.5 + R() * 0.4 : 0.85 + R() * 0.35), rot: R() * Math.PI * 2, c: tone(base, 0.16),
      });
    }
  }

  // grama de campo: preenche o chão verde entre as manchas (e rareia dentro delas, onde a grama do
  // kit já cobre). Nada no chão batido, no calçamento, na rocha, na margem nem sob a mata fechada.
  const MG = 0.9;
  for (let gz = 0; gz < CELL / MG; gz++) for (let gx = 0; gx < CELL / MG; gx++) {
    const x = x0 + (gx + R()) * MG, z = z0 + (gz + R()) * MG;
    const roll = R(), pick = R();
    if (!free(x, z, 0.8, 0.5)) continue;
    const b = biomeAt(x, z);
    if (b === 2 || b === 4 || inWastes(x, z)) continue;
    const f = F(x, z), wet = wetnessAt(x, z);
    const g = grassPatch(x, z) + seedBoost(x, z);
    const t = patchT(x, z, b, f, wet);
    const gap = 1 - smoothstep(t - 0.12, t + 0.01, g);
    const dens = 0.9 * gap * (1 - 0.85 * smoothstep(0.45, 0.85, f)) * (1 - smoothstep(0.3, 0.6, wet));
    if (roll > dens) continue;
    const green = greenGroundAt(x, z);
    if (green < 0.55 || roll > dens * smoothstep(0.55, 0.8, green) || slopeHere(x, z) > 1.1) continue;
    const tall = pick < 0.4 && vnoise(x / 9, z / 9, 89) > 0.6;
    const s = tall ? 0.8 + R() * 0.4 : 0.75 + R() * 0.55;
    const base = [0.9 + 0.1 * vnoise(x / 30, z / 30, 97), 0.95, 0.84];
    (tall ? out.meadowTall : out.meadow).push({
      x, y: regionHeightAt(x, z) - 0.03, z, s, sy: s * (0.85 + R() * 0.3), rot: R() * Math.PI * 2, c: tone(base, 0.14),
    });
  }

  // flores em colônias
  const FS = 1.2;
  const HUES = [[1, 1, 1], [1.2, 0.95, 0.85], [0.85, 0.9, 1.15], [1.1, 1.1, 0.8]];
  for (let gz = 0; gz < CELL / FS; gz++) for (let gx = 0; gx < CELL / FS; gx++) {
    const x = x0 + (gx + R()) * FS, z = z0 + (gz + R()) * FS;
    const roll = R();
    if (!free(x, z, 0.8, 0.6)) continue;
    const f = F(x, z), ring = ringAt(x, z), wet = wetnessAt(x, z);
    const col = vnoise(x / 5, z / 5, 81);
    const thr = 0.7 - 0.12 * ring - (wet > 0.2 && wet < 0.8 ? 0.06 : 0) + 0.2 * smoothstep(0.5, 0.9, f);
    if (col < thr || roll > 0.7 * smoothstep(thr, thr + 0.1, col)) continue;
    const s = 0.7 + R() * 0.5;
    out.flower.push({ x, y: regionHeightAt(x, z) - 0.03, z, s, sy: s * (0.85 + R() * 0.35), rot: R() * Math.PI * 2,
      c: tone(HUES[Math.floor(vnoise(x / 11, z / 11, 87) * 3.99)], 0.12) });
  }

  // cogumelos no chão da mata
  const MS = 1.4;
  for (let gz = 0; gz < CELL / MS; gz++) for (let gx = 0; gx < CELL / MS; gx++) {
    const x = x0 + (gx + R()) * MS, z = z0 + (gz + R()) * MS;
    const roll = R();
    if (!free(x, z, 1, 0.6)) continue;
    const f = F(x, z);
    if (f < 0.55 || vnoise(x / 4, z / 4, 83) < 0.84 || roll > 0.3) continue;
    const s = 0.6 + R() * 0.7;
    out.mushroom.push({ x, y: regionHeightAt(x, z) - 0.02, z, s, sy: s, rot: R() * Math.PI * 2, c: tone([0.72, 0.66, 0.62], 0.2) });
  }

  // seixos: margem, beira de estrada e chão de rocha
  const PS = 1.0;
  for (let gz = 0; gz < CELL / PS; gz++) for (let gx = 0; gx < CELL / PS; gx++) {
    const x = x0 + (gx + R()) * PS, z = z0 + (gz + R()) * PS;
    const roll = R();
    if (!inRegion(x, z) || isWaterCell(x, z) || lowGapExact(x, z) < 0.3) continue;
    const wet = wetnessAt(x, z), edge = routeEdgeAt(x, z), b = biomeAt(x, z);
    const beach = wet > 0.3 && regionHeightAt(x, z) < waterLevelAt(x) + 0.9;
    const verge = edge > 0.1 && edge < 1.4;
    const rocky = b === 2 && fbm2(x / 35, z / 35, 2, 63) > 0.56;
    if (!beach && !verge && !rocky) continue;
    if (vnoise(x / 3.5, z / 3.5, 85) < 0.52 || roll > (beach ? 0.5 : 0.28)) continue;
    const s = 0.6 + R() * 1.1;
    out.pebble.push({ x, y: regionHeightAt(x, z) - 0.04, z, s, sy: s * (0.8 + R() * 0.4), rot: R() * Math.PI * 2,
      c: tone(beach ? [0.8, 0.8, 0.78] : [1, 1, 1], 0.18) });
  }

  // galhos no chão da mata
  const BS = 3;
  for (let gz = 0; gz < CELL / BS; gz++) for (let gx = 0; gx < CELL / BS; gx++) {
    const x = x0 + (gx + R()) * BS, z = z0 + (gz + R()) * BS;
    const roll = R();
    if (!free(x, z, 0.6, 0.6)) continue;
    const f = F(x, z);
    if (roll > 0.22 * smoothstep(0.45, 0.85, f) + (inWastes(x, z) ? 0.08 : 0)) continue;
    const s = 0.7 + R() * 0.6;
    out.branch.push({ x, y: regionHeightAt(x, z) - 0.01, z, s, sy: s, rot: R() * Math.PI * 2, c: tone([0.95, 0.92, 0.88], 0.15) });
  }

  // ordem aleatória: cortar do fim rareia por igual
  for (const k of KINDS) {
    const l = out[k];
    for (let i = l.length - 1; i > 0; i--) { const j = Math.floor(R() * (i + 1)); [l[i], l[j]] = [l[j], l[i]]; }
    if (l.length > CAP[k]) l.length = CAP[k];
  }
  return out;
}

// ---------------------------------------------------------------- pool de células
const _e = new THREE.Euler(), _q = new THREE.Quaternion(), _p = new THREE.Vector3(), _s = new THREE.Vector3(), _m = new THREE.Matrix4();
function attr(n, size) {
  const a = new THREE.InstancedBufferAttribute(new Float32Array(n * size), size);
  a.setUsage(THREE.DynamicDrawUsage);
  return a;
}
function makeCell() {
  const group = new THREE.Group();
  group.name = 'ground-cover';
  const sphere = new THREE.Sphere(new THREE.Vector3(), CELL);
  const kinds = {};
  for (const k of KINDS) {
    const mat = attr(CAP[k], 16), col = attr(CAP[k], 3);
    const lv = { near: [], far: [] };
    // sem a grama de campo carregada, os tipos dela ficam sem malha (e nada é desenhado)
    const have = !k.startsWith('meadow') || PR.KIT.has?.(k);
    for (const [key, level] of have ? [['near', 1], ['far', 2]] : []) {
      lv[key] = PR.levelParts(k, 0, level).map(([geo, material]) => {
        const im = new THREE.InstancedMesh(geo, material, CAP[k]);
        im.instanceMatrix = mat; im.instanceColor = col;
        im.count = 0; im.visible = false; im.castShadow = false; im.receiveShadow = true;
        im.boundingSphere = sphere; im.matrixAutoUpdate = false;
        group.add(im);
        return im;
      });
    }
    kinds[k] = { mat, col, lv, n: 0, variant: 0 };
  }
  return { group, kinds, sphere, key: -1, near: null, shown: -1 };
}

function fill(cell, ci, cj) {
  const t0 = performance.now();
  const data = generateCell(ci, cj);
  let y0 = Infinity, y1 = -Infinity;
  for (const k of KINDS) {
    const slot = cell.kinds[k], list = data[k];
    const m = slot.mat.array, c = slot.col.array;
    list.forEach((it, i) => {
      _e.set(0, it.rot, 0); _q.setFromEuler(_e);
      _p.set(it.x, it.y, it.z); _s.set(it.s, it.sy, it.s);
      _m.compose(_p, _q, _s).toArray(m, i * 16);
      c[i * 3] = it.c[0]; c[i * 3 + 1] = it.c[1]; c[i * 3 + 2] = it.c[2];
      if (it.y < y0) y0 = it.y; if (it.y > y1) y1 = it.y;
    });
    slot.n = list.length;
    for (const a of [slot.mat, slot.col]) { a.clearUpdateRanges(); a.addUpdateRange(0, Math.max(1, list.length) * a.itemSize); a.needsUpdate = true; }
    if (VARIANTS[k] && data.variant[k] !== slot.variant) {
      slot.variant = data.variant[k];
      for (const [key, level] of [['near', 1], ['far', 2]]) {
        const parts = PR.levelParts(k, slot.variant, level);
        slot.lv[key].forEach((im, p) => { if (parts[p]) im.geometry = parts[p][0]; });
      }
    }
  }
  const cx = -512 + (ci + 0.5) * CELL, cz = -512 + (cj + 0.5) * CELL;
  if (y0 === Infinity) { y0 = y1 = regionHeightAt(cx, cz); }
  cell.sphere.center.set(cx, (y0 + y1) / 2 + 0.5, cz);
  cell.sphere.radius = Math.hypot(CELL * 0.72, (y1 - y0) / 2 + 1.5);
  cell.key = cj * NC + ci; cell.ci = ci; cell.cj = cj; cell.cx = cx; cell.cz = cz;
  cell.near = null; cell.shown = -1;
  COVER.generated++;
  COVER.ms += performance.now() - t0;
}

function show(cell, cam) {
  const d = Math.max(0, Math.hypot(cell.cx - cam.x, cell.cz - cam.z) - CELL * 0.71);
  const near = d < 26;
  const thin = (1 - 0.55 * smoothstep(30, COVER.radius, d)) * COVER.density;
  for (const k of KINDS) {
    const slot = cell.kinds[k];
    const count = Math.floor(slot.n * thin);
    for (const [key, list] of Object.entries(slot.lv)) {
      const on = (key === 'near') === near;
      for (const im of list) { im.count = on ? count : 0; im.visible = on && count > 0; }
    }
  }
}

// ---------------------------------------------------------------- interface
export function initGroundCover(scene, { seeds = [] } = {}) {
  COVER.scene = scene;
  seedGrid = new Map();
  for (const [x, z] of seeds) {
    const k = Math.floor((z + 512) / 8) * 200 + Math.floor((x + 512) / 8);
    if (!seedGrid.has(k)) seedGrid.set(k, []);
    seedGrid.get(k).push([x, z]);
  }
}

export function setGroundCoverQuality({ radius, density } = {}) {
  if (radius != null) COVER.radius = radius;
  if (density != null) COVER.density = density;
  U.uGrassFar.value = COVER.radius * 0.94;
  lastX = Infinity;
}

let lastX = Infinity, lastZ = Infinity;
// A cada quadro: gera até `budget` células novas (as mais próximas primeiro), devolve ao pool as que
// saíram do alcance e acerta nível e contagem quando a câmera anda.
export function updateGroundCover(cam, budget = 2) {
  if (!COVER.scene || COVER.density <= 0) {
    for (const cell of COVER.cells.values()) { cell.group.visible = false; }
    return;
  }
  const R = COVER.radius;
  const moved = Math.hypot(cam.x - lastX, cam.z - lastZ) >= 1;
  // células fora do alcance voltam ao pool
  for (const [key, cell] of COVER.cells) {
    if (Math.hypot(cell.cx - cam.x, cell.cz - cam.z) > R + CELL * 1.6) {
      COVER.scene.remove(cell.group);
      COVER.cells.delete(key);
      COVER.pool.push(cell);
    }
  }
  // células que faltam, das mais perto para as mais longe
  const want = [];
  const i0 = Math.max(0, Math.floor((cam.x - R + 512) / CELL)), i1 = Math.min(NC - 1, Math.floor((cam.x + R + 512) / CELL));
  const j0 = Math.max(0, Math.floor((cam.z - R + 512) / CELL)), j1 = Math.min(NC - 1, Math.floor((cam.z + R + 512) / CELL));
  for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) {
    const cx = -512 + (i + 0.5) * CELL, cz = -512 + (j + 0.5) * CELL;
    const d = Math.hypot(cx - cam.x, cz - cam.z);
    if (d - CELL * 0.71 > R || COVER.cells.has(j * NC + i)) continue;
    want.push([d, i, j]);
  }
  want.sort((a, b) => a[0] - b[0]);
  for (let n = 0; n < Math.min(budget, want.length); n++) {
    const [, i, j] = want[n];
    const cell = COVER.pool.pop() || makeCell();
    fill(cell, i, j);
    COVER.cells.set(cell.key, cell);
    COVER.scene.add(cell.group);
    cell.group.visible = true;
    show(cell, cam);
  }
  if (moved) {
    lastX = cam.x; lastZ = cam.z;
    for (const cell of COVER.cells.values()) { cell.group.visible = true; show(cell, cam); }
  }
}

// Malhas das células ativas (para a compilação antecipada dos sombreadores).
export function coverMeshes() {
  const out = [];
  for (const cell of COVER.cells.values()) cell.group.traverse((o) => { if (o.isInstancedMesh) out.push(o); });
  return out;
}
