// Criaturas, saqueadores, figuras da Turbulenta e guardas: modelos, IA e grupos com reaparecimento.
import * as THREE from 'three';
import { G, rand, angleDiff } from '../state.js';
import { ENEMIES } from '../config.js';
import { groundHeight } from '../engine/terrain.js';
import { makeBeast, animateBeast, makeHumanoid, animateHumanoid, releaseCharacter } from '../engine/characters.js';
import { telegraph, cancelTelegraph, removeBar } from '../engine/fx.js';
import { sfx } from '../engine/audio.js';
import { moveEntity } from './collide.js';
import { zoneAt } from './layout.js';
import { hurtPlayer, hurtEnemy, shoot } from './combat.js';
import { toast } from '../ui/hud.js';



function buildModel(type, cfg) {
  if (cfg.faction === 'beast') {
    const b = makeBeast({ scale: cfg.scale || 1, mane: type === 'garraLider', pale: type === 'garraPalida' });
    return { kind: 'beast', m: b };
  }
  let h;
  // tons por facção sobre o mesmo modelo: guarda azulado, saqueador avermelhado, sombra em silhueta
  if (cfg.faction === 'shadow') h = makeHumanoid({ shadow: true });
  else if (cfg.faction === 'guard') h = makeHumanoid({ guard: true, tint: '#b9c8e6' });
  else h = makeHumanoid({ bandit: true, tint: '#d2ae9c' });
  h.setWeapon(cfg.weapon || 'espada');
  return { kind: 'human', m: h };
}

export function spawnEnemy(type, x, z, opts = {}) {
  const cfg = type === 'guarda'
    ? { name: 'Guarda', faction: 'guard', hp: 200, speed: 6.2, dmg: 14, range: 2.4, weapon: 'espada' }
    : ENEMIES[type];
  const model = buildModel(type, cfg);
  const e = {
    type, cfg, faction: cfg.faction, alive: true, state: 'idle', t: rand(0, 2),
    pos: new THREE.Vector3(x, groundHeight(x, z), z), yaw: rand(0, Math.PI * 2),
    hp: cfg.hp, maxHp: cfg.hp, home: { x, z }, leash: opts.leash || 42, radius: cfg.faction === 'beast' ? 0.7 * (cfg.scale || 1) : 0.45,
    model, tele: null, stun: 0, carrying: [], group: opts.group || null, barY: cfg.faction === 'beast' ? 1.75 * (cfg.scale || 1) : 2.3,
    hurtT: 0, wander: null, speedK: 0, cd: 0, post: opts.post || null, patrol: opts.patrol || null, pi: 0,
  };
  G.scene.add(model.m.root);
  G.enemies.push(e);
  return e;
}

export function removeEnemy(e) {
  G.scene.remove(e.model.m.root);
  releaseCharacter(e.model.m.root);
  cancelTelegraph(e.tele);
  removeBar(e);
  const i = G.enemies.indexOf(e); if (i >= 0) G.enemies.splice(i, 1);
}

// ---------- grupos com reaparecimento ----------
export const groups = [];
export function defineGroup(id, center, members, opts = {}) {
  const g = { id, center, members, respawn: opts.respawn ?? 70, leash: opts.leash, active: opts.active || (() => true), list: [], deadAt: null, patrol: opts.patrol };
  groups.push(g);
  spawnGroup(g);
  return g;
}
function spawnGroup(g) {
  g.list = g.members.map(([type, dx, dz]) => spawnEnemy(type, g.center.x + dx, g.center.z + dz, { group: g, leash: g.leash, patrol: g.patrol }));
  g.deadAt = null;
}
export function updateGroups() {
  const P = G.player;
  for (const g of groups) {
    if (g.list.some((e) => e.alive)) continue;
    if (g.deadAt === null) g.deadAt = G.time;
    if (!g.active()) continue;
    if (G.time - g.deadAt > g.respawn && Math.hypot(P.pos.x - g.center.x, P.pos.z - g.center.z) > 55) spawnGroup(g);
  }
}

// ---------- IA ----------
function canTarget(e) {
  const P = G.player;
  if (P.state === 'down' || P.state === 'dead' || G.cinematic || G.uiOpen === 'death') return false;
  if (e.faction === 'bandit' && P.zone === 'protegida') return false;
  return true;
}
function stepToward(e, tx, tz, speed, dt) {
  if (e.rootT > 0) { e.rootT = Math.max(0, e.rootT - dt); return 0; }
  if (e.slowT > 0) { e.slowT = Math.max(0, e.slowT - dt); speed *= 0.52; }
  const dx = tx - e.pos.x, dz = tz - e.pos.z, d = Math.hypot(dx, dz);
  if (d < 0.05) return 0;
  const s = Math.min(d, speed * dt);
  const nx = e.pos.x + dx / d * s, nz = e.pos.z + dz / d * s;
  if (e.faction === 'bandit' && zoneAt(nx, nz) === 'protegida') return 0;   // não entram em área protegida
  moveEntity(e, dx / d * s, dz / d * s, e.radius);
  e.yaw = Math.atan2(dx, dz);
  return s / dt;
}
function nearestHostile(from, r) {
  let best = null, bd = r;
  for (const o of G.enemies) {
    if (!o.alive || o.faction === 'guard' || o.faction === 'shadow') continue;
    const d = Math.hypot(o.pos.x - from.pos.x, o.pos.z - from.pos.z);
    if (d < bd) { bd = d; best = o; }
  }
  return best;
}

function updateGuard(e, dt) {
  const tgt = nearestHostile(e, 18);
  let v = 0;
  e.cd -= dt;
  if (tgt) {
    const d = Math.hypot(tgt.pos.x - e.pos.x, tgt.pos.z - e.pos.z);
    if (d > 2.2) v = stepToward(e, tgt.pos.x, tgt.pos.z, e.cfg.speed, dt);
    else {
      e.yaw = Math.atan2(tgt.pos.x - e.pos.x, tgt.pos.z - e.pos.z);
      if (e.cd <= 0) { e.cd = 1.3; e.atk = 0; sfx('swing'); hurtEnemy(tgt, e.cfg.dmg, { byPlayer: false, from: e.pos, knock: 0.5 }); }
    }
  } else if (e.post) {
    const d = Math.hypot(e.post.x - e.pos.x, e.post.z - e.pos.z);
    if (d > 1) v = stepToward(e, e.post.x, e.post.z, e.cfg.speed * 0.6, dt);
    else e.yaw = e.post.yaw ?? e.yaw;
  }
  if (e.atk !== undefined && e.atk >= 0) { e.atk += dt * 2.2; if (e.atk > 1) e.atk = -1; }
  return v;
}

function updateHostile(e, dt) {
  const P = G.player, cfg = e.cfg;
  const dP = Math.hypot(P.pos.x - e.pos.x, P.pos.z - e.pos.z);
  const dHome = Math.hypot(e.home.x - e.pos.x, e.home.z - e.pos.z);
  let v = 0;
  e.t += dt;
  switch (e.state) {
    case 'idle': {
      if (e.patrol) {
        const p = e.patrol[e.pi % e.patrol.length];
        if (Math.hypot(p[0] - e.pos.x, p[1] - e.pos.z) < 2) e.pi++;
        v = stepToward(e, p[0], p[1], cfg.speed * 0.45, dt);
      } else {
        if (!e.wander || e.t > 6) { e.wander = { x: e.home.x + rand(-8, 8), z: e.home.z + rand(-8, 8) }; e.t = 0; }
        if (e.t < 3.5) v = stepToward(e, e.wander.x, e.wander.z, cfg.speed * 0.35, dt);
      }
      if (dP < cfg.aggro && canTarget(e)) { e.state = 'chase'; e.t = 0; }
      else if (e.faction === 'bandit' && !e.carrying.length) {
        const bag = G.lootBags.find((b) => !b.claimed && b.items.length && zoneAt(b.x, b.z) !== 'protegida' && Math.hypot(b.x - e.pos.x, b.z - e.pos.z) < 75);
        if (bag) { bag.claimed = e; e.targetBag = bag; e.state = 'loot'; }
      }
      break;
    }
    case 'loot': {
      const b = e.targetBag;
      if (!b || !G.lootBags.includes(b) || !b.items.length) { e.state = 'idle'; if (b) b.claimed = null; break; }
      v = stepToward(e, b.x, b.z, cfg.speed * 0.8, dt);
      if (Math.hypot(b.x - e.pos.x, b.z - e.pos.z) < 1.6) {
        e.carrying = b.items.splice(0);
        b.remove();
        if (dP < 70) toast('Um saqueador recolheu a carga deixada no chão. Ela ainda pode ser recuperada.', 'warn');
        e.state = 'idle'; e.targetBag = null;
      }
      if (dP < cfg.aggro * 0.8 && canTarget(e)) { e.state = 'chase'; if (b) b.claimed = null; }
      break;
    }
    case 'chase': {
      if (!canTarget(e) || dHome > e.leash || dP > cfg.aggro * 2.2) { e.state = 'return'; break; }
      if (cfg.ranged) {
        if (dP < 5.5) v = stepToward(e, e.pos.x - (P.pos.x - e.pos.x), e.pos.z - (P.pos.z - e.pos.z), cfg.speed * 0.8, dt);
        else if (dP > cfg.range * 0.85) v = stepToward(e, P.pos.x, P.pos.z, cfg.speed, dt);
        else if (e.t > 0.4) startWindup(e, dP);
        else e.yaw = Math.atan2(P.pos.x - e.pos.x, P.pos.z - e.pos.z);
      } else if (dP > cfg.range * 0.8 + 0.3) v = stepToward(e, P.pos.x, P.pos.z, cfg.speed, dt);
      else startWindup(e, dP);
      break;
    }
    case 'windup': {
      if (e.t >= cfg.windup) strike(e);
      break;
    }
    case 'recover': {
      if (e.atk >= 0) { e.atk += dt * 3; if (e.atk > 1) e.atk = -1; }
      if (e.t > cfg.cd * 0.7) { e.state = 'chase'; e.t = 0; }
      break;
    }
    case 'stun': {
      e.stun -= dt;
      if (e.stun <= 0) { e.state = 'chase'; e.t = 0; }
      break;
    }
    case 'return': {
      v = stepToward(e, e.home.x, e.home.z, cfg.speed * 1.1, dt);
      e.hp = Math.min(e.maxHp, e.hp + e.maxHp * 0.25 * dt);
      if (dHome < 2.5) { e.state = 'idle'; e.t = 0; removeBar(e); }
      break;
    }
  }
  return v;
}

function startWindup(e, dP) {
  const P = G.player, cfg = e.cfg;
  e.state = 'windup'; e.t = 0;
  e.lockYaw = Math.atan2(P.pos.x - e.pos.x, P.pos.z - e.pos.z);
  e.yaw = e.lockYaw;
  e.origin = { x: e.pos.x, z: e.pos.z };
  const shape = cfg.shape === 'circle' ? 'circle' : cfg.ranged ? 'line' : 'sector';
  e.tele = telegraph({ x: e.pos.x, z: e.pos.z, shape, radius: cfg.range + 0.3, arc: cfg.arc || 1.4, length: Math.min(cfg.range + 4, dP + 6), width: 1.4, facing: e.lockYaw, duration: cfg.windup, color: '#ff5a3c' });
  if (dP < 26) sfx('warn');
}

function strike(e) {
  const P = G.player, cfg = e.cfg;
  e.tele = null;
  e.state = 'recover'; e.t = 0; e.atk = 0;
  if (cfg.ranged) {
    shoot({ x: e.pos.x + Math.sin(e.lockYaw) * 0.8, y: e.pos.y + 1.45, z: e.pos.z + Math.cos(e.lockYaw) * 0.8, yaw: e.lockYaw, speed: 30, dmg: cfg.dmg, owner: e.faction === 'shadow' ? 'shadow' : 'enemy', range: cfg.range + 8 });
    return;
  }
  sfx('swing');
  const dx = P.pos.x - e.origin.x, dz = P.pos.z - e.origin.z, d = Math.hypot(dx, dz);
  let inside;
  if (cfg.shape === 'circle') inside = d <= cfg.range + 0.4;
  else inside = d <= cfg.range + 0.6 && (d < 0.8 || Math.abs(angleDiff(Math.atan2(dx, dz), e.lockYaw)) <= (cfg.arc || 1.4) / 2 + 0.15);
  if (inside && Math.abs(P.pos.y - e.pos.y) < 2.5) hurtPlayer(cfg.dmg, { x: e.origin.x, z: e.origin.z, name: cfg.name, faction: e.faction });
}

function separate() {
  const list = G.enemies;
  for (let i = 0; i < list.length; i++) {
    const a = list[i]; if (!a.alive) continue;
    for (let j = i + 1; j < list.length; j++) {
      const b = list[j]; if (!b.alive) continue;
      const dx = b.pos.x - a.pos.x, dz = b.pos.z - a.pos.z, d = Math.hypot(dx, dz), m = a.radius + b.radius;
      if (d < m && d > 1e-3) { const k = (m - d) / 2 / d; a.pos.x -= dx * k; a.pos.z -= dz * k; b.pos.x += dx * k; b.pos.z += dz * k; }
    }
  }
}

export function updateEnemies(dt) {
  const P = G.player;
  for (let i = G.enemies.length - 1; i >= 0; i--) {
    const e = G.enemies[i];
    const dP = Math.hypot(P.pos.x - e.pos.x, P.pos.z - e.pos.z);
    if (!e.alive) {
      e.t += dt;
      animate(e, 0, dt);
      if (e.t > 5) { e.model.m.root.position.y -= dt * 0.8; }
      if (e.t > 7) removeEnemy(e);
      continue;
    }
    if (dP > 110 && e.state === 'idle') { e.model.m.root.visible = false; continue; }
    e.model.m.root.visible = !e.hidden;
    if (e.burnT > 0) {
      e.burnT = Math.max(0, e.burnT - dt);
      e.burnTick = (e.burnTick || 0) + dt;
      if (e.burnTick >= 0.5) {
        e.burnTick = 0;
        hurtEnemy(e, 5, { byPlayer: true, burstColor: '#ff6b35' });
      }
    }
    if (e.curseT > 0) e.curseT = Math.max(0, e.curseT - dt);
    const v = e.faction === 'guard' ? updateGuard(e, dt) : updateHostile(e, dt);
    e.hurtT = Math.max(0, e.hurtT - dt);
    e.pos.y = groundHeight(e.pos.x, e.pos.z);
    animate(e, v, dt);
  }
  separate();
}

function animate(e, v, dt) {
  const r = e.model.m.root;
  r.position.copy(e.pos);
  r.rotation.y = e.yaw;
  const k = 1 + e.hurtT * 0.5; r.scale.setScalar(k);
  const speed = Math.min(1, v / Math.max(1, e.cfg.speed));
  if (e.model.kind === 'beast') {
    animateBeast(e.model.m, {
      dt, mps: v, down: !e.alive, stun: e.state === 'stun',
      windup: e.state === 'windup' ? Math.min(1, e.t / e.cfg.windup) : -1,
      attack: e.state === 'recover' && e.atk >= 0 ? e.atk : -1,
    });
  } else {
    const ranged = e.cfg.ranged || e.cfg.weapon === 'arco';
    let attack = -1, kind = 'swing';
    if (e.state === 'windup') { attack = ranged ? Math.min(1, e.t / e.cfg.windup) : Math.min(0.34, e.t / e.cfg.windup * 0.34); kind = ranged ? 'bow' : 'swing'; }
    else if (e.atk >= 0) { attack = ranged ? -1 : 0.35 + e.atk * 0.65; }
    animateHumanoid(e.model.m, { dt, mps: v, down: !e.alive, attack, kind, channel: false, dodge: -1 });
  }
}
