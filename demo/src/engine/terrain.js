// Relevo do mundo: agora vem do pacote preparado em `assets/world-runtime` (região comercial de
// 1.024 × 1.024 m e Região Turbulenta de 256 × 256 m), e não mais de ruído procedural.
//
// Este arquivo continua sendo a porta de entrada do resto do jogo: `groundHeight`, `terrainHeight`,
// `WATER_Y`, `bridges` e `buildTerrain` mantêm a mesma assinatura de antes. O que mudou é a fonte:
// um heightfield determinístico amostrado por interpolação bilinear, sem alocação por consulta e
// sem nenhum raycast contra a geometria do cenário.
import { isTurbulentSpace } from '../world/coordinates.js';
import {
  regionHeightAt, turbulentHeightAt, waterLevelAt, isWaterCell, biomeAt, slopeAt,
  WATER_LEVEL, REGION_LIMITS, TURB_LIMITS,
} from '../world/heightfield.js';
import { assertRuntimeSchema } from '../world/runtime-manifest.js';
import { initRouteField } from '../world/world-routes.js';
import { buildWorldColliders } from '../world/world-colliders.js';
import {
  buildRegionTerrain, buildRegionWater, buildRegionRoutes,
  buildTurbulentTerrain, buildTurbulentRoutes, WORLD_SCENE,
} from '../world/scene-world.js';

// Nível de referência do rio (mediana do perfil). Para a lâmina exata em um ponto use `waterAt`.
export const WATER_Y = WATER_LEVEL;

// Pisos elevados atravessáveis (pontes): {x, z, hw, hd, y, rot}
export const bridges = [];

export { REGION_LIMITS, TURB_LIMITS, biomeAt, isWaterCell, slopeAt };

let ready = false;

// Prepara os dados do relevo e os colisores estáticos do cenário. Idempotente.
export function initTerrainData() {
  if (ready) return;
  assertRuntimeSchema();
  initRouteField();
  const built = buildWorldColliders();
  bridges.length = 0;
  for (const b of built.bridges) bridges.push(b);
  ready = true;
}

// ---------------------------------------------------------------- consultas
export function terrainHeight(x, z) {
  return isTurbulentSpace(x) ? turbulentHeightAt(x, z) : regionHeightAt(x, z);
}

// Pisos elevados com rampa: no tabuleiro o chão é o piso da ponte; nos metros seguintes a altura
// volta ao terreno de forma contínua, para a travessia não virar um degrau intransponível.
export function groundHeight(x, z) {
  let h = terrainHeight(x, z);
  for (let i = 0; i < bridges.length; i++) {
    const b = bridges[i];
    const dx = x - b.x, dz = z - b.z;
    let lx = dx, lz = dz;
    if (b.rot) { const c = Math.cos(-b.rot), s = Math.sin(-b.rot); lx = dx * c - dz * s; lz = dx * s + dz * c; }
    const ax = lx < 0 ? -lx : lx, az = lz < 0 ? -lz : lz;
    if (ax > b.hw + b.apronSide || az > b.hd + b.apron) continue;
    const wx = ax <= b.hw ? 1 : 1 - (ax - b.hw) / b.apronSide;
    const wz = az <= b.hd ? 1 : 1 - (az - b.hd) / b.apron;
    const w = wx < wz ? wx : wz;
    if (w <= 0) continue;
    const t = w * w * (3 - 2 * w);
    const y = h + (b.y - h) * t;
    if (y > h) h = y;
  }
  return h;
}

// Altura da lâmina de água em um ponto; -Infinity onde não há água.
export function waterAt(x, z) {
  if (isTurbulentSpace(x)) return -Infinity;
  return isWaterCell(x, z) ? waterLevelAt(x) : -Infinity;
}
// Profundidade submersa (0 em terra firme).
export function waterDepth(x, z) {
  const w = waterAt(x, z);
  return w === -Infinity ? 0 : Math.max(0, w - terrainHeight(x, z));
}

// ---------------------------------------------------------------- construção visual
// O cenário completo (props, instâncias e Turbulenta) é montado por `world/index.js`; aqui ficam
// as camadas que dependem diretamente do relevo.
export function buildTerrain(scene, onProgress) {
  const region = buildRegionTerrain(scene, onProgress);
  const water = buildRegionWater(scene);
  const routes = buildRegionRoutes(scene);
  const turb = buildTurbulentTerrain(scene);
  const turbRoutes = buildTurbulentRoutes(scene);
  return { main: region.group, water, routes, turb: turb.group, turbRoutes, scene: WORLD_SCENE };
}

// ---------------------------------------------------------------- compatibilidade
// `fbm` continua exportado: efeitos e decorações da demo o usam como ruído barato. Ele não
// participa mais da altura do chão.
function hash(x, z) {
  let h = Math.imul(x | 0, 374761393) ^ Math.imul(z | 0, 668265263);
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967295;
}
function vnoise(x, z) {
  const xi = Math.floor(x), zi = Math.floor(z);
  const xf = x - xi, zf = z - zi;
  const u = xf * xf * (3 - 2 * xf), v = zf * zf * (3 - 2 * zf);
  const a = hash(xi, zi), b = hash(xi + 1, zi), c = hash(xi, zi + 1), d = hash(xi + 1, zi + 1);
  return (a + (b - a) * u) * (1 - v) + (c + (d - c) * u) * v;
}
export function fbm(x, z, oct = 4) {
  let s = 0, amp = 1, f = 1, norm = 0;
  for (let i = 0; i < oct; i++) { s += vnoise(x * f, z * f) * amp; norm += amp; amp *= 0.5; f *= 2.03; }
  return s / norm * 2 - 1;
}
