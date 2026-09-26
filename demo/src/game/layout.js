// Geografia do recorte jogável. Norte = -Z. Nomes provisórios.
//
// Depois da migração para o mundo do Blender, este arquivo deixou de guardar coordenadas próprias:
// ele é o adaptador entre o manifesto preparado (`assets/world-runtime/world-manifest.json`) e as
// APIs que o resto do jogo já usava — `LOC`, `MAIN_ROAD`, `zoneAt()`, `placeAt()`, `gorgeX()`,
// `riverZ()`, `PORTAL_SPOTS`. Nenhum outro módulo deve repetir posições do mundo.
//
// A região comercial ocupa x, z ∈ [-512, 512] (1.024 × 1.024 m, 1 unidade = 1 metro). A Região
// Turbulenta continua tecnicamente separada, deslocada em X por `TURB_RUNTIME_OFFSET`.
import {
  REGION, TURBULENT, REGION_ANCHORS, TURB_ANCHORS, WORLD_LAYOUT_VERSION, placement,
} from '../world/runtime-manifest.js';
import {
  PLAN_Z_SIGN, TURB_RUNTIME_OFFSET, TURB_GATE_X, isTurbulentSpace,
} from '../world/coordinates.js';
import {
  REGION_ROUTE_LINES, TURB_ROUTE_LINES, routeXAtZ, distToPolyline as distToLine,
} from '../world/world-routes.js';
import { riverCenterAt } from '../world/heightfield.js';

export { WORLD_LAYOUT_VERSION, isTurbulentSpace, TURB_GATE_X };

// meia-extensão da região (x, z ∈ [-HALF, HALF])
export const HALF = (REGION.bounds_m[2] - REGION.bounds_m[0]) / 2;

// Região Turbulenta: centro deslocado e meia-extensão própria.
export const TURB = {
  x: TURB_RUNTIME_OFFSET.x,
  z: TURB_RUNTIME_OFFSET.z,
  half: (TURBULENT.bounds_m[2] - TURBULENT.bounds_m[0]) / 2,
  gate: TURB_GATE_X,
};

// âncora do manifesto → ponto da cena
const at = (id) => {
  const a = REGION_ANCHORS[id];
  return { x: a[0], z: PLAN_Z_SIGN * a[1], y: a[2] };
};
export const turbAt = (id) => {
  const a = TURB_ANCHORS[id];
  return { x: a[0] + TURB.x, z: PLAN_Z_SIGN * a[1] + TURB.z, y: a[2] };
};

// ---------------------------------------------------------------- rotas
// Os quatro caminhos da fonte assumem os papéis das rotas antigas. As polilinhas já vêm em
// coordenadas da cena; o formato ([[x, z], …]) é o mesmo que a demo sempre usou.
export const ROUTES = REGION_ROUTE_LINES;
export const TURB_ROUTES = TURB_ROUTE_LINES;

export const MAIN_ROAD = ROUTES.short_gorge.pts;            // entreposto sul → ponte → garganta → norte
export const EAST_ROAD = ROUTES.safe_east.pts;              // desvio seguro pelo leste
export const WEST_PATH = ROUTES.mine_spur.pts;              // ponte principal → mina → caverna oeste
export const RAVINE_PATH = ROUTES.observatory_trail.pts;    // garganta → observatório → Ermos

// Trecho da rota segura entre a ponte secundária e o acampamento.
export const CAMP_PATH = (() => {
  const pts = EAST_ROAD;
  const camp = at('camp');
  let best = 0, bestD = Infinity;
  pts.forEach((p, i) => { const d = Math.hypot(p[0] - camp.x, p[1] - camp.z); if (d < bestD) { bestD = d; best = i; } });
  const bridge = at('bridge_minor');
  let start = 0, startD = Infinity;
  pts.forEach((p, i) => { const d = Math.hypot(p[0] - bridge.x, p[1] - bridge.z); if (d < startD) { startD = d; start = i; } });
  const a = Math.min(start, best), b = Math.max(start, best);
  return pts.slice(a, b + 1);
})();

export function distToPolyline(x, z, pts) { return distToLine(x, z, pts); }

// X da rota da garganta na altura Z (mesmo contrato da função procedural antiga).
export const gorgeX = (z) => routeXAtZ('short_gorge', z);

// Z do eixo do rio na coluna X: o curso refeito dentro do vale (`world/river.js`). Nas pontes o
// eixo passa pelo vão do tabuleiro.
export const riverZ = (x) => riverCenterAt(x);

// ---------------------------------------------------------------- lugares
// Os papéis do recorte antigo passam a apontar para as âncoras da região comercial. Os raios
// acompanham a nova escala (o mundo é 2,5× mais largo que o recorte de 400 m).
const forest = REGION.derived?.forest;
const vigiaPoint = (() => {
  // ruína de vigia: meio do caminho entre a ponte principal e a garganta, sobre a rota curta
  const pts = MAIN_ROAD, bridge = at('bridge_main'), gorge = at('gorge_center');
  const mid = { x: (bridge.x + gorge.x) / 2, z: (bridge.z + gorge.z) / 2 };
  let best = pts[0], bestD = Infinity;
  for (const p of pts) { const d = Math.hypot(p[0] - mid.x, p[1] - mid.z); if (d < bestD) { bestD = d; best = p; } }
  return { x: best[0] + 14, z: best[1] };
})();

export const LOC = {
  vale: { ...at('outpost_south'), r: 52, name: 'Entreposto do Vale' },
  mercado: { ...at('market'), r: 22, name: 'Mercado do Vale' },
  alto: { ...at('outpost_north'), r: 46, name: 'Entreposto Alto' },
  canteiro: { ...at('bridge_main'), r: 20, name: 'Canteiro da Passagem' },
  passagem: { ...at('gorge_center'), r: 46, name: 'Passagem Estreita' },
  vigia: { ...vigiaPoint, r: 8, name: 'Ruína de vigia' },
  acampamento: { ...at('camp'), r: 20, name: 'Acampamento das Garras' },
  observatorio: { ...at('observatory'), r: 16, name: 'Observatório Antigo' },
  mina: { ...at('mine'), r: 16, name: 'Boca da Mina' },
  caverna: { ...at('cave_west'), r: 13, name: 'Caverna do Oeste' },
  portal: { ...at('portal_turbulent'), r: 16, name: 'Portal Instável' },
  ponte: { ...at('bridge_main'), r: 14, name: 'Ponte Principal' },
  ponteMenor: { ...at('bridge_minor'), r: 12, name: 'Ponte Secundária' },
  ermos: { ...at('wastes'), rx: 150, rz: 135, r: 140, name: 'Ermos Quebrados' },
  bosque: forest
    ? { x: forest.plan[0], z: PLAN_Z_SIGN * forest.plan[1], r: Math.max(60, forest.radius_m), name: 'Bosque das Forjas' }
    : { ...at('gorge_center'), r: 80, name: 'Bosque das Forjas' },
};

// ---------------------------------------------------------------- zonas
// A matriz de zonas (cap. 07) passa a se apoiar no risco declarado de cada rota e nas âncoras.
// Só os dois entrepostos e o mercado são área protegida; a rota segura leva a proteção consigo
// numa faixa estreita. O acampamento das Garras e o portal instável ficam sempre em Fronteira,
// mesmo estando sobre a rota segura.
const PROTECTED_POINTS = ['outpost_south', 'market', 'outpost_north'].map((id) => ({ ...at(id), r: 96 }));
const NEVER_PROTECTED = ['camp', 'portal_turbulent'].map((id) => ({ ...at(id), r: 46 }));

export function zoneAt(x, z) {
  if (isTurbulentSpace(x)) return 'turbulenta';
  const e = LOC.ermos;
  const ex = (x - e.x) / e.rx, ez = (z - e.z) / e.rz;
  if (ex * ex + ez * ez < 1) return 'fullloot';
  for (const p of NEVER_PROTECTED) if (Math.hypot(x - p.x, z - p.z) < p.r) return 'fronteira';
  for (const p of PROTECTED_POINTS) if (Math.hypot(x - p.x, z - p.z) < p.r) return 'protegida';
  if (distToPolyline(x, z, EAST_ROAD) < 12) return 'protegida';
  return 'fronteira';
}

export function placeAt(x, z) {
  if (isTurbulentSpace(x)) return 'Região Turbulenta';
  const d = (l) => Math.hypot(x - l.x, z - l.z);
  if (d(LOC.mercado) < LOC.mercado.r) return LOC.mercado.name;
  if (d(LOC.vale) < LOC.vale.r) return LOC.vale.name;
  if (d(LOC.alto) < LOC.alto.r) return LOC.alto.name;
  if (d(LOC.acampamento) < LOC.acampamento.r + 10) return LOC.acampamento.name;
  if (d(LOC.observatorio) < LOC.observatorio.r + 14) return LOC.observatorio.name;
  if (d(LOC.mina) < LOC.mina.r + 10) return LOC.mina.name;
  if (d(LOC.caverna) < LOC.caverna.r + 10) return LOC.caverna.name;
  if (d(LOC.portal) < LOC.portal.r + 10) return LOC.portal.name;
  const e = LOC.ermos;
  const ex = (x - e.x) / e.rx, ez = (z - e.z) / e.rz;
  if (ex * ex + ez * ez < 1) return e.name;
  if (d(LOC.canteiro) < LOC.canteiro.r + 8) return LOC.canteiro.name;
  if (d(LOC.ponteMenor) < LOC.ponteMenor.r + 8) return LOC.ponteMenor.name;
  if (d(LOC.passagem) < LOC.passagem.r) return LOC.passagem.name;
  if (d(LOC.bosque) < LOC.bosque.r) return LOC.bosque.name;
  if (distToPolyline(x, z, EAST_ROAD) < 24) return 'Estrada Longa';
  if (Math.abs(z - riverZ(x)) < 26) return 'Rio Largo';
  if (distToPolyline(x, z, MAIN_ROAD) < 24) return LOC.passagem.name;
  if (z < -200) return 'Campos do Norte';
  if (z > 240) return 'Campos do Vale';
  if (x < -180) return 'Colinas do Oeste';
  return 'Encostas da Serra';
}

// ---------------------------------------------------------------- portais
// Pontos onde portais turbulentos podem surgir: sempre em Fronteira ou Full Loot, distribuídos
// pelas rotas de risco alto e pelos marcos afastados. Derivados, nunca digitados à mão.
export const PORTAL_SPOTS = (() => {
  const spots = [];
  const push = (x, z) => {
    if (zoneAt(x, z) === 'protegida' || zoneAt(x, z) === 'turbulenta') return;
    if (spots.some(([a, b]) => Math.hypot(a - x, b - z) < 70)) return;
    spots.push([x, z]);
  };
  for (const id of ['short_gorge', 'observatory_trail', 'mine_spur']) {
    const pts = ROUTES[id].pts;
    for (let i = 3; i < pts.length - 3; i += 5) push(pts[i][0] + 22, pts[i][1] - 16);
  }
  for (const id of ['mine', 'cave_west', 'gorge_center', 'wastes', 'observatory']) {
    const a = at(id);
    push(a.x + 34, a.z + 28);
  }
  return spots.length ? spots : [[at('gorge_center').x + 30, at('gorge_center').z]];
})();

// ---------------------------------------------------------------- pontos de jogo
// Posições que a camada de jogo monta (`game/world.js`, `event.js`, `camp.js`) e que a vegetação
// precisa deixar livres. Ficam aqui para os dois lados lerem o mesmo número.
const onLine = (id, t, dx = 0, dz = 0) => {
  const pts = ROUTES[id].pts;
  const p = pts[Math.max(0, Math.min(pts.length - 1, Math.round(t * (pts.length - 1))))];
  return { x: p[0] + dx, z: p[1] + dz };
};

// Canteiro: ao lado da cabeceira sul da ponte principal.
const bridgeMain = placement('Bridge_Main');
export const CANTEIRO = bridgeMain
  ? { x: bridgeMain.plan[0] + 13, z: PLAN_Z_SIGN * bridgeMain.plan[1] + bridgeMain.half[1] + 6 }
  : { x: LOC.canteiro.x, z: LOC.canteiro.z };
LOC.canteiro.x = CANTEIRO.x; LOC.canteiro.z = CANTEIRO.z;

// Estandartes, carroça tombada e paliçada ao longo da subida da garganta.
export const PASSAGE_PROPS = (() => {
  const banners = [];
  for (const t of [0.34, 0.62]) {
    const p = onLine('short_gorge', t);
    banners.push({ x: p.x + 9, z: p.z }, { x: p.x - 9, z: p.z });
  }
  return { banners, cart: onLine('short_gorge', 0.46, 7, 3), palisade: onLine('short_gorge', 0.72, -8, 0) };
})();

// Grupos de encontro: centro e coleira.
export const ENCOUNTERS = [
  { id: 'passagem-sul', ...onLine('short_gorge', 0.38, 16, 0), leash: 44 },
  { id: 'passagem-norte', ...onLine('short_gorge', 0.66, -14, 0), leash: 44 },
  { id: 'mina', ...onLine('mine_spur', 0.55, 0, 18), leash: 40 },
  { id: 'ermos-a', x: LOC.ermos.x + 22, z: LOC.ermos.z - 30, leash: 60 },
  { id: 'ermos-b', x: LOC.ermos.x - 48, z: LOC.ermos.z + 40, leash: 50 },
  { id: 'bosque-norte', x: LOC.bosque.x, z: LOC.bosque.z - LOC.bosque.r * 0.5, leash: 26 },
  { id: 'observatorio', ...onLine('observatory_trail', 0.45, 20, 12), leash: 44 },
];

// Postos de guarda depois que a Passagem é reaberta: o canteiro e três pontos da rota curta.
export const GUARD_POSTS = [
  { x: CANTEIRO.x - 6, z: CANTEIRO.z - 9, yaw: Math.PI },
  { ...onLine('short_gorge', 0.42, 4, 0), yaw: Math.PI },
  { ...onLine('short_gorge', 0.52, -5, 0), yaw: Math.PI },
  { ...onLine('short_gorge', 0.66, 3, 0), yaw: Math.PI },
];

// Acampamento: posição deslocada (encosta a oeste) e a patrulha pela garganta.
export const CAMP_DISPLACED = { x: LOC.acampamento.x - 90, z: LOC.acampamento.z + 60 };
export const CAMP_PATROL = [0.44, 0.52, 0.60, 0.52].map((t) => {
  const p = onLine('short_gorge', t);
  return [p.x, p.z];
});
