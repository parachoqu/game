// Camada de jogo sobre o mundo autoral: serviços, pessoas, coleta, marcos de zona e encontros.
//
// O relevo, as construções, as estradas e a vegetação vêm prontos do pacote de runtime
// (`src/world/`). Aqui ficam só os objetos que o jogo precisa e que o cenário não traz: bancadas,
// quadros, alvos de treino, bandeiras de zona, pontos de coleta, NPCs e grupos de inimigos.
// Nenhuma posição é digitada: tudo se apoia nas âncoras e nos volumes já medidos do mundo.
import * as THREE from 'three';
import { G } from '../state.js';
import { ZONES } from '../config.js';
import { groundHeight, fbm } from '../engine/terrain.js';
import * as PR from '../engine/props.js';
import { addBox, addCircle } from './collide.js';
import { LOC, zoneAt, HALF, CANTEIRO, PASSAGE_PROPS, ENCOUNTERS } from './layout.js';
import { placement } from '../world/runtime-manifest.js';
import { PLAN_Z_SIGN } from '../world/coordinates.js';
import { isClearGround } from '../world/world-colliders.js';
import { extraModel } from '../world/extra-props.js';
import { FUNCTIONAL } from '../world/functional-areas.js';
import { addNpc, initTravelers } from './npcs.js';
import { defineGroup } from './enemies.js';
import { CAMP } from './camp.js';
import { EVT } from './event.js';

export const NODES = [];

function rng(seed) { return () => { seed |= 0; seed = seed + 0x6D2B79F5 | 0; let t = Math.imul(seed ^ seed >>> 15, 1 | seed); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }; }
const R = rng(1337);
const rr = (a, b) => a + R() * (b - a);

// posição de um volume do mundo, já na cena
function spot(name, dx = 0, dz = 0) {
  const p = placement(name);
  if (!p) return null;
  return { x: p.plan[0] + dx, z: PLAN_Z_SIGN * p.plan[1] + dz, hw: p.half[0], hd: p.half[1], top: p.top, base: p.base };
}

function place(obj, x, z, rotY = 0, col, dynamic = false) {
  obj.position.set(x, groundHeight(x, z), z);
  obj.rotation.y = rotY;
  obj.userData.static = !dynamic;
  G.scene.add(obj);
  if (col) { if (col.r) addCircle(x, z, col.r); else addBox(x, z, col.hw, col.hd, rotY); }
  return obj;
}
function interactable(kind, x, z, r, label, data) {
  const it = { kind, x, z, r, label, data };
  G.interactables.push(it);
  return it;
}

// ---------------------------------------------------------------- entreposto sul (Vale)
function buildVale() {
  const market = spot('SouthMarket_01') || { x: LOC.mercado.x, z: LOC.mercado.z };
  const market2 = spot('SouthMarket_02') || { x: market.x + 16, z: market.z + 2 };
  const warehouse = spot('SouthWarehouse') || { x: LOC.vale.x + 40, z: LOC.vale.z - 40 };
  const stable = spot('SouthStable') || { x: LOC.vale.x + 50, z: LOC.vale.z + 20 };
  const workshop = spot('SouthWorkshop') || { x: LOC.vale.x - 60, z: LOC.vale.z - 40 };
  const tower = spot('SouthOutpost_Tower') || { x: LOC.vale.x - 50, z: LOC.vale.z - 26 };

  // mercado: as barracas já existem no cenário; aqui entram os vendedores e o alvo de negociação
  for (const [m, cloth] of [[market, PR.COL.tecido1], [market2, PR.COL.tecido3]]) {
    addNpc({ x: m.x, z: m.z + m.hd + 1.6, yaw: Math.PI, race: ['humano', 'anao', 'orc'][Math.floor(R() * 3)], cloth, role: 'merchant', face: true });
  }
  interactable('market', (market.x + market2.x) / 2, (market.z + market2.z) / 2 + 4, 9, 'Negociar no Mercado do Vale', 'vale');

  // bancada e forja, encostadas na oficina
  place(PR.forge(), workshop.x + workshop.hw + 2.6, workshop.z, Math.PI / 2, { hw: 1.7, hd: 1.5 });
  addNpc({ x: workshop.x + workshop.hw + 4.2, z: workshop.z + 1.6, yaw: Math.PI / 2, race: 'anao', cloth: '#6a4030', role: 'smith', work: true, weapon: 'martelo' });
  interactable('forge', workshop.x + workshop.hw + 3.4, workshop.z, 5, 'Usar a bancada: fabricar e reparar');

  // campo de treino ao pé da torre do posto
  const dummy = new THREE.Group();
  dummy.add(PR.box(0.25, 2, 0.25, PR.COL.madeira, 0, 1, 0), PR.box(1.4, 0.2, 0.2, PR.COL.madeira, 0, 1.6, 0), PR.box(0.6, 0.7, 0.4, '#c9b48a', 0, 1.2, 0));
  place(dummy, tower.x + 6, tower.z + 5, 0, { r: 0.5 });
  place(dummy.clone(), tower.x + 9, tower.z + 8, 0.4, { r: 0.5 });
  addNpc({ x: tower.x + 4, z: tower.z + 7, yaw: -Math.PI / 2, race: 'humano', cloth: '#3f5f7a', role: 'trainer', weapon: 'espada', face: true });
  interactable('trainer', tower.x + 4.5, tower.z + 6, 5.5, 'Falar com o instrutor');

  // armazém e estábulo, nos prédios correspondentes
  for (const [dx, dz] of [[-warehouse.hw - 1.2, -2.2], [-warehouse.hw - 2, 0.8], [warehouse.hw + 1.4, -1.6]]) {
    place(PR.crate(), warehouse.x + dx, warehouse.z + dz, R(), { r: 0.6 });
  }
  interactable('storage', warehouse.x, warehouse.z + warehouse.hd + 2, 5, 'Abrir o armazém');
  addNpc({ x: stable.x - stable.hw - 2.4, z: stable.z + 1.5, yaw: Math.PI / 2, race: 'orc', cloth: '#6a5a3a', role: 'stable', face: true });
  interactable('stable', stable.x - stable.hw - 2.4, stable.z, 5.5, 'Falar com o cuidador do estábulo');

  // quadro de relatos na entrada do mercado
  place(PR.noticeBoard(), market.x - 9, market.z + 6, Math.PI, { hw: 1.3, hd: 0.3 });
  interactable('board', market.x - 9, market.z + 7.5, 3.6, 'Ler o quadro de relatos');

  // caixas e barris soltos entre as casas
  for (let i = 0; i < 10; i++) {
    const x = market.x + rr(-24, 28), z = market.z + rr(-14, 18);
    if (!isClearGround(x, z, 0.8, 3)) continue;
    place(i % 2 ? PR.barrel() : PR.crate(PR.COL.madeiraClara, 0.8), x, z, R() * 3, { r: 0.5 });
  }

  // guardas nas duas frentes do entreposto
  const gate = spot('SouthHouse_01') || market;
  for (const [dx, dz, y] of [[-5, -6, Math.PI], [5, -6, Math.PI], [-6, 6, 0], [6, 6, 0]]) {
    addNpc({ x: gate.x + dx, z: gate.z + dz, yaw: y, race: 'humano', cloth: '#2f64a3', trim: '#d6b25a', role: 'guard', weapon: 'espada' });
  }
  for (const s of [-1, 1]) place(PR.banner(ZONES.protegida.color), market.x + s * 16, market.z + 12, 0);
}

// ---------------------------------------------------------------- entreposto norte (Alto)
function buildAlto() {
  const tower = spot('NorthOutpost_Tower') || { x: LOC.alto.x + 50, z: LOC.alto.z };
  const house = spot('NorthHouse_01') || { x: LOC.alto.x, z: LOC.alto.z };
  const stallCloths = [PR.COL.tecido4, PR.COL.tecido2];
  const base = { x: (tower.x + house.x) / 2, z: (tower.z + house.z) / 2 };
  stallCloths.forEach((c, i) => {
    const x = base.x + (i - 0.5) * 11, z = base.z + 7;
    // banca do kit Kenney com o telhado na cor do tecido; sem o modelo, a barraca procedural
    const banca = extraModel('banca', { w: 3.3, d: 2.5, tint: c });
    place(banca || PR.stall(c), x, z, 0, { hw: 1.6, hd: 1.3 }, !!banca);
    addNpc({ x, z: z - 1.6, yaw: 0, race: ['elfo', 'humano'][i % 2], cloth: c, role: 'merchant', face: true });
  });
  interactable('market', base.x, base.z + 10, 9, 'Negociar no Mercado do Alto', 'alto');
  for (const s of [-1, 1]) {
    addNpc({ x: base.x + s * 7, z: base.z + 14, yaw: 0, race: 'humano', cloth: '#2f64a3', trim: '#d6b25a', role: 'guard', weapon: 'espada' });
    place(PR.banner(ZONES.protegida.color), base.x + s * 13, base.z + 16, 0);
  }
  addNpc({ x: tower.x - 6, z: tower.z + 4, yaw: -0.6, race: 'elfo', cloth: '#35506e', hood: '#2a3d55', role: 'astro', face: true });
}

// ---------------------------------------------------------------- canteiro junto à ponte principal
function buildCanteiro() {
  const cx = CANTEIRO.x, cz = CANTEIRO.z;
  place(PR.tent(PR.COL.tecido2), cx + 4, cz + 2, 0, { r: 2 });
  place(PR.tent('#9a8a6a'), cx + 7, cz - 4, 0, { r: 2 });
  for (let i = 0; i < 5; i++) place(PR.crate(PR.COL.madeiraClara, 0.9), cx - 2 + (i % 3) * 1.1, cz + 5 + Math.floor(i / 3) * 1.1, 0, { r: 0.6 });
  const logs = new THREE.Group();
  for (let i = 0; i < 4; i++) logs.add(PR.box(3.6, 0.45, 0.45, PR.COL.madeiraClara, 0, 0.25 + (i > 2 ? 0.45 : 0), -0.5 + (i % 3) * 0.5));
  place(logs, cx - 5, cz - 2, 0.3, { hw: 1.8, hd: 0.8 });
  place(PR.banner(ZONES.protegida.color), cx - 3, cz + 10, 0);
  addNpc({ x: cx + 1, z: cz, yaw: -2.4, race: 'anao', cloth: '#6c8a4a', role: 'foreman', face: true, name: 'Mestra do canteiro' });
  interactable('canteiro', cx + 1, cz, 5, 'Falar com a mestra do canteiro');
}

// ---------------------------------------------------------------- garganta: marcos e ruína de vigia
function buildPassage() {
  for (const b of PASSAGE_PROPS.banners) place(PR.banner(ZONES.fronteira.color, 'diamond'), b.x, b.z, 0);
  const v = LOC.vigia;
  // torre com escombros no lugar da ruína procedural (mesmo colisor); os modelos texturizados ficam
  // fora da junção de malhas estáticas, que só guarda posição, normal e cor
  const torre = extraModel('torre_escombros', { w: 4.4, d: 4.4, fit: 'redonda' });
  place(torre || PR.watchtowerRuin(), v.x, v.z, Math.PI / 2, { hw: 2.2, hd: 2.2 }, !!torre);
  interactable('vigia', v.x + 3.4, v.z, 3.4, 'Registrar o movimento na Passagem');

  // destroços na subida da garganta
  const cart = new THREE.Group();
  cart.add(PR.box(2.2, 0.6, 1.4, PR.COL.madeira, 0, 0.8, 0));
  for (const [x, z] of [[-0.8, 0.8], [0.8, 0.8], [0.8, -0.8]]) {
    const w = PR.mesh(new THREE.CylinderGeometry(0.5, 0.5, 0.15, 8), PR.COL.madeiraClara);
    w.rotation.x = Math.PI / 2; w.position.set(x, 0.5, z); cart.add(w);
  }
  place(cart, PASSAGE_PROPS.cart.x, PASSAGE_PROPS.cart.z, 0.6, { r: 1.4 });
  place(PR.palisade(6), PASSAGE_PROPS.palisade.x, PASSAGE_PROPS.palisade.z, 0.5, { hw: 3, hd: 0.4 });
}

// ---------------------------------------------------------------- acampamento reativo
function buildCamp() {
  const c = spot('POI_ReactiveCamp') || { x: LOC.acampamento.x, z: LOC.acampamento.z, hw: 7, hd: 7 };
  for (let i = 0; i < 4; i++) {
    const a = i / 4 * Math.PI * 2 + 0.4;
    place(PR.tent(i % 2 ? '#7a6048' : '#8a7058'), c.x + Math.cos(a) * 11, c.z + Math.sin(a) * 11, 0, { r: 1.9 });
  }
  const p1 = PR.palisade(10); place(p1, c.x + 5, c.z - 15, 0.2, { hw: 5, hd: 0.4 });
  const p2 = PR.palisade(9); place(p2, c.x + 14, c.z + 9, 1.2, { hw: 4.5, hd: 0.4 });
  const extra = PR.palisade(8); place(extra, c.x - 15, c.z + 5, Math.PI / 2 + 0.2, null, true);
  CAMP.extraPalisade = extra; extra.visible = false;
  for (let i = 0; i < 6; i++) {
    const b = PR.box(0.12, 0.12, 0.9, '#e8dcc0', 0, 0.08, 0);
    b.rotation.y = R() * 3;
    place(b, c.x + rr(-7, 7), c.z + rr(-7, 7), R() * 3);
  }
}

// ---------------------------------------------------------------- observatório
function buildObservatory() {
  const o = spot('POI_RegionalObservatory') || { x: LOC.observatorio.x, z: LOC.observatorio.z, hw: 11 };
  interactable('observatory', o.x, o.z + o.hw + 1.5, 4, 'Observar o céu pelo instrumento');
  addNpc({ x: o.x + o.hw + 2.5, z: o.z + 3, yaw: 2.4, race: 'elfo', cloth: '#35506e', hood: '#2a3d55', role: 'astronomer', face: true });
  interactable('astronomer', o.x + o.hw + 2.5, o.z + 3, 3.4, 'Falar com a astrônoma');
}

// ---------------------------------------------------------------- Ermos Quebrados
function buildErmos() {
  const e = LOC.ermos;
  const D = rng(90210);
  for (let i = 0; i < 26; i++) {
    const a = D() * Math.PI * 2, r = 20 + D() * (e.rx - 30);
    const x = e.x + Math.cos(a) * r, z = e.z + Math.sin(a) * r * (e.rz / e.rx);
    if (!isClearGround(x, z, 1.2, 5)) continue;
    const c = PR.ruinColumn([1.4, 3.5, 5, 2.2][i % 4]);
    if (i % 5 === 0) c.rotation.z = 1.35;
    place(c, x, z, D() * 3, { r: 0.8 });
  }
  // marcos de Full Loot na borda da elipse
  for (let i = 0; i < 16; i++) {
    const a = i / 16 * Math.PI * 2;
    const x = e.x + Math.cos(a) * e.rx, z = e.z + Math.sin(a) * e.rz;
    if (Math.abs(x) > HALF - 12 || Math.abs(z) > HALF - 12) continue;
    place(PR.banner(ZONES.fullloot.color, 'diamond'), x, z, -a);
  }
}

// ---------------------------------------------------------------- mina, caverna e portal
function buildPointsOfInterest() {
  const mine = spot('POI_Mine_Entrance');
  if (mine) {
    for (let i = 0; i < 4; i++) place(PR.crate(PR.COL.madeiraClara, 0.9), mine.x - 6 + i * 1.3, mine.z + mine.hd + 3, 0, { r: 0.6 });
    place(PR.banner(ZONES.fronteira.color, 'diamond'), mine.x + 8, mine.z + 7, 0);
  }
  const cave = spot('POI_CaveWest_Entrance');
  if (cave) place(PR.banner(ZONES.fronteira.color, 'diamond'), cave.x + 6, cave.z + 6, 0);
  const portal = spot('POI_TurbulentPortal_Surface');
  if (portal) {
    for (const s of [-1, 1]) place(PR.banner(ZONES.fronteira.color, 'diamond'), portal.x + s * 9, portal.z + 8, 0);
  }
}

// ---------------------------------------------------------------- pontos de coleta
function addNode(kind, x, z, extra = 0) {
  const mesh = PR.resourceNode(kind);
  place(mesh, x, z, R() * 6, { r: kind === 'madeira' ? 0.7 : kind === 'erva' ? 0 : 1.1 }, true);
  const zone = zoneAt(x, z);
  const n = { kind, x, z, mesh, charges: 3, max: 3, regrowAt: 0, yieldBonus: (zone === 'fronteira' ? 1 : zone === 'fullloot' ? 2 : 0) + extra };
  NODES.push(n);
  interactable('node', x, z, kind === 'madeira' ? 2.8 : 2.6, null, n);
}

// Pontos de coleta: a lista é escolhida junto das áreas funcionais, antes da floresta, para a mata
// abrir espaço em volta de cada um (`world/functional-areas.js`).
function scatterNodes() {
  for (const n of FUNCTIONAL.nodes) addNode(n.kind, n.x, n.z, n.extra);
}

// ---------------------------------------------------------------- junção das malhas estáticas
function mergeStatic() {
  const buckets = new Map();
  const roots = G.scene.children.filter((o) => o.userData.static);
  for (const root of roots) {
    root.updateMatrixWorld(true);
    root.traverse((o) => {
      if (!o.isMesh) return;
      for (let p = o; p && p !== root.parent; p = p.parent) if (!p.visible) return;
      const g = o.geometry.index ? o.geometry.toNonIndexed() : o.geometry.clone();
      g.applyMatrix4(o.matrixWorld);
      const mat = o.material;
      const juntavel = mat.userData.surface && !mat.transparent && (!mat.emissive || mat.emissive.getHex() === 0);
      const key = juntavel ? `s:${mat.userData.surface}:${o.castShadow ? 's' : 'n'}` : mat.uuid + (o.castShadow ? ':s' : ':n');
      if (!buckets.has(key)) buckets.set(key, { mat, cast: o.castShadow, geos: [], cores: juntavel ? [] : null, surface: juntavel ? mat.userData.surface : null });
      const b = buckets.get(key);
      b.geos.push(g);
      if (b.cores) b.cores.push(mat.color);
    });
    G.scene.remove(root);
  }
  for (const { mat, cast, geos, cores, surface } of buckets.values()) {
    let n = 0; for (const g of geos) n += g.attributes.position.count;
    const pos = new Float32Array(n * 3), nor = new Float32Array(n * 3), col = cores ? new Float32Array(n * 3) : null;
    let o = 0;
    geos.forEach((g, i) => {
      pos.set(g.attributes.position.array, o * 3);
      if (g.attributes.normal) nor.set(g.attributes.normal.array, o * 3);
      if (col) { const c = cores[i]; for (let v = 0; v < g.attributes.position.count; v++) { col[(o + v) * 3] = c.r; col[(o + v) * 3 + 1] = c.g; col[(o + v) * 3 + 2] = c.b; } }
      o += g.attributes.position.count; g.dispose();
    });
    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    geo.setAttribute('normal', new THREE.BufferAttribute(nor, 3));
    if (col) geo.setAttribute('color', new THREE.BufferAttribute(col, 3));
    geo.computeBoundingSphere();
    const m = new THREE.Mesh(geo, surface ? PR.mat('#ffffff', { vertexColors: true }, surface) : mat);
    m.castShadow = cast; m.receiveShadow = true;
    G.scene.add(m);
  }
}

// ---------------------------------------------------------------- montagem
export function buildWorld() {
  buildVale(); buildAlto(); buildCanteiro(); buildPassage(); buildCamp(); buildObservatory(); buildErmos();
  buildPointsOfInterest();
  scatterNodes();
  mergeStatic();
  initTravelers();

  // Encontros: os grupos seguem as rotas de risco alto e os marcos afastados (posições em `layout.js`).
  const gorgeActive = () => !EVT.done || Math.random() < 0.35;
  const at = Object.fromEntries(ENCOUNTERS.map((e) => [e.id, e]));
  const group = (id, members, opts) => defineGroup(id, { x: at[id].x, z: at[id].z }, members, { leash: at[id].leash, ...opts });
  group('passagem-sul', [['saqueador', 0, 0], ['saqueador', 3, 2]], { respawn: 75, active: gorgeActive });
  group('passagem-norte', [['saqueador', 0, 0], ['saqueadorArco', -2, -4]], { respawn: 90, active: gorgeActive });
  group('mina', [['saqueador', 0, 0], ['saqueadorArco', 4, -3]], { respawn: 100 });
  group('ermos-a', [['saqueador', 0, 0], ['saqueador', 3, 3], ['saqueadorArco', -3, 4]], { respawn: 100 });
  group('ermos-b', [['garraPalida', 0, 0], ['garraPalida', 3, -2]], { respawn: 80 });
  group('bosque-norte', [['garra', 0, 0]], { respawn: 120 });
  group('observatorio', [['saqueador', 0, 0], ['saqueadorArco', 5, 3]], { respawn: 110 });
}

// Guarda real no canteiro (pode levantar o jogador derrubado)
export function initCanteiroGuard(spawnEnemy) {
  const p = { x: LOC.canteiro.x - 7, z: LOC.canteiro.z - 6, yaw: Math.PI };
  const g = spawnEnemy('guarda', p.x, p.z, { post: p });
  g.guardPost = true;
}
export { fbm };
