// Região Turbulenta (cap. 09): localizar → comprometer-se → explorar → escolher → extrair ou perder.
import * as THREE from 'three';
import { G, emit, rand, pick, stat } from '../state.js';
import { groundHeight } from '../engine/terrain.js';
import { portalMesh, shrine, extractionStone } from '../engine/props.js';
import { sfx, setAmbience } from '../engine/audio.js';
import { TURB, PORTAL_SPOTS, placeAt, turbAt } from './layout.js';
import { TURB_PLACEMENTS } from '../world/runtime-manifest.js';
import { PLAN_Z_SIGN } from '../world/coordinates.js';
import { addCircle } from './collide.js';
import { spawnEnemy, removeEnemy } from './enemies.js';
import { receive, count } from './inventory.js';
import { startChannel } from './player.js';
import { log, grantTitle } from './book.js';
import { toast } from '../ui/hud.js';

export const DURATION = 240;
export const TS = {
  portal: null, next: 75, inside: false, origin: null, enteredAt: 0, collapseAt: 0,
  shrines: [], exits: [], shadows: [], encounters: new Map(), fragIn: 0, warned: {}, lateSpawn: false,
};

// Posições vindas do manifesto da Turbulenta (plano local da cena, convertido em `turbAt`).
// `turbulent/points_of_interest.json` vem vazio na fonte: o que vale são as âncoras e as ruínas.
const RUINS = TURB_PLACEMENTS.filter((p) => p.kind === 'ruin')
  .map((p) => ({ x: p.plan[0], z: PLAN_Z_SIGN * p.plan[1] }));
const local = (id) => { const a = turbAt(id); return { x: a.x - TURB.x, z: a.z - TURB.z }; };

// fragmentos: as três ruínas da região, sendo a primeira o objetivo declarado
const SHRINE_POS = (RUINS.length ? RUINS : [local('objective_ruins')]).map((p) => [p.x, p.z]);
// duas saídas: leste e oeste
const EXIT_POS = [local('exit_east'), local('exit_west')].map((p) => [p.x, p.z]);
// entrada única, com pequena dispersão para não repetir sempre o mesmo passo
const START_POS = [local('entry')].map((p) => [p.x, p.z]);
export const OBJECTIVE = local('objective_ruins');
export const PORTAL_CORE = local('portal_core');

// O terreno, as ruínas, o portal, as raízes e os fragmentos suspensos vêm do GLB da Turbulenta.
// Aqui entram apenas os objetos de jogo: os fragmentos recolhíveis e as duas pedras de extração.
export function initTurbulent() {
  const s = G.scene;
  for (const [x, z] of SHRINE_POS) {
    const m = shrine(); const wx = TURB.x + x, wz = TURB.z + z;
    m.position.set(wx, groundHeight(wx, wz), wz); s.add(m); addCircle(wx, wz, 1.5);
    TS.shrines.push({ x: wx, z: wz, mesh: m, taken: false });
  }
  for (const [x, z] of EXIT_POS) {
    const m = extractionStone(); const wx = TURB.x + x, wz = TURB.z + z;
    m.position.set(wx, groundHeight(wx, wz), wz); s.add(m); addCircle(wx, wz, 0.9);
    TS.exits.push({ x: wx, z: wz, mesh: m });
  }
}

// ---------- portais no mundo ----------
function spawnPortal() {
  const P = G.player;
  const spots = PORTAL_SPOTS.filter(([x, z]) => Math.hypot(x - P.pos.x, z - P.pos.z) > 25);
  const [x, z] = pick(spots.length ? spots : PORTAL_SPOTS);
  const mesh = portalMesh();
  mesh.position.set(x, groundHeight(x, z), z);
  G.scene.add(mesh);
  TS.portal = { x, z, mesh, expires: G.time + 150, place: placeAt(x, z) };
  const dir = compass(x - P.pos.x, z - P.pos.z);
  toast(`Um rasgo de luz violeta surgiu ${dir} — ${TS.portal.place}. Ficará aberto por pouco tempo.`, 'turb');
  sfx('portal');
  emit('portal', TS.portal);
}
export function compass(dx, dz) {
  const a = (Math.atan2(dx, -dz) * 180 / Math.PI + 360) % 360;
  return ['ao norte', 'a nordeste', 'a leste', 'a sudeste', 'ao sul', 'a sudoeste', 'a oeste', 'a noroeste'][Math.round(a / 45) % 8];
}
function closePortal() {
  if (!TS.portal) return;
  G.scene.remove(TS.portal.mesh);
  TS.portal = null;
  TS.next = G.time + 100 + rand(0, 40);
}
export function forcePortalNear() {
  closePortal();
  const P = G.player;
  const x = P.pos.x + Math.sin(P.yaw) * 8, z = P.pos.z + Math.cos(P.yaw) * 8;
  const mesh = portalMesh(); mesh.position.set(x, groundHeight(x, z), z); G.scene.add(mesh);
  TS.portal = { x, z, mesh, expires: G.time + 150, place: placeAt(x, z) };
}

// ---------- entrada ----------
export function canEnter(P) {
  if (P.mounted) return 'Desmonte e deixe a montaria do lado de fora.';
  if (P.state !== 'free') return 'Termine a ação atual antes de atravessar.';
  return null;
}
export function enter(P) {
  const p = TS.portal;
  TS.origin = { x: p.x + 2.5, z: p.z + 2.5 };
  TS.inside = true; TS.enteredAt = G.time; TS.collapseAt = G.time + DURATION;
  TS.fragIn = count(P.inv, 'fragmento'); TS.warned = {}; TS.encounters = new Map(); TS.lateSpawn = false;
  for (const s of TS.shrines) { s.taken = false; s.mesh.getObjectByName('crystal').visible = true; }
  const [sx0, sz0] = pick(START_POS);
  const sx = sx0 + rand(-6, 6), sz = sz0 + rand(-6, 6);
  P.pos.set(TURB.x + sx, groundHeight(TURB.x + sx, TURB.z + sz), TURB.z + sz);
  // de costas para a borda, olhando para o portal central
  P.yaw = Math.atan2(PORTAL_CORE.x - sx, PORTAL_CORE.z - sz);
  // figuras desconhecidas: outros "jogadores" simulados, identidades ocultas
  const types = ['sombraEspada', 'sombraArco', 'sombraMagica'];
  TS.shadows = types.map((t, i) => {
    const [x, z] = SHRINE_POS[i % SHRINE_POS.length];
    const e = spawnEnemy(t, TURB.x + x + rand(-6, 6), TURB.z + z + rand(-6, 6), { leash: 70, patrol: SHRINE_POS.map(([a, b]) => [TURB.x + a, TURB.z + b]) });
    e.pi = i; return e;
  });
  closePortal();
  stat('incursoes');
  setAmbience('turb'); sfx('portal');
  G.anon = true;
  emit('turb-enter');
}

function encounterFor(e) {
  const wName = ITEMS[e.cfg.weapon]?.name?.toLowerCase() || e.cfg.weapon || 'espada';
  if (!TS.encounters.has(e)) TS.encounters.set(e, { weapon: wName, result: null });
  return TS.encounters.get(e);
}

export function leave(P, success) {
  const frags = count(P.inv, 'fragmento') - TS.fragIn;
  const enc = [...TS.encounters.values()];
  const encTxt = enc.length
    ? enc.map((x) => `uma figura desconhecida que portava ${x.weapon}${x.result ? ` (${x.result})` : ''}`).join('; ')
    : 'nenhum confronto';
  for (const e of TS.shadows) if (G.enemies.includes(e)) removeEnemy(e);
  TS.shadows = [];
  TS.inside = false; G.anon = false;
  setAmbience('world');
  if (success) {
    P.pos.set(TS.origin.x, groundHeight(TS.origin.x, TS.origin.z), TS.origin.z);
    stat('extracoes');
    log('historia', 'risco', 'Incursão em uma Região Turbulenta',
      `Atravessou sem companhia um portal temporário para ruínas sob névoa violeta. Objetivo: reunir fragmentos. Resultado: extraiu com ${Math.max(0, frags)} fragmento${frags === 1 ? '' : 's'}. Encontros: ${encTxt}. Identidades preservadas.`, 'turb' + G.time);
    grantTitle('voltou-turbulenta', 'Quem voltou da Turbulenta', 'Extraiu de uma Região Turbulenta.');
    toast('Extração concluída. Você retornou ao local do portal.', 'event');
  } else {
    log('historia', 'risco', 'Uma incursão sem retorno',
      `Entrou numa Região Turbulenta e não alcançou uma saída. Tudo o que levava ficou lá. Encontros: ${encTxt}. Identidades preservadas.`, 'turb' + G.time);
  }
  emit('turb-leave', { success });
}

export function extract(P, exit) {
  startChannel(P, 'Extraindo', 4, () => leave(P, true), { interruptible: true, anim: false });
}
export function takeShrine(P, s) {
  startChannel(P, 'Recolhendo fragmento', 1.8, () => {
    if (s.taken) return;
    const got = receive(P, 'fragmento', 1);
    if (!got) { toast('Sem capacidade para o fragmento.', 'warn'); return; }
    s.taken = true; s.mesh.getObjectByName('crystal').visible = false;
    toast('+1 Fragmento anômalo', 'item'); sfx('pickup');
  });
}

export function updateTurbulent(dt) {
  const P = G.player;
  if (TS.portal) {
    const m = TS.portal.mesh;
    m.getObjectByName('disc').material.opacity = 0.45 + Math.sin(G.time * 3) * 0.15;
    m.children.filter((c) => c.name === 'shard').forEach((c) => { const a = c.userData.a + G.time * 1.3; c.position.set(Math.cos(a) * 3, 3 + Math.sin(a * 2) * 0.6, Math.sin(a) * 0.6); c.rotation.y += dt * 3; });
    if (G.time > TS.portal.expires) { closePortal(); toast('O rasgo violeta se fechou.', 'turb'); }
  } else if (!TS.inside && G.time > TS.next && G.running) spawnPortal();

  for (const s of TS.shrines) { const c = s.mesh.getObjectByName('crystal'); c.rotation.y += dt; c.position.y = 2.7 + Math.sin(G.time * 2 + s.x) * 0.15; }
  if (!TS.inside) return;

  // registrar encontros (anônimos) e seus desfechos
  for (const e of TS.shadows) {
    if (e.state === 'chase' || e.state === 'windup') encounterFor(e);
    if (!e.alive && TS.encounters.has(e) && !TS.encounters.get(e).result) TS.encounters.get(e).result = 'a figura caiu';
  }
  if (!TS.lateSpawn && G.time - TS.enteredAt > 100) {
    TS.lateSpawn = true;
    const [x, z] = pick(SHRINE_POS);
    TS.shadows.push(spawnEnemy('sombraArco', TURB.x + x, TURB.z + z, { leash: 70, patrol: SHRINE_POS.map(([a, b]) => [TURB.x + a, TURB.z + b]) }));
    toast('Outra silhueta atravessou para a região.', 'turb');
  }
  const left = TS.collapseAt - G.time;
  for (const w of [60, 20]) if (left < w && !TS.warned[w]) { TS.warned[w] = true; sfx('horn'); toast(`A região começa a colapsar: ${w} segundos para alcançar uma saída.`, 'danger'); }
  if (left <= 0 && P.state !== 'dead') {
    for (const e of TS.shadows) if (TS.encounters.has(e) && !TS.encounters.get(e).result) TS.encounters.get(e).result = 'sem desfecho';
    P.hp = 0; P.state = 'down'; P.stateT = 4.4; P.downSrc = { name: 'Colapso da região' };
  }
}

// chamado quando o jogador é derrotado dentro da região
export function onTurbulentDeath(src) {
  for (const e of TS.shadows) {
    const enc = TS.encounters.get(e);
    if (enc && !enc.result && e.alive) enc.result = src && src.faction === 'shadow' ? 'terminou o encontro de pé' : 'sem desfecho';
  }
  leave(G.player, false);
}
