// Harness de paridade da simulação (empacotado junto com sim-world.entry.js pelo bake).
//
// Roda as funções de jogo da própria demo — updatePlayer, updateEnemies, combate, economia,
// acampamento, evento, céu, Turbulenta — no Node, quadro a quadro, com teclas e mira roteirizadas e
// `Math.random` trocado por um mulberry com semente. Grava o rastro (posições, estados, vida, vigor,
// inimigos, projéteis) que a simulação em C++ tem que reproduzir exatamente.
//
// O que é só visual e também sorteava com Math.random (recuo e tremor de câmera, partículas,
// olhares dos personagens) passa por `__visRand`, um gerador à parte: no C++ isso é do cliente.
// Os trechos trocados em demo/src só existem no pacote do bake (plugin de bake-sim-world.mjs).
import * as THREE from 'three';
import { G } from '../../src/state.js';
import { input, endFrame } from '../../src/engine/input.js';
import { initFx } from '../../src/engine/fx.js';
import { createSky } from '../../src/engine/sky.js';
import { initHud } from '../../src/ui/hud.js';
import { groundHeight } from '../../src/engine/terrain.js';
import { createPlayer, updatePlayer, mountUp } from '../../src/game/player.js';
import { initMount, updateMount, mountAction } from '../../src/game/mount.js';
import { spawnEnemy, removeEnemy, updateEnemies, updateGroups, groups } from '../../src/game/enemies.js';
import { updateProjectiles, hurtEnemy, hurtPlayer } from '../../src/game/combat.js';
import { initCamp, updateCamp, CAMP, __applyComposition } from '../../src/game/camp.js';
import { initEvent, updateEvent, EVT, deliver } from '../../src/game/event.js';
import { SKY, updateStars, observe, endObserve } from '../../src/game/stars.js';
import { TS, updateTurbulent, forcePortalNear, enter } from '../../src/game/turbulent.js';
import { MKT, updateMarkets, sell, buy, buyMount, restartKit, learn, craft, repair } from '../../src/game/economy.js';
import { updateLootBags, respawn } from '../../src/game/zones.js';
import { NPCS, updateNpcs } from '../../src/game/npcs.js';
import { updateInteract } from '../../src/game/interact.js';
import { updateDiscovery, __resetDiscovery } from '../../src/game/discovery.js';
import { resetBook, BOOK, first } from '../../src/game/book.js';
import { NODES, initCanteiroGuard } from '../../src/game/world.js';
import { addTo, makeGear, receive } from '../../src/game/inventory.js';
import { MARKETS, RECIPES, STARTS, ITEMS } from '../../src/config.js';

export function mulberry(seed) {
  return () => {
    seed |= 0; seed = seed + 0x6D2B79F5 | 0;
    let t = Math.imul(seed ^ seed >>> 15, 1 | seed);
    t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
    return ((t ^ t >>> 14) >>> 0) / 4294967296;
  };
}

// Elemento de DOM que aceita qualquer coisa: o HUD e os efeitos escrevem em elementos que aqui não
// existem, e nada disso entra no rastro.
const universal = new Proxy(function () {}, {
  get: (t, k) => (k === Symbol.toPrimitive ? () => 0 : k === 'length' ? 0 : universal),
  set: () => true,
  apply: () => universal,
});

let ready = false;
const ORIGINAL_SELL = { alto: MARKETS.alto.sell.ferramentas, vale: MARKETS.vale.sell.ferramentas };
const PLAYER_STATES = ['free', 'attack', 'skill', 'dodge', 'stagger', 'channel', 'down', 'dead'];
const ENEMY_STATES = ['idle', 'loot', 'chase', 'windup', 'recover', 'stun', 'return', 'dead'];

function setup() {
  if (ready) return;
  ready = true;
  globalThis.__visRand = mulberry(99);
  document.getElementById = () => universal;
  document.createElement = () => universal;
  const camera = new THREE.PerspectiveCamera(55, 16 / 9, 0.1, 3000);
  G.camera = camera;
  G.cam = { yaw: 0, pitch: 0.67, dist: 14, mode: 'follow', look: new THREE.Vector3(), skyDir: null };
  G.running = true;
  initFx(G.scene, camera);
  initHud();
  G.sky = createSky(G.scene, { getPixelRatio: () => 1 });
}

// ---------------------------------------------------------------- mira roteirizada
// Substitui `updateAim` (raio da câmera): a mira vem pronta, como no comando do cliente em C++.
let aimSpec = null;
export function aimTarget(P, spec, enemies) {
  if (spec && spec.enemy != null) {
    const e = enemies[spec.enemy];
    if (e) return { x: e.pos.x, y: e.pos.y + (e.barY || 2.4) * 0.45, z: e.pos.z, enemy: e.alive && e.faction !== 'guard' ? e : null };
  }
  if (spec && spec.point) return { x: spec.point[0], y: spec.point[1], z: spec.point[2], enemy: null };
  const d = spec && spec.ahead != null ? spec.ahead : 20;
  return { x: P.pos.x + Math.sin(P.yaw) * d, y: P.pos.y + 1.45, z: P.pos.z + Math.cos(P.yaw) * d, enemy: null };
}
let spawned = [];
globalThis.__aimHook = (P) => {
  const t = aimTarget(P, aimSpec, spawned);
  if (!P.aimPoint) P.aimPoint = new THREE.Vector3();
  P.aimPoint.set(t.x, t.y, t.z);
  P.aimEnemy = t.enemy;
  const dx = t.x - P.pos.x, dz = t.z - P.pos.z, horizDist = Math.hypot(dx, dz);
  P.aimYaw = Math.atan2(dx, dz);
  const dy = t.y - (P.pos.y + 1.45);
  const pitch = Math.atan2(dy, Math.max(1.2, horizDist));
  P.aimPitch = pitch < -0.85 ? -0.85 : pitch > 0.85 ? 0.85 : pitch;
  P.aimDist = Math.hypot(dx, dy, dz);
};

// ---------------------------------------------------------------- estado inicial
function resetWorld(spec) {
  for (const e of [...G.enemies]) removeEnemy(e);
  groups.length = 0;
  for (const p of G.projectiles) G.scene.remove(p.m);
  G.projectiles.length = 0;
  for (const b of [...G.lootBags]) b.remove();
  G.time = spec.time ?? 0;
  G.stats = {}; G.flags = {};
  G.cinematic = false; G.uiOpen = null; G.anon = false;
  Object.assign(CAMP, { pop: 9, state: 'estabelecido', since: 0, pending: false, members: [], playerKills: 0, regenT: 0 });
  Object.assign(EVT, {
    progress: 0, done: false, doneAt: null, othersPts: 0, playerPts: 0, othersT: 0,
    contrib: { ferramentas: 0, lingotes: 0, reconhecimento: 0, saqueadores: 0, acampamento: 0 },
    delivered: { ferramentas: 0, lingote: 0 },
  });
  MARKETS.alto.sell.ferramentas = ORIGINAL_SELL.alto;
  MARKETS.vale.sell.ferramentas = ORIGINAL_SELL.vale;
  Object.assign(SKY, { vanished: [], next: 50, observed: new Set(), instrumentGiven: false });
  if (TS.portal) G.scene.remove(TS.portal.mesh);
  Object.assign(TS, { portal: null, next: 75, inside: false, origin: null, enteredAt: 0, collapseAt: 0, shadows: [], encounters: new Map(), fragIn: 0, warned: {}, lateSpawn: false });
  MKT.vale.demand = {}; MKT.alto.demand = {};
  resetBook();
  for (const n of NODES) { n.charges = n.max = 3; n.regrowAt = 0; }
  __resetDiscovery();
}

// Mesma sequência de main.js `startGame`, depois do boot (buildWorld já rodou com a semente do boot).
function startGameLikeDemo(profile) {
  G.player = createPlayer(profile);
  initMount();
  initCamp(); initEvent();
  initCanteiroGuard(spawnEnemy);
  first('chegada', 'historia', null, 'Chegada ao Entreposto do Vale', `${profile.name} chegou ao Vale com ${STARTS[profile.start].label} como primeiro conhecimento e ${ITEMS[STARTS[profile.start].weapon].name.toLowerCase()} na mão.`);
}

function applyPlayer(P, o) {
  if (!o) return;
  if (o.x != null) P.pos.set(o.x, groundHeight(o.x, o.z), o.z);
  if (o.yaw != null) P.yaw = o.yaw;
  if (o.coins != null) P.coins = o.coins;
  if (o.hp != null) P.hp = o.hp;
  if (o.vigor != null) P.vigor = o.vigor;
  if (o.trainings) for (const t of o.trainings) P.trainings.add(t);
  if (o.clearInv) P.inv.length = 0;
  if (o.inv) for (const [id, q, cond] of o.inv) addTo(P.inv, id, q, cond);
  if (o.storage) for (const [id, q] of o.storage) addTo(P.storage, id, q);
  if (o.equip) for (const [slot, id, cond] of o.equip) P.equip[slot] = id ? makeGear(id, cond ?? 100) : null;
  if (o.mounted) { G.mount.spawnNear(P); mountUp(P); }
}

function spawnList(list) {
  for (const s of list || []) {
    const o = {};
    if (s.leash != null) o.leash = s.leash;
    if (s.post) o.post = { ...s.post };
    if (s.patrol) o.patrol = s.patrol.map((p) => [...p]);
    spawned.push(spawnEnemy(s.type, s.x, s.z, o));
  }
}

// ---------------------------------------------------------------- ações roteirizadas (painéis)
function call(P, c) {
  const [op, ...a] = c;
  switch (op) {
    case 'sell': return sell(P, a[0], a[1], a[2]);
    case 'buy': return buy(P, a[0], a[1], a[2]);
    case 'craft': return craft(P, RECIPES[a[0]], a[1]);
    case 'repair': return repair(P, a[0]);
    case 'learn': return learn(P, a[0]);
    case 'kit': return restartKit(P);
    case 'buyMount': return buyMount(P);
    case 'deliver': return deliver(P, a[0]);
    case 'give': return receive(P, a[0], a[1]);
    case 'kill': { const e = spawned[a[0]]; if (e) hurtEnemy(e, 9999); return; }
    case 'killCamp': { const e = CAMP.members[a[0]]; if (e && e.alive) hurtEnemy(e, 9999); return; }
    case 'hurt': return hurtPlayer(a[0], { x: P.pos.x + 1, z: P.pos.z, name: 'teste' });
    case 'portal': return forcePortalNear();
    case 'enter': return enter(P);
    case 'respawn': return respawn(P);
    case 'observe': return observe(G.nightNow || 0);
    case 'endObserve': return endObserve();
    case 'time': G.time += a[0]; return;
    case 'spawn': return spawnList([a[0]]);
    case 'composeCamp': return __applyComposition();
    case 'clickAttack': { const e = spawned[a[0]]; if (e) P.cmd = { type: 'attack', e }; return; }
    case 'wield': P.equip.weapon = makeGear(a[0], a[1] ?? 100); P.warnedBroken = false; return;
    case 'equip': P.equip[a[0]] = a[1] ? makeGear(a[1], a[2] ?? 100) : null; return;
    case 'coins': P.coins += a[0]; return;
    case 'refresh': P.cd.Q = 0; P.cd.E = 0; P.vigor = P.maxVigor; P.hp = P.maxHp; return;
    case 'tp': P.pos.set(a[0], groundHeight(a[0], a[1]), a[1]); return;
    default: throw new Error(`harness: ação desconhecida ${op}`);
  }
}

// ---------------------------------------------------------------- rastro
// Cada quadro vira uma lista de números (posições, estados, vida, vigor, inimigos, projéteis). O
// fixture guarda o hash FNV-1a dos bits desses doubles em todo quadro e a lista completa a cada
// TRACE_EVERY quadros (para achar onde divergiu). O C++ monta a mesma lista e compara.
const r = (v) => v;
const TRACE_EVERY = 15;
let ENEMY_TYPES = null;
function tracePlayer(P) {
  return [r(P.pos.x), r(P.pos.y), r(P.pos.z), r(P.yaw), PLAYER_STATES.indexOf(P.state), r(P.stateT), r(P.hp), r(P.vigor),
    r(P.coins), P.air ? r(P.air.y) : -1, r(P.speedNow)];
}
function traceEnemies() {
  return G.enemies.map((e) => [ENEMY_TYPES.indexOf(e.type), e.alive ? 1 : 0, r(e.pos.x), r(e.pos.y), r(e.pos.z), r(e.yaw), r(e.hp), ENEMY_STATES.indexOf(e.state), r(e.t)]);
}
function traceProjectiles() {
  return G.projectiles.map((p) => [r(p.x), r(p.y), r(p.z)]);
}
export function fnv(values) {
  const bytes = new Uint8Array(new Float64Array(values).buffer);
  let h = 0x811c9dc5;
  for (let i = 0; i < bytes.length; i++) { h ^= bytes[i]; h = Math.imul(h, 0x01000193) >>> 0; }
  return h.toString(16).padStart(8, '0');
}
function flatFrame(p, e, pr) {
  const out = [...p, e.length];
  for (const x of e) out.push(...x);
  out.push(pr.length);
  for (const x of pr) out.push(...x);
  return out;
}
function summary(P) {
  const list = (l) => l.map((e) => [e.id, e.qty || 1, e.uid ? r(e.cond) : null]);
  return {
    inv: list(P.inv), saddle: list(P.saddle), storage: list(P.storage), stableBags: list(P.stableBags || []),
    equip: ['weapon', 'armor', 'bag', 'mount'].map((s) => (P.equip[s] ? [P.equip[s].id, r(P.equip[s].cond)] : null)),
    coins: P.coins, trainings: [...P.trainings],
    stats: Object.entries(G.stats), flags: Object.keys(G.flags).sort(),
    book: BOOK.entries.map((e) => [e.key, e.count]), titles: BOOK.titles.map((t) => t.id),
    camp: [CAMP.state, CAMP.pop], evt: [EVT.progress, EVT.done ? 1 : 0, EVT.playerPts, EVT.othersPts],
    sky: SKY.vanished.map((v) => [v.index, v.time]),
    bags: G.lootBags.map((b) => [r(b.x), r(b.z), b.items.map((e) => [e.id, e.qty || 1])]),
    portal: TS.portal ? [r(TS.portal.x), r(TS.portal.z), r(TS.portal.expires)] : null,
    demand: [Object.entries(MKT.vale.demand), Object.entries(MKT.alto.demand)],
    nodes: NODES.map((n) => [n.charges, n.regrowAt]),
    npcs: NPCS.filter((n) => n.kind === 'traveler').map((n) => [r(n.pos.x), r(n.pos.z)]),
  };
}

// Um quadro: a mesma ordem de main.js `tick` (sem render, HUD, câmera e ponteiro).
function tick(P, dt, sys) {
  const on = (k) => !sys || sys.includes(k);
  if (!G.cinematic && P.state !== 'dead' && input.pressed.has('KeyR') && !G.uiOpen) mountAction(P);
  G.time += dt;
  if (!G.uiOpen && !G.cinematic && P.state !== 'down' && P.state !== 'dead' && P.aiming) P.cmd = null;
  updatePlayer(dt);
  if (on('mount')) updateMount(dt);
  if (on('enemies')) updateEnemies(dt);
  if (on('groups')) updateGroups();
  if (on('projectiles')) updateProjectiles(dt);
  if (on('camp')) updateCamp(dt);
  if (on('event')) updateEvent(dt);
  if (on('stars')) updateStars(dt, G.nightNow || 0);
  if (on('turbulent')) updateTurbulent(dt);
  if (on('markets')) updateMarkets(dt);
  if (on('lootbags')) updateLootBags();
  if (on('npcs')) updateNpcs(dt);
  if (on('interact')) updateInteract();
  if (on('discovery')) updateDiscovery(dt);
  const hour = ((G.time + 9 / 24 * 420) % 420) / 420 * 24;
  const a = (hour - 6) / 24 * Math.PI * 2;
  G.nightNow = 1 - THREE.MathUtils.smoothstep(Math.sin(a), -0.22, 0.02);
}

// ---------------------------------------------------------------- execução
// `boot`: o cenário continua do estado logo após buildWorld (com a semente do boot) e segue a
// sequência de startGame; os demais zeram o mundo e montam só o que o roteiro pede.
export function runScenario(spec, enemyTypes) {
  setup();
  ENEMY_TYPES = enemyTypes;
  spawned = [];
  aimSpec = null;
  if (spec.boot) {
    startGameLikeDemo(spec.profile);
  } else {
    resetWorld(spec);
    Math.random = mulberry(spec.seed);
    G.player = createPlayer(spec.profile);
    if (!G.mount) initMount();
    G.mount.present = false;
    if (spec.camp) { CAMP.pending = true; __applyComposition(); }
  }
  const P = G.player;
  applyPlayer(P, spec.player);
  spawnList(spec.enemies);
  const frames = [];
  const dt = spec.dt ?? 1 / 30;
  for (let f = 0; f < spec.frames.length; f++) {
    const fr = spec.frames[f];
    for (const c of fr.calls || []) call(P, c);
    input.keys = new Set(fr.keys || []);
    input.pressed = new Set(fr.press || []);
    input.mouse.right = !!fr.right;
    input.mouse.leftPressed = !!fr.fire;
    G.cam.yaw = fr.cam ?? 0;
    G.uiOpen = fr.ui ? 'panel' : null;
    aimSpec = fr.aim || null;
    tick(P, dt, spec.systems);
    const p = tracePlayer(P), e = traceEnemies(), pr = traceProjectiles();
    const rec = { h: fnv(flatFrame(p, e, pr)) };
    if (f % TRACE_EVERY === 0 || f === spec.frames.length - 1) Object.assign(rec, { p, e, pr });
    frames.push(rec);
    endFrame();
  }
  return { frames, end: summary(P) };
}
