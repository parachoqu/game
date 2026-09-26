// Colisores estáticos (círculos e caixas) em grade espacial + movimento com limite de inclinação.
import { groundHeight, waterAt } from '../engine/terrain.js';
import { HALF, TURB, isTurbulentSpace } from './layout.js';
import { routeDistance } from '../world/world-routes.js';

const CELL = 12;
const grid = new Map();
const key = (i, j) => i * 100003 + j;

export function addCircle(x, z, r) { insert({ t: 0, x, z, r }); }
// rotY no mesmo sentido de Object3D.rotation.y
export function addBox(x, z, hw, hd, rotY = 0) { const rot = -rotY; insert({ t: 1, x, z, hw, hd, c: Math.cos(rot), s: Math.sin(rot), r: Math.hypot(hw, hd) }); }
function insert(o) {
  const i0 = Math.floor((o.x - o.r) / CELL), i1 = Math.floor((o.x + o.r) / CELL);
  const j0 = Math.floor((o.z - o.r) / CELL), j1 = Math.floor((o.z + o.r) / CELL);
  for (let i = i0; i <= i1; i++) for (let j = j0; j <= j1; j++) {
    const k = key(i, j); if (!grid.has(k)) grid.set(k, []); grid.get(k).push(o);
  }
}

// empurra um círculo (x,z,r) para fora dos colisores; retorna [x,z]
export function resolve(x, z, r) {
  const list = grid.get(key(Math.floor(x / CELL), Math.floor(z / CELL)));
  if (!list) return [x, z];
  for (const o of list) {
    if (o.t === 0) {
      const dx = x - o.x, dz = z - o.z, d = Math.hypot(dx, dz), m = o.r + r;
      if (d < m && d > 1e-4) { x = o.x + dx / d * m; z = o.z + dz / d * m; }
    } else {
      // para o espaço local da caixa
      const dx = x - o.x, dz = z - o.z;
      const lx = dx * o.c + dz * o.s, lz = -dx * o.s + dz * o.c;
      const cx = Math.max(-o.hw, Math.min(o.hw, lx)), cz = Math.max(-o.hd, Math.min(o.hd, lz));
      let px = lx - cx, pz = lz - cz, d = Math.hypot(px, pz);
      if (d < r) {
        let nlx, nlz;
        if (d > 1e-4) { nlx = cx + px / d * r; nlz = cz + pz / d * r; }
        else { // centro dentro da caixa: sai pelo lado mais próximo
          const ox = o.hw - Math.abs(lx), oz = o.hd - Math.abs(lz);
          if (ox < oz) { nlx = Math.sign(lx || 1) * (o.hw + r); nlz = lz; } else { nlx = lx; nlz = Math.sign(lz || 1) * (o.hd + r); }
        }
        x = o.x + nlx * o.c - nlz * o.s; z = o.z + nlx * o.s + nlz * o.c;
      }
    }
  }
  return [x, z];
}

// testa se um ponto (x,z) colide com algum obstáculo estático (com margem de raio r)
export function testPoint(x, z, r = 0.4) {
  const list = grid.get(key(Math.floor(x / CELL), Math.floor(z / CELL)));
  if (!list) return false;
  for (const o of list) {
    if (o.t === 0) {
      if (Math.hypot(x - o.x, z - o.z) < o.r + r) return true;
    } else {
      const dx = x - o.x, dz = z - o.z;
      const lx = dx * o.c + dz * o.s, lz = -dx * o.s + dz * o.c;
      if (Math.abs(lx) < o.hw + r && Math.abs(lz) < o.hd + r) return true;
    }
  }
  return false;
}

const MAX_SLOPE = 1.25;
// Sobre uma estrada o limite é mais alto: a rampa foi construída para ser subida, e a encosta
// leste do rio chega a 49° no traçado autoral da rota segura.
const ROUTE_SLOPE = 1.9;
const ROUTE_LANE = 3.2;
// move uma entidade {pos:{x,y,z}} respeitando relevo íngreme, colisores e limites do mapa.
// `lift`: altura do corpo acima do chão (no pulo) — o que ele alcança não conta como encosta.
export function moveEntity(ent, dx, dz, r = 0.45, lift = 0) {
  const p = ent.pos;
  const h0 = groundHeight(p.x, p.z);
  const tryMove = (mx, mz) => {
    const nx = p.x + mx, nz = p.z + mz;
    const step = Math.hypot(mx, mz);
    if (step < 1e-5) return false;
    const h1 = groundHeight(nx, nz);
    // sair da água para a margem é sempre permitido; fora dela vale o limite de inclinação
    const inWater = waterAt(p.x, p.z) > h0 + 0.2;
    const limit = (!isTurbulentSpace(nx) && routeDistance(nx, nz) < ROUTE_LANE) ? ROUTE_SLOPE : MAX_SLOPE;
    if (h1 - h0 - lift > limit * Math.max(step, 0.05) && !inWater) return false;
    p.x = nx; p.z = nz; return true;
  };
  if (!tryMove(dx, dz)) { if (!tryMove(dx, 0)) tryMove(0, dz); }
  [p.x, p.z] = resolve(p.x, p.z, r);
  if (isTurbulentSpace(p.x)) {
    const lim = TURB.half - 6;
    p.x = Math.max(TURB.x - lim, Math.min(TURB.x + lim, p.x));
    p.z = Math.max(TURB.z - lim, Math.min(TURB.z + lim, p.z));
  } else {
    const lim = HALF - 8;
    p.x = Math.max(-lim, Math.min(lim, p.x));
    p.z = Math.max(-lim, Math.min(lim, p.z));
  }
}
