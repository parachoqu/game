// Resolução de dano, projéteis e abates (cap. 04: ameaça legível → resposta → custo).
import * as THREE from 'three';
import { G, emit, stat, angleDiff, randi } from '../state.js';
import { ITEMS } from '../config.js';
import { groundHeight } from '../engine/terrain.js';
import { burst, floatText, showBar, cancelTelegraph } from '../engine/fx.js';
import { charMat as mat } from '../engine/materials.js';
import { sfx } from '../engine/audio.js';
import { wear, receive } from './inventory.js';
import { moveEntity, testPoint } from './collide.js';
import { knockDown, stagger, dismount, cancelChannel } from './player.js';
import { createLootBag } from './zones.js';
import { toast } from '../ui/hud.js';
import { hitSide, deathFall } from '../engine/anim-select.js';

// Reação visual a um golpe (só o animador lê): lado de onde veio, no referencial da vítima, e o
// instante. Um golpe novo em menos de 0,35 s não reinicia a reação que acabou de começar.
function markHit(v, yaw, from) {
  if (v.hitAt != null && G.time - v.hitAt < 0.35) return;
  v.hitDir = hitSide(yaw, v.pos.x, v.pos.z, from);
  v.hitAt = G.time;
}

export const inCombat = () => G.time - G.player.lastCombat < 5;

export function hurtPlayer(dmg, src) {
  const P = G.player;
  if (P.state === 'down' || P.state === 'dead' || G.cinematic) return;
  if (P.invuln > 0) { floatText(P.pos.x, P.pos.y + 2.3, P.pos.z, 'esquiva', 'dodge'); return; }
  P.lastCombat = G.time;
  if (P.shieldHp > 0) {
    const abs = Math.min(P.shieldHp, dmg);
    P.shieldHp -= abs;
    dmg -= abs;
    floatText(P.pos.x, P.pos.y + 2.3, P.pos.z, `escudo (−${abs})`, 'block');
    sfx('block');
    if (dmg <= 0) return;
  }
  const d = dmg;
  if (P.mounted) { dismount(P, true); toast('Você foi derrubado da montaria.', 'warn'); stagger(P, 0.5); }
  cancelChannel(P, 'Interrompido');
  P.hp -= d;
  if (d > 0) markHit(P, P.yaw, src);
  wear(P.equip.armor, 1.2);
  if (d > 0) {
    sfx('hurt');
    burst(P.pos.x, P.pos.y + 1.3, P.pos.z, '#c8402e', 7, 4);
    floatText(P.pos.x, P.pos.y + 2.3, P.pos.z, '−' + d, 'hurt');
    emit('player-hurt', d);
  }
  if (P.hp <= 0) { P.hp = 0; knockDown(P, src); }
}

export function hurtEnemy(e, dmg, opts = {}) {
  if (!e.alive) return false;
  if (e.curseT > 0) dmg *= 1.35;
  dmg = Math.round(dmg);
  // lado do golpe medido antes do empurrão; dano contínuo (queimadura) não faz o corpo reagir
  if (!opts.dot) markHit(e, e.yaw, opts.from);
  if (e.hp - dmg <= 0) e.deathDir = deathFall(e.yaw, e.pos.x, e.pos.z, opts.from);
  e.hp -= dmg; e.hurtT = 0.18; showBar(e);
  floatText(e.pos.x, e.pos.y + (e.barY || 2.4), e.pos.z, String(dmg), opts.big ? 'crit' : 'dmg');
  burst(e.pos.x, e.pos.y + 1.1, e.pos.z, opts.burstColor || (e.faction === 'shadow' ? '#b48cff' : '#e8c070'), 6, 4);
  sfx(opts.sfx || 'hit');
  if (opts.from && opts.knock) {
    const a = Math.atan2(e.pos.x - opts.from.x, e.pos.z - opts.from.z);
    moveEntity(e, Math.sin(a) * opts.knock, Math.cos(a) * opts.knock, e.radius);
  }
  if (opts.slow) { e.slowT = Math.max(e.slowT || 0, opts.slow); floatText(e.pos.x, e.pos.y + 3, e.pos.z, 'frio', 'block'); }
  if (opts.root) { e.rootT = Math.max(e.rootT || 0, opts.root); floatText(e.pos.x, e.pos.y + 3, e.pos.z, 'enraizado', 'block'); }
  if (opts.burn) { e.burnT = Math.max(e.burnT || 0, opts.burn); }
  if (opts.curse) { e.curseT = Math.max(e.curseT || 0, opts.curse); floatText(e.pos.x, e.pos.y + 3, e.pos.z, 'marcado', 'block'); }
  if (opts.healPlayer) {
    const P = G.player;
    P.hp = Math.min(P.maxHp, P.hp + opts.healPlayer);
    floatText(P.pos.x, P.pos.y + 2.3, P.pos.z, '+' + opts.healPlayer, 'heal');
  }
  if (opts.stun) { e.stun = opts.stun; cancelTelegraph(e.tele); e.tele = null; e.state = 'stun'; floatText(e.pos.x, e.pos.y + 3, e.pos.z, 'atordoado', 'block'); }
  else if (e.state === 'windup' && e.cfg.faction === 'bandit' && dmg >= 20) { cancelTelegraph(e.tele); e.tele = null; e.state = 'recover'; e.t = 0; floatText(e.pos.x, e.pos.y + 3, e.pos.z, 'interrompido', 'block'); }
  if (e.state === 'idle' || e.state === 'return' || e.state === 'loot') { e.state = 'chase'; e.t = 0; }
  if (opts.byPlayer !== false) { G.player.lastCombat = G.time; wear(G.player.equip.weapon, 0.6); }
  if (e.hp <= 0) killEnemy(e, opts.byPlayer !== false);
  return true;
}

export function killEnemy(e, byPlayer) {
  e.alive = false; e.state = 'dead'; e.t = 0; e.hp = 0;
  cancelTelegraph(e.tele); e.tele = null;
  if (byPlayer) {
    stat('kill_' + e.type);
    stat('kills');
    const P = G.player;
    if (e.cfg.coins) { const c = randi(e.cfg.coins[0], e.cfg.coins[1]); P.coins += c; floatText(e.pos.x, e.pos.y + 3, e.pos.z, '+' + c + ' moedas', 'coin'); sfx('coin'); }
    if (e.cfg.drop) {
      for (const [id, [a, b]] of Object.entries(e.cfg.drop)) {
        const q = randi(a, b);
        const got = receive(P, id, q);
        if (got) toast(`+${got} ${ITEMS[id].name}`, 'item');
        if (got < q) createLootBag(e.pos.x, e.pos.z, [{ id, qty: q - got }], 'espólio');
      }
    }
  }
  if (e.carrying && e.carrying.length) {
    createLootBag(e.pos.x, e.pos.z, e.carrying, 'carga recuperável');
    toast('O saqueador deixou cair uma carga.', 'item');
    e.carrying = [];
  }
  emit('enemy-killed', { e, byPlayer });
}

// golpe em arco à frente (ou círculo se arc >= 2π)
export function meleeSweep(ox, oz, yaw, range, arc, dmg, opts = {}) {
  let n = 0;
  for (const e of G.enemies) {
    if (!e.alive || e.faction === 'guard') continue;
    const dx = e.pos.x - ox, dz = e.pos.z - oz, d = Math.hypot(dx, dz);
    if (d > range + e.radius) continue;
    if (Math.abs(e.pos.y - groundHeight(ox, oz)) > 3) continue;
    if (arc < 6.2 && d > 1.1 && Math.abs(angleDiff(Math.atan2(dx, dz), yaw)) > arc / 2) continue;
    hurtEnemy(e, dmg, { ...opts, from: { x: ox, z: oz } });
    n++;
  }
  return n;
}

// ---------- projéteis e magias ----------
const arrowGeo = new THREE.BoxGeometry(0.07, 0.07, 1.0);
const boltGeo = new THREE.BoxGeometry(0.09, 0.09, 0.65);
const spellGeo = new THREE.SphereGeometry(0.24, 8, 6);

const _fwdVec = new THREE.Vector3();
const _unitZ = new THREE.Vector3(0, 0, 1);

export function shoot(o) {
  let color = o.owner === 'player' ? '#f3e2b0' : o.owner === 'shadow' ? '#d9c4ff' : '#ff9a74';
  let geo = arrowGeo;
  if (o.kind === 'bolt') {
    geo = boltGeo;
    color = '#f0e6d2';
  } else if (o.kind === 'spell' || o.school) {
    geo = spellGeo;
    const colors = {
      ice: '#70d6ff', fire: '#ff6b35', nature: '#52b788', wind: '#90e0ef',
      curse: '#9b5de5', holy: '#ffd166', vital: '#a7c957',
    };
    color = colors[o.school] || o.color || '#9fe2f2';
  } else if (o.color) {
    color = o.color;
  }
  const m = new THREE.Mesh(geo, mat(color, { emissive: color, emissiveIntensity: 0.85 }));
  m.position.set(o.x, o.y, o.z);

  let dx, dy, dz;
  if (o.target) {
    const vx = o.target.x - o.x, vy = o.target.y - o.y, vz = o.target.z - o.z;
    const len = Math.hypot(vx, vy, vz) || 1;
    dx = vx / len; dy = vy / len; dz = vz / len;
  } else if (o.dx != null && o.dy != null && o.dz != null) {
    dx = o.dx; dy = o.dy; dz = o.dz;
  } else {
    const p = o.pitch || 0;
    const cp = Math.cos(p);
    dx = Math.sin(o.yaw) * cp;
    dy = Math.sin(p);
    dz = Math.cos(o.yaw) * cp;
  }

  _fwdVec.set(dx, dy, dz).normalize();
  m.quaternion.setFromUnitVectors(_unitZ, _fwdVec);
  G.scene.add(m);

  const ballistic = o.owner === 'player' && (o.kind === 'bolt' || geo === arrowGeo);

  G.projectiles.push({
    m, x: o.x, y: o.y, z: o.z, dx, dy, dz,
    speed: o.speed, dmg: o.dmg, owner: o.owner, pierce: !!o.pierce,
    left: o.range || 45, hit: new Set(), ox: o.x, oy: o.y, oz: o.z,
    school: o.school, color, kind: o.kind, ballistic,
  });
  sfx(o.school ? 'magic' : 'arrow');
}

// distância de um ponto ao segmento percorrido pela flecha neste quadro
function segDist(px, pz, ax, az, bx, bz) {
  const dx = bx - ax, dz = bz - az, l2 = dx * dx + dz * dz;
  const t = l2 ? Math.max(0, Math.min(1, ((px - ax) * dx + (pz - az) * dz) / l2)) : 0;
  return Math.hypot(ax + dx * t - px, az + dz * t - pz);
}

export function updateProjectiles(dt) {
  const P = G.player;
  for (let i = G.projectiles.length - 1; i >= 0; i--) {
    const p = G.projectiles[i];
    const step = p.speed * dt;
    const ax = p.x, ay = p.y, az = p.z;
    p.x += p.dx * step; p.y += p.dy * step; p.z += p.dz * step;
    p.left -= step;

    if (p.ballistic) {
      p.dy -= 9.8 * 0.22 * dt;
      const vlen = Math.hypot(p.dx, p.dy, p.dz) || 1;
      p.dx /= vlen; p.dy /= vlen; p.dz /= vlen;
      _fwdVec.set(p.dx, p.dy, p.dz);
      p.m.quaternion.setFromUnitVectors(_unitZ, _fwdVec);
    }
    p.m.position.set(p.x, p.y, p.z);

    const g = groundHeight(p.x, p.z);
    const hitGround = p.y <= g + 0.12;
    const hitWall = testPoint(p.x, p.z, 0.28);
    let dead = p.left <= 0 || hitGround || hitWall;

    if (!dead && p.owner === 'player') {
      for (const e of G.enemies) {
        if (!e.alive || e.faction === 'guard' || p.hit.has(e)) continue;
        const eBot = e.pos.y, eTop = e.pos.y + (e.barY || 2.4);
        const inY = p.y >= eBot - 0.25 && p.y <= eTop + 0.35;
        if (segDist(e.pos.x, e.pos.z, ax, az, p.x, p.z) < e.radius + 0.55 && inY) {
          p.hit.add(e);
          const isCrit = (p.y >= eBot + (eTop - eBot) * 0.68) || p.pierce;
          const opts = {
            from: { x: p.ox, z: p.oz },
            knock: p.pierce ? 1.4 : (p.school === 'wind' ? 2.5 : 0.4),
            big: isCrit || p.school === 'fire',
            burstColor: p.color,
            slow: p.school === 'ice' ? 3.0 : undefined,
            burn: p.school === 'fire' ? 3.0 : undefined,
            root: p.school === 'nature' ? 2.0 : undefined,
            curse: p.school === 'curse' ? 5.0 : undefined,
          };
          hurtEnemy(e, isCrit ? Math.round(p.dmg * 1.5) : p.dmg, opts);
          emit('hitmarker', { crit: isCrit });
          if (!p.pierce) { dead = true; break; }
        }
      }
    } else if (!dead && p.owner !== 'player') {
      const pBot = P.pos.y, pTop = P.pos.y + 1.85;
      const inY = p.y >= pBot - 0.2 && p.y <= pTop + 0.3;
      if (segDist(P.pos.x, P.pos.z, ax, az, p.x, p.z) < 0.85 && inY) {
        hurtPlayer(p.dmg, { x: p.x - p.dx * 3, z: p.z - p.dz * 3, name: p.school ? 'feitiço' : 'flecha' });
        dead = true;
      }
    }
    if (dead) {
      if ((hitGround || hitWall) && p.left > 0) burst(p.x, Math.max(g + 0.1, p.y), p.z, '#c8b698', 3, 2);
      G.scene.remove(p.m);
      G.projectiles.splice(i, 1);
    }
  }
}
