// Rotas do mundo: polilinhas do manifesto, campo de distância em grade e as faixas visíveis.
//
// As estradas do Blender vêm como faixas planas de 4 m de resolução; aqui elas são reconstruídas
// sobre o heightfield de 1 m, então acompanham o relevo sem flutuar nem afundar. O campo de
// distância é calculado uma vez e serve ao terreno (chão batido), à vegetação (nada na estrada)
// e aos colisores (nenhum obstáculo dentro da largura útil).
import * as THREE from 'three';
import { REGION_ROUTES, TURB_ROUTES } from './runtime-manifest.js';
import { routeToThree, TURB_RUNTIME_OFFSET, PLAN_Z_SIGN } from './coordinates.js';
import { REGION_FIELD, regionHeightAt, turbulentHeightAt } from './heightfield.js';
import { surfaceMat } from '../engine/materials.js';

// ---------------------------------------------------------------- polilinhas
// Subdivide a polilinha a cada `seg` metros para a faixa acompanhar o terreno entre os pontos.
function densify(points, seg) {
  const out = [];
  for (let i = 0; i < points.length - 1; i++) {
    const [ax, az] = points[i], [bx, bz] = points[i + 1];
    const len = Math.hypot(bx - ax, bz - az);
    const steps = Math.max(1, Math.ceil(len / seg));
    for (let s = 0; s < steps; s++) {
      const t = s / steps;
      out.push([ax + (bx - ax) * t, az + (bz - az) * t]);
    }
  }
  out.push(points[points.length - 1]);
  return out;
}

export const REGION_ROUTE_LINES = Object.fromEntries(
  Object.entries(REGION_ROUTES).map(([id, r]) => [id, { id, width: r.width_m, risk: r.risk, anchors: r.anchors, pts: routeToThree(r.points) }]),
);
export const TURB_ROUTE_LINES = Object.fromEntries(
  Object.entries(TURB_ROUTES).map(([id, r]) => [id, { id, width: r.width_m, risk: r.risk, anchors: r.anchors, pts: routeToThree(r.points, 'turbulent') }]),
);

export function distToPolyline(x, z, pts) {
  let best = Infinity;
  for (let i = 0; i < pts.length - 1; i++) {
    const ax = pts[i][0], az = pts[i][1];
    const dx = pts[i + 1][0] - ax, dz = pts[i + 1][1] - az;
    const l2 = dx * dx + dz * dz;
    let t = l2 ? ((x - ax) * dx + (z - az) * dz) / l2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    const px = ax + dx * t - x, pz = az + dz * t - z;
    const d = px * px + pz * pz;
    if (d < best) best = d;
  }
  return Math.sqrt(best);
}

// ---------------------------------------------------------------- campo de distância
// Grade de 2 m sobre a região, distância em decímetros saturada em 25,5 m. Uma varredura pelos
// segmentos custa alguns milissegundos e evita percorrer as polilinhas a cada consulta.
const RF_STEP = 2;
const RF_N = 513;
const RF_MAX = 25.5;
const routeField = new Uint8Array(RF_N * RF_N).fill(255);

function rasterize(pts, pad) {
  for (let i = 0; i < pts.length - 1; i++) {
    const ax = pts[i][0], az = pts[i][1], bx = pts[i + 1][0], bz = pts[i + 1][1];
    const x0 = Math.min(ax, bx) - pad, x1 = Math.max(ax, bx) + pad;
    const z0 = Math.min(az, bz) - pad, z1 = Math.max(az, bz) + pad;
    const i0 = Math.max(0, Math.floor((x0 + 512) / RF_STEP)), i1 = Math.min(RF_N - 1, Math.ceil((x1 + 512) / RF_STEP));
    const j0 = Math.max(0, Math.floor((z0 + 512) / RF_STEP)), j1 = Math.min(RF_N - 1, Math.ceil((z1 + 512) / RF_STEP));
    const dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
    for (let j = j0; j <= j1; j++) {
      const pz = -512 + j * RF_STEP;
      for (let i = i0; i <= i1; i++) {
        const px = -512 + i * RF_STEP;
        let t = l2 ? ((px - ax) * dx + (pz - az) * dz) / l2 : 0;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        const qx = ax + dx * t - px, qz = az + dz * t - pz;
        const d = Math.sqrt(qx * qx + qz * qz);
        if (d >= RF_MAX) continue;
        const v = Math.round(d * 10);
        const k = j * RF_N + i;
        if (v < routeField[k]) routeField[k] = v;
      }
    }
  }
}

let fieldReady = false;
export function initRouteField() {
  if (fieldReady) return;
  for (const line of Object.values(REGION_ROUTE_LINES)) rasterize(line.pts, RF_MAX);
  fieldReady = true;
}

// Distância até a rota mais próxima, em metros (saturada em 25,5).
export function routeDistance(x, z) {
  const fi = (x + 512) / RF_STEP, fj = (z + 512) / RF_STEP;
  const i = fi < 0 ? 0 : fi > RF_N - 1 ? RF_N - 1 : Math.round(fi);
  const j = fj < 0 ? 0 : fj > RF_N - 1 ? RF_N - 1 : Math.round(fj);
  return routeField[j * RF_N + i] / 10;
}

// Meia-largura útil da rota mais próxima naquele ponto (usada pelas exclusões).
export function onRoute(x, z, margin = 0) {
  return routeDistance(x, z) <= margin;
}

// Ponto mais próximo sobre alguma rota, com a direção local. Usado para abrir corredores nos
// marcos que a fonte colocou em cima da estrada (a mina, o acampamento, o observatório).
export function nearestRouteInfo(x, z, scene = 'region') {
  const lines = scene === 'turbulent' ? TURB_ROUTE_LINES : REGION_ROUTE_LINES;
  let best = null, bestD = Infinity;
  for (const line of Object.values(lines)) {
    const pts = line.pts;
    for (let i = 0; i < pts.length - 1; i++) {
      const ax = pts[i][0], az = pts[i][1];
      const dx = pts[i + 1][0] - ax, dz = pts[i + 1][1] - az;
      const l2 = dx * dx + dz * dz;
      let t = l2 ? ((x - ax) * dx + (z - az) * dz) / l2 : 0;
      t = t < 0 ? 0 : t > 1 ? 1 : t;
      const px = ax + dx * t, pz = az + dz * t;
      const d = Math.hypot(px - x, pz - z);
      if (d < bestD) {
        const len = Math.sqrt(l2) || 1;
        bestD = d;
        best = { dist: d, px, pz, tx: dx / len, tz: dz / len, width: line.width, id: line.id };
      }
    }
  }
  return best;
}

// ---------------------------------------------------------------- faixas visíveis
// A faixa é uma tira de triângulos com a largura declarada da rota, 8 cm acima do chão.
function stripGeometry(pts, width, heightAt, lift) {
  const dense = densify(pts, 4);
  const half = width / 2;
  const n = dense.length;
  const pos = new Float32Array(n * 2 * 3);
  const uv = new Float32Array(n * 2 * 2);
  let run = 0;
  for (let i = 0; i < n; i++) {
    const [x, z] = dense[i];
    const p = dense[Math.max(0, i - 1)], q = dense[Math.min(n - 1, i + 1)];
    let tx = q[0] - p[0], tz = q[1] - p[1];
    const l = Math.hypot(tx, tz) || 1;
    tx /= l; tz /= l;
    const nx = -tz, nz = tx;
    if (i > 0) run += Math.hypot(x - dense[i - 1][0], z - dense[i - 1][1]);
    for (const s of [-1, 1]) {
      const k = (i * 2 + (s < 0 ? 0 : 1));
      const px = x + nx * half * s, pz = z + nz * half * s;
      pos[k * 3] = px;
      pos[k * 3 + 1] = heightAt(px, pz) + lift;
      pos[k * 3 + 2] = pz;
      uv[k * 2] = s < 0 ? 0 : 1;
      uv[k * 2 + 1] = run / Math.max(1, width);
    }
  }
  const idx = new Uint32Array((n - 1) * 6);
  let p = 0;
  for (let i = 0; i < n - 1; i++) {
    const a = i * 2, b = a + 1, c = a + 2, d = a + 3;
    idx[p++] = a; idx[p++] = c; idx[p++] = d;
    idx[p++] = a; idx[p++] = d; idx[p++] = b;
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
  g.setIndex(new THREE.BufferAttribute(idx, 1));
  g.computeVertexNormals();
  g.computeBoundingSphere();
  return g;
}

export function buildRouteMeshes(scene = 'region') {
  const lines = scene === 'turbulent' ? TURB_ROUTE_LINES : REGION_ROUTE_LINES;
  const heightAt = scene === 'turbulent' ? turbulentHeightAt : regionHeightAt;
  const group = new THREE.Group();
  group.name = scene === 'turbulent' ? 'turb-routes' : 'region-routes';
  const material = surfaceMat('trilha', scene === 'turbulent' ? '#9d92a8' : '#b9a184');
  for (const line of Object.values(lines)) {
    const m = new THREE.Mesh(stripGeometry(line.pts, line.width, heightAt, 0.08), material);
    m.name = `route-${line.id}`;
    m.receiveShadow = true;
    m.castShadow = false;
    m.matrixAutoUpdate = false;
    group.add(m);
  }
  return group;
}

// Ponto de uma rota por fração do percurso (0..1), útil para posicionar marcos e patrulhas.
export function routePoint(id, t, scene = 'region') {
  const line = (scene === 'turbulent' ? TURB_ROUTE_LINES : REGION_ROUTE_LINES)[id];
  if (!line) return null;
  const pts = line.pts;
  const at = Math.max(0, Math.min(pts.length - 1, Math.round(t * (pts.length - 1))));
  return { x: pts[at][0], z: pts[at][1] };
}

// X da rota da garganta na altura Z pedida (mantém o contrato de `gorgeX` do layout antigo).
export function routeXAtZ(id, z, scene = 'region') {
  const line = (scene === 'turbulent' ? TURB_ROUTE_LINES : REGION_ROUTE_LINES)[id];
  if (!line) return 0;
  const pts = line.pts;
  let best = pts[0], bestD = Infinity, second = pts[1];
  for (let i = 0; i < pts.length - 1; i++) {
    const a = pts[i], b = pts[i + 1];
    const lo = Math.min(a[1], b[1]), hi = Math.max(a[1], b[1]);
    if (z >= lo && z <= hi) {
      const t = hi - lo < 1e-6 ? 0 : (z - a[1]) / (b[1] - a[1]);
      return a[0] + (b[0] - a[0]) * t;
    }
    const d = Math.min(Math.abs(z - a[1]), Math.abs(z - b[1]));
    if (d < bestD) { bestD = d; best = a; second = b; }
  }
  return Math.abs(z - best[1]) < Math.abs(z - second[1]) ? best[0] : second[0];
}

export { densify, PLAN_Z_SIGN, TURB_RUNTIME_OFFSET, REGION_FIELD };
