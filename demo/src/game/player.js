// Personagem do jogador: movimento, mira, ações de combate, canalizações, derrubada e câmera.
import * as THREE from 'three';
import { G, emit, clamp, lerp, angleDiff, stat } from '../state.js';
import { input, down, hit, releaseLook } from '../engine/input.js';
import { groundHeight, waterAt } from '../engine/terrain.js';
import { makeHumanoid, animateHumanoid, standingDodgeFits } from '../engine/characters.js';
import { toLocal, dodgeClip, DODGE_FACING } from '../engine/anim-select.js';
import { OCC } from '../engine/props.js';
import { telegraph, floatText, burst } from '../engine/fx.js';
import { sfx } from '../engine/audio.js';
import { ITEMS, WEAPONS, SKILLS, ORIGINS, STARTS } from '../config.js';
import { moveEntity, testPoint } from './collide.js';
import { zoneAt, placeAt, isTurbulentSpace } from './layout.js';
import { meleeSweep, shoot, hurtEnemy, inCombat } from './combat.js';
import { loadState, armorSpeed, removeFrom, count, makeGear, addTo, wear } from './inventory.js';
import { handleDeath, SHELTER } from './zones.js';
import { tally } from './book.js';
import { toast } from '../ui/hud.js';

// a pé: anda por padrão; Shift (com WASD) corre. Aproximar-se de um inimigo clicado é sempre correndo.
const WALK = 3, RUN = 7.2;
// Ctrl segurado agacha: passo curto, sem corrida
const CROUCH = 0.45;
// Espaço pula; Shift + Espaço é o salto impulsionado (mais alto, com impulso à frente)
const GRAVITY = 20;
const JUMP = { vy: 5.2, vigor: 8 };
const BOOST = { vy: 7.0, vigor: 20, speed: RUN * 1.25 };
const AIR_CONTROL = 0.35;

// ---------- preferências de câmera (menu de pausa, salvas no navegador) ----------
const SENS = { baixa: 0.7, media: 1, alta: 1.5 };
const CAM_KEY = 'projeto-game-camera';
export const CAM_PREFS = { sens: 'media', invert: false };
try {
  const v = JSON.parse(localStorage.getItem(CAM_KEY) || 'null');
  if (v && SENS[v.sens]) { CAM_PREFS.sens = v.sens; CAM_PREFS.invert = !!v.invert; }
} catch { /* sem armazenamento: valores padrão */ }
export function setCamPref(key, value) {
  CAM_PREFS[key] = value;
  try { localStorage.setItem(CAM_KEY, JSON.stringify(CAM_PREFS)); } catch { /* ignora */ }
}
export const camSens = () => SENS[CAM_PREFS.sens] || 1;

export function addCamRecoil(pitchKick = 0.03, yawKick = 0) {
  const cam = G.cam;
  if (!cam) return;
  cam.recoilPitch = (cam.recoilPitch || 0) - pitchKick;
  cam.recoilYaw = (cam.recoilYaw || 0) + yawKick;
}

export function addCamShake(amount = 0.3) {
  const cam = G.cam;
  if (!cam) return;
  cam.shake = Math.min(1.2, (cam.shake || 0) + amount);
}

// Tab + 1/2/3: distância da câmera (o mesmo alvo do zoom suave da roda)
const CAM_PRESETS = { 1: [20, 'Câmera distante'], 2: [13, 'Câmera média'], 3: [7.5, 'Câmera próxima'] };
export function setCameraPreset(n) {
  const p = CAM_PRESETS[n], cam = G.cam;
  if (!p || !cam) return;
  cam.targetDist = p[0];
  toast(p[1], 'info', 1400);
  sfx('ui');
}

export function toggleShoulder() {
  const cam = G.cam;
  if (!cam) return;
  cam.shoulder = (cam.shoulder === -1) ? 1 : -1;
  toast(cam.shoulder > 0 ? 'Câmera: ombro direito' : 'Câmera: ombro esquerdo', 'info', 1600);
  sfx('ui');
}

export function createPlayer(profile) {
  const o = ORIGINS[profile.origin];
  const model = makeHumanoid({ model: profile.model || 'paladina', race: profile.origin, skin: o.skin, cloth: o.cloth, trim: o.trim, hair: profile.origin === 'anao' ? '#8a5a32' : '#3a2a20' });
  G.scene.add(model.root);
  const start = STARTS[profile.start] || STARTS.espadachim;
  const P = {
    name: profile.name, origin: profile.origin, start: profile.start,
    pos: new THREE.Vector3(SHELTER.x, groundHeight(SHELTER.x, SHELTER.z), SHELTER.z), yaw: Math.PI, aimYaw: Math.PI,
    aimPitch: 0, aimPoint: new THREE.Vector3(), aimEnemy: null,
    hp: 100, maxHp: 100, vigor: 100, maxVigor: 100, vigorDelay: 0,
    coins: 40, trainings: new Set([profile.start]),
    inv: [], saddle: [], storage: [],
    equip: { weapon: makeGear(start.weapon, 82), armor: null, bag: null, mount: null },
    state: 'free', stateT: 0, invuln: 0, lastCombat: -99, mounted: false, cmd: null,
    aiming: false, aimT: 0,
    cd: { Q: 0, E: 0, pot: 0 }, channel: null, zone: 'protegida', place: placeAt(SHELTER.x, SHELTER.z),
    model, act: null, speedNow: 0,
    metamorphT: 0, shieldHp: 0, shieldT: 0, auraVitalT: 0,
  };
  addTo(P.inv, 'pocao', 2);
  if (profile.start === 'artesao') { addTo(P.inv, 'minerio', 4); addTo(P.inv, 'madeira', 2); }
  model.setWeapon(ITEMS[start.weapon].family);
  return P;
}

// ---------- utilidades de estado ----------
export function weaponFamily(P) {
  const w = P.equip.weapon;
  return w && w.cond > 0 ? ITEMS[w.id].family : 'punhos';
}
export function stagger(P, t) { if (P.state === 'down' || P.state === 'dead') return; P.state = 'stagger'; P.stateT = 0; P.staggerT = t; }
export function startChannel(P, label, dur, onDone, opts = {}) {
  P.channel = { label, dur, t: 0, onDone, interruptible: opts.interruptible !== false, anim: opts.anim ?? true };
  P.state = 'channel'; P.cmd = null;   // um clique antigo não deve cancelar a canalização
}
export function cancelChannel(P, reason) {
  if (!P.channel) return;
  const c = P.channel; P.channel = null;
  if (P.state === 'channel') P.state = 'free';
  if (reason) toast(`${c.label}: ${reason.toLowerCase()}.`, 'warn');
  if (c.onCancel) c.onCancel();
}
export function knockDown(P, src) {
  if (P.state === 'down' || P.state === 'dead') return;
  cancelChannel(P);
  if (P.mounted) dismount(P, true);
  P.state = 'down'; P.stateT = 0; P.downSrc = src; P.cmd = null;
  sfx('death');
  toast('Derrubado.', 'danger');
  emit('player-down');
}
export function dismount(P, forced) {
  if (!P.mounted) return;
  P.mounted = false;
  const m = G.mount;
  m.pos.copy(P.pos); m.yaw = P.yaw;
  const side = P.yaw + Math.PI / 2;
  moveEntity(P, Math.sin(side) * 2, Math.cos(side) * 2, 0.45);
  if (!forced) sfx('ui');
}
export function mountUp(P) {
  const m = G.mount;
  P.mounted = true;
  P.pos.copy(m.pos); P.yaw = m.yaw;
  sfx('ui');
  tally('montaria', 'historia', 'comercio', (n) => ({ title: 'Um cavalo de carga', text: `Seguiu viagem com a montaria de carga ${n > 1 ? `(${n} partidas registradas)` : 'pela primeira vez'}. Os alforjes acompanham o animal, não o abrigo.` }));
}

// ---------- mira em terceira pessoa (3D raycasting, terreno, inimigos e compensação) ----------
const _aimRay = new THREE.Raycaster();
const _aimNdc = new THREE.Vector2();
const _aimTarget = new THREE.Vector3();
const _vEnemy = new THREE.Vector3();
const _vRayDiff = new THREE.Vector3();
const _vRayPt = new THREE.Vector3();

function updateAim(P) {
  _aimNdc.set(input.mouse.x / innerWidth * 2 - 1, -(input.mouse.y / innerHeight) * 2 + 1);
  _aimRay.setFromCamera(_aimNdc, G.camera);
  const rOrg = _aimRay.ray.origin;
  const rDir = _aimRay.ray.direction;

  // 1. Raycast esférico/cilíndrico contra inimigos visíveis
  let bestEnemy = null;
  let bestEnemyDist = 70;
  const bestEnemyPt = new THREE.Vector3();

  for (const e of G.enemies) {
    if (!e.alive || e.faction === 'guard' || !e.model?.m?.root?.visible) continue;
    const barY = e.barY || 2.4;
    _vEnemy.set(e.pos.x, e.pos.y + barY * 0.45, e.pos.z);
    _vRayDiff.subVectors(_vEnemy, rOrg);
    const t = rDir.dot(_vRayDiff);
    if (t > 0.8 && t < bestEnemyDist) {
      _vRayPt.copy(rOrg).addScaledVector(rDir, t);
      const d = _vRayPt.distanceTo(_vEnemy);
      const hitR = e.radius + 0.65;
      if (d < hitR) {
        bestEnemy = e;
        bestEnemyDist = t;
        bestEnemyPt.copy(_vEnemy);
      }
    }
  }

  // 2. Raycast contínuo contra relevo do terreno
  let terrainHitDist = 70;
  let hasTerrainHit = false;
  let prevD = 1.0;
  for (let d = 2.0; d <= 70; d += 2.2) {
    const px = rOrg.x + rDir.x * d;
    const py = rOrg.y + rDir.y * d;
    const pz = rOrg.z + rDir.z * d;
    const gh = groundHeight(px, pz);
    if (py <= gh) {
      let l = prevD, r = d;
      for (let b = 0; b < 5; b++) {
        const m = (l + r) * 0.5;
        const my = rOrg.y + rDir.y * m;
        if (my <= groundHeight(rOrg.x + rDir.x * m, rOrg.z + rDir.z * m)) r = m;
        else l = m;
      }
      terrainHitDist = (l + r) * 0.5;
      hasTerrainHit = true;
      break;
    }
    prevD = d;
  }

  // Determina ponto 3D final de pontaria
  if (bestEnemy && bestEnemyDist <= terrainHitDist + 0.5) {
    _aimTarget.copy(bestEnemyPt);
    P.aimEnemy = bestEnemy;
  } else if (hasTerrainHit) {
    _aimTarget.copy(rOrg).addScaledVector(rDir, terrainHitDist);
    P.aimEnemy = null;
  } else {
    _aimTarget.copy(rOrg).addScaledVector(rDir, 65);
    P.aimEnemy = null;
  }

  if (!P.aimPoint) P.aimPoint = new THREE.Vector3();
  P.aimPoint.copy(_aimTarget);

  const dx = _aimTarget.x - P.pos.x;
  const dz = _aimTarget.z - P.pos.z;
  const horizDist = Math.hypot(dx, dz);

  // Alinhamento horizontal: se muito próximo do personagem, atenua paralaxe da câmera lateral
  const camFwdYaw = Math.atan2(-Math.sin(G.cam.yaw), -Math.cos(G.cam.yaw));
  if (horizDist < 2.4) {
    const blend = Math.max(0, (horizDist - 0.6) / 1.8);
    const targetYaw = Math.atan2(dx, dz);
    P.aimYaw = lerpAngle(camFwdYaw, targetYaw, blend);
  } else {
    P.aimYaw = Math.atan2(dx, dz);
  }

  // Pitch do peito do personagem até o alvo 3D
  const dy = _aimTarget.y - (P.pos.y + 1.45);
  P.aimPitch = clamp(Math.atan2(dy, Math.max(1.2, horizDist)), -0.85, 0.85);
  P.aimDist = Math.hypot(dx, dy, dz);
}

// ---------- ações ----------
// leve assistência de mira para disparos: alvo dentro de ~6° da mira
function assistYaw(P, yaw, range = 40, tol = 0.11) {
  if (P.aimEnemy && P.aimEnemy.alive && P.aimEnemy.faction !== 'guard') {
    const dx = P.aimEnemy.pos.x - P.pos.x, dz = P.aimEnemy.pos.z - P.pos.z;
    return Math.atan2(dx, dz);
  }
  let best = null, bd = tol;
  for (const e of G.enemies) {
    if (!e.alive || e.faction === 'guard') continue;
    const dx = e.pos.x - P.pos.x, dz = e.pos.z - P.pos.z, d = Math.hypot(dx, dz);
    if (d > range || d < 1) continue;
    const a = Math.abs(angleDiff(Math.atan2(dx, dz), yaw));
    if (a < bd) { bd = a; best = Math.atan2(dx, dz); }
  }
  return best ?? yaw;
}
function basicAttack(P) {
  const fam = weaponFamily(P);
  const b = WEAPONS[fam]?.basic || WEAPONS.punhos.basic;
  if (P.equip.weapon && P.equip.weapon.cond <= 0 && !P.warnedBroken) { toast('Arma inutilizada: lutando com os punhos. Repare na bancada.', 'warn'); P.warnedBroken = true; }
  P.yaw = P.aimYaw;
  P.state = 'attack'; P.stateT = 0;
  let kind = 'swing';
  if (b.type === 'ranged') kind = fam === 'besta' ? 'crossbow' : 'bow';
  else if (b.type === 'spell') kind = 'cast';
  else if (fam === 'manoplas') kind = 'rapid';
  else if (fam === 'machado' || fam === 'maca') kind = 'heavy';
  else if (fam === 'lanca') kind = 'thrust';
  else if (fam === 'foice' || fam === 'adaga') kind = 'slash';
  P.act = { kind, windup: b.windup, recover: b.recover, done: false, fam, b, aimed: !!P.aiming };
}
function resolveBasic(P) {
  const { b, fam } = P.act;
  const tol = P.act.aimed ? 0.04 : 0.11;   // mirando, quase não há imã: a pontaria é do jogador
  if (b.type === 'ranged') {
    const yaw = assistYaw(P, P.yaw, 40, tol);
    shoot({
      x: P.pos.x + Math.sin(yaw) * 0.8, y: P.pos.y + 1.45, z: P.pos.z + Math.cos(yaw) * 0.8,
      yaw, speed: b.speed, dmg: b.dmg, owner: 'player', range: 44,
      kind: fam === 'besta' ? 'bolt' : 'arrow',
      target: P.aimPoint,
    });
    addCamRecoil(fam === 'besta' ? 0.038 : 0.026, (Math.random() - 0.5) * 0.012);
  } else if (b.type === 'spell') {
    const yaw = assistYaw(P, P.yaw, 40, tol);
    shoot({
      x: P.pos.x + Math.sin(yaw) * 0.8, y: P.pos.y + 1.45, z: P.pos.z + Math.cos(yaw) * 0.8,
      yaw, speed: b.speed, dmg: b.dmg, owner: 'player', range: 40,
      kind: 'spell', school: b.school,
      target: P.aimPoint,
    });
    addCamRecoil(0.028, (Math.random() - 0.5) * 0.01);
  } else {
    sfx(fam === 'maca' || fam === 'martelo' ? 'slam' : (fam === 'metamorfose' ? 'roar' : 'swing'));
    const knock = fam === 'maca' ? 0.9 : (fam === 'machado' ? 0.8 : (fam === 'martelo' ? 0.9 : 0.4));
    meleeSweep(P.pos.x, P.pos.z, P.yaw, b.range, b.arc, b.dmg, { knock });
  }
}

const TEAL = '#5fd6c4';
const ICE_COL = '#70d6ff';
const FIRE_COL = '#ff6b35';
const HOLY_COL = '#ffd166';

function useSkill(P, slot) {
  const fam = weaponFamily(P);
  const id = WEAPONS[fam]?.skills[slot === 'Q' ? 0 : 1];
  if (!id) { toast('Esta arma não oferece técnica neste espaço.', 'info'); return; }
  const s = SKILLS[id];
  if (!s) return;
  if (P.cd[slot] > 0) { sfx('deny'); return; }
  if (P.vigor < s.cost) { toast('Vigor insuficiente.', 'warn'); sfx('deny'); return; }
  P.vigor -= s.cost; P.vigorDelay = 0.7; P.cd[slot] = s.cd;
  P.yaw = P.aimYaw;
  P.state = 'skill'; P.stateT = 0; P.act = { id, hitSet: new Set(), done: false, subStep: 0 };
  stat('skill_' + id);

  if (id === 'giro' || id === 'ceifaCircular') {
    P.act.tele = telegraph({ x: P.pos.x, z: P.pos.z, shape: 'circle', radius: 3.4, duration: 0.35, color: TEAL });
  } else if (id === 'ondaChoque') {
    P.act.tele = telegraph({ x: P.pos.x, z: P.pos.z, shape: 'circle', radius: 3.8, duration: 0.45, color: '#e3b24c' });
  } else if (id === 'prisaoGelo') {
    P.act.tele = telegraph({ x: P.pos.x + Math.sin(P.yaw) * 4, z: P.pos.z + Math.cos(P.yaw) * 4, shape: 'circle', radius: 3.6, duration: 0.5, color: ICE_COL });
  } else if (id === 'erupcaoFogo') {
    P.act.tele = telegraph({ x: P.pos.x + Math.sin(P.yaw) * 4, z: P.pos.z + Math.cos(P.yaw) * 4, shape: 'circle', radius: 3.6, duration: 0.5, color: FIRE_COL });
  } else if (id === 'rugidoFera') {
    P.act.tele = telegraph({ x: P.pos.x, z: P.pos.z, shape: 'circle', radius: 5.0, duration: 0.35, color: '#d4a373' });
  } else if (id === 'purificacaoSagrada') {
    P.act.tele = telegraph({ x: P.pos.x, z: P.pos.z, shape: 'circle', radius: 4.8, duration: 0.4, color: HOLY_COL });
  } else if (id === 'tiroCarregado' || id === 'tiroBesta') {
    P.act.tele = telegraph({ x: P.pos.x, z: P.pos.z, shape: 'line', length: 38, width: 1.2, facing: P.yaw, duration: 0.6, color: TEAL });
  } else if (id === 'estocadaLonga') {
    P.act.tele = telegraph({ x: P.pos.x, z: P.pos.z, shape: 'line', length: 4.5, width: 1.2, facing: P.yaw, duration: 0.3, color: '#e3b24c' });
  } else if (id === 'reparoCampo') {
    P.state = 'free';
    startChannel(P, 'Reparo de campo', 1.5, () => {
      wear(P.equip.weapon, -25); wear(P.equip.armor, -25);
      for (const g of [P.equip.weapon, P.equip.armor]) if (g) g.cond = Math.min(100, g.cond);
      toast('Condição da arma e da armadura +25%.', 'item'); sfx('craft');
    });
  }
}

function updateSkill(P, dt) {
  const a = P.act, t = P.stateT;
  switch (a.id) {
    case 'investida': {
      if (t > 0.1 && t < 0.38) {
        P.invuln = 0.05;
        moveEntity(P, Math.sin(P.yaw) * 26 * dt, Math.cos(P.yaw) * 26 * dt, 0.45);
        for (const e of G.enemies) {
          if (!e.alive || e.faction === 'guard' || a.hitSet.has(e)) continue;
          if (Math.hypot(e.pos.x - P.pos.x, e.pos.z - P.pos.z) < 1.8 + e.radius) { a.hitSet.add(e); hurtEnemy(e, 22, { from: P.pos, knock: 2, big: true }); }
        }
      }
      if (t > 0.1 && !a.sw) { a.sw = true; sfx('dodge'); }
      if (t > 0.55) P.state = 'free';
      break;
    }
    case 'giro':
      if (t >= 0.35 && !a.done) { a.done = true; sfx('swing'); meleeSweep(P.pos.x, P.pos.z, P.yaw, 3.4, 7, 18, { knock: 2.5, big: true }); }
      if (t > 0.75) P.state = 'free';
      break;
    case 'tiroCarregado':
      if (t >= 0.6 && !a.done) {
        a.done = true; const y = assistYaw(P, P.yaw, 48);
        shoot({ x: P.pos.x + Math.sin(y) * 0.8, y: P.pos.y + 1.45, z: P.pos.z + Math.cos(y) * 0.8, yaw: y, speed: 62, dmg: 30, owner: 'player', pierce: true, range: 48, target: P.aimPoint });
        addCamRecoil(0.045, (Math.random() - 0.5) * 0.015);
        addCamShake(0.35);
      }
      if (t > 0.85) P.state = 'free';
      break;
    case 'recuo':
      if (t < 0.25) { P.invuln = 0.05; moveEntity(P, -Math.sin(P.yaw) * 20 * dt, -Math.cos(P.yaw) * 20 * dt, 0.45); }
      if (t >= 0.25 && !a.done) {
        a.done = true;
        shoot({ x: P.pos.x + Math.sin(P.yaw) * 0.8, y: P.pos.y + 1.45, z: P.pos.z + Math.cos(P.yaw) * 0.8, yaw: assistYaw(P, P.aimYaw), speed: 46, dmg: 12, owner: 'player', range: 40, target: P.aimPoint });
        addCamRecoil(0.03, (Math.random() - 0.5) * 0.01);
      }
      if (t > 0.5) P.state = 'free';
      break;
    case 'atordoar':
      if (t >= 0.45 && !a.done) { a.done = true; sfx('swing'); meleeSweep(P.pos.x, P.pos.z, P.yaw, 2.7, 1.9, 10, { stun: 1.5, knock: 0.6 }); }
      if (t > 0.85) P.state = 'free';
      break;

    // Manoplas
    case 'comboManopla': {
      if (t >= 0.12 && a.subStep === 0) { a.subStep = 1; sfx('swing'); meleeSweep(P.pos.x, P.pos.z, P.yaw, 2.3, 1.8, 9, { knock: 0.2 }); moveEntity(P, Math.sin(P.yaw) * 4 * dt, Math.cos(P.yaw) * 4 * dt, 0.45); }
      if (t >= 0.25 && a.subStep === 1) { a.subStep = 2; sfx('swing'); meleeSweep(P.pos.x, P.pos.z, P.yaw, 2.3, 1.8, 9, { knock: 0.3 }); moveEntity(P, Math.sin(P.yaw) * 4 * dt, Math.cos(P.yaw) * 4 * dt, 0.45); }
      if (t >= 0.38 && a.subStep === 2) { a.subStep = 3; a.done = true; sfx('hit'); meleeSweep(P.pos.x, P.pos.z, P.yaw, 2.4, 2.0, 14, { knock: 1.5, big: true }); }
      if (t > 0.55) P.state = 'free';
      break;
    }
    case 'avancoImpacto': {
      if (t > 0.08 && t < 0.28) {
        P.invuln = 0.05;
        moveEntity(P, Math.sin(P.yaw) * 24 * dt, Math.cos(P.yaw) * 24 * dt, 0.45);
        for (const e of G.enemies) {
          if (!e.alive || e.faction === 'guard' || a.hitSet.has(e)) continue;
          if (Math.hypot(e.pos.x - P.pos.x, e.pos.z - P.pos.z) < 1.7 + e.radius) {
            a.hitSet.add(e); hurtEnemy(e, 24, { from: P.pos, knock: 2.2, stun: 1.4, big: true }); sfx('slam');
          }
        }
      }
      if (t > 0.55) P.state = 'free';
      break;
    }

    // Maça
    case 'golpeEsmagador': {
      if (t >= 0.4 && !a.done) {
        a.done = true; sfx('slam');
        meleeSweep(P.pos.x, P.pos.z, P.yaw, 2.7, 1.8, 26, { slow: 3.5, knock: 1.4, big: true });
      }
      if (t > 0.7) P.state = 'free';
      break;
    }
    case 'ondaChoque': {
      if (t >= 0.45 && !a.done) {
        a.done = true; sfx('slam');
        burst(P.pos.x, P.pos.y + 0.3, P.pos.z, '#c49a45', 18, 5);
        meleeSweep(P.pos.x, P.pos.z, P.yaw, 3.8, 7.0, 18, { knock: 3.2, big: true });
      }
      if (t > 0.75) P.state = 'free';
      break;
    }

    // Machado
    case 'machadadaPesada': {
      if (t >= 0.45 && !a.done) {
        a.done = true; sfx('hit');
        meleeSweep(P.pos.x, P.pos.z, P.yaw, 2.8, 1.6, 34, { knock: 1.8, big: true });
      }
      if (t > 0.75) P.state = 'free';
      break;
    }
    case 'dilacerar': {
      if (t >= 0.28 && !a.done) {
        a.done = true; sfx('swing');
        meleeSweep(P.pos.x, P.pos.z, P.yaw, 2.8, 3.2, 18, { burn: 3.5, knock: 0.8 });
      }
      if (t > 0.6) P.state = 'free';
      break;
    }

    // Adaga
    case 'passoSombras': {
      if (!a.done) {
        a.done = true;
        P.invuln = 0.25;
        sfx('dodge');
        moveEntity(P, Math.sin(P.aimYaw) * 6.5, Math.cos(P.aimYaw) * 6.5, 0.45);
        burst(P.pos.x, P.pos.y + 1, P.pos.z, '#b0c4de', 10, 4);
        meleeSweep(P.pos.x, P.pos.z, P.aimYaw, 2.4, 2.0, 22, { knock: 0.5, big: true });
      }
      if (t > 0.45) P.state = 'free';
      break;
    }
    case 'golpePreciso': {
      if (t >= 0.22 && !a.done) {
        a.done = true; sfx('hit');
        meleeSweep(P.pos.x, P.pos.z, P.yaw, 2.3, 1.4, 28, { knock: 0.6, big: true });
      }
      if (t > 0.45) P.state = 'free';
      break;
    }

    // Lança
    case 'estocadaLonga': {
      if (t >= 0.3 && !a.done) {
        a.done = true; sfx('swing');
        meleeSweep(P.pos.x, P.pos.z, P.yaw, 4.4, 1.1, 24, { knock: 2.8, big: true });
      }
      if (t > 0.6) P.state = 'free';
      break;
    }
    case 'varrerDistancia': {
      if (t >= 0.32 && !a.done) {
        a.done = true; sfx('swing');
        meleeSweep(P.pos.x, P.pos.z, P.yaw, 3.6, 3.4, 17, { knock: 2.4 });
      }
      if (t > 0.65) P.state = 'free';
      break;
    }

    // Foice
    case 'ceifaCircular': {
      if (t >= 0.35 && !a.done) {
        a.done = true; sfx('swing');
        burst(P.pos.x, P.pos.y + 0.8, P.pos.z, '#8b008b', 12, 3);
        meleeSweep(P.pos.x, P.pos.z, P.yaw, 3.3, 7.0, 22, { knock: 1.0, big: true });
      }
      if (t > 0.75) P.state = 'free';
      break;
    }
    case 'puxaoFoice': {
      if (t >= 0.28 && !a.done) {
        a.done = true; sfx('swing');
        for (const e of G.enemies) {
          if (!e.alive || e.faction === 'guard') continue;
          const d = Math.hypot(e.pos.x - P.pos.x, e.pos.z - P.pos.z);
          if (d < 7.5 && Math.abs(angleDiff(Math.atan2(e.pos.x - P.pos.x, e.pos.z - P.pos.z), P.yaw)) < 0.7) {
            hurtEnemy(e, 16, { big: true });
            const toX = P.pos.x + Math.sin(P.yaw) * 1.8;
            const toZ = P.pos.z + Math.cos(P.yaw) * 1.8;
            moveEntity(e, toX - e.pos.x, toZ - e.pos.z, e.radius);
            break;
          }
        }
      }
      if (t > 0.6) P.state = 'free';
      break;
    }

    // Besta
    case 'tiroBesta': {
      if (t >= 0.38 && !a.done) {
        a.done = true;
        const y = assistYaw(P, P.yaw, 45);
        shoot({ x: P.pos.x + Math.sin(y) * 0.8, y: P.pos.y + 1.45, z: P.pos.z + Math.cos(y) * 0.8, yaw: y, speed: 68, dmg: 34, owner: 'player', pierce: true, kind: 'bolt', range: 45, target: P.aimPoint });
        addCamRecoil(0.045, (Math.random() - 0.5) * 0.012);
      }
      if (t > 0.7) P.state = 'free';
      break;
    }
    case 'disparoRepulsao': {
      if (t >= 0.25 && !a.done) {
        a.done = true;
        const y = assistYaw(P, P.yaw, 15);
        shoot({ x: P.pos.x + Math.sin(y) * 0.8, y: P.pos.y + 1.45, z: P.pos.z + Math.cos(y) * 0.8, yaw: y, speed: 55, dmg: 22, owner: 'player', kind: 'bolt', range: 18, target: P.aimPoint });
        meleeSweep(P.pos.x, P.pos.z, P.yaw, 3.2, 1.6, 15, { knock: 4.5, big: true });
        addCamRecoil(0.035, (Math.random() - 0.5) * 0.01);
      }
      if (t > 0.55) P.state = 'free';
      break;
    }

    // Gelo
    case 'setaGelo': {
      if (t >= 0.28 && !a.done) {
        a.done = true;
        const y = assistYaw(P, P.yaw, 40);
        shoot({ x: P.pos.x + Math.sin(y) * 0.8, y: P.pos.y + 1.45, z: P.pos.z + Math.cos(y) * 0.8, yaw: y, speed: 42, dmg: 18, owner: 'player', kind: 'spell', school: 'ice', target: P.aimPoint });
        addCamRecoil(0.026, (Math.random() - 0.5) * 0.01);
      }
      if (t > 0.55) P.state = 'free';
      break;
    }
    case 'prisaoGelo': {
      if (t >= 0.5 && !a.done) {
        a.done = true; sfx('magic');
        const cx = P.pos.x + Math.sin(P.yaw) * 4, cz = P.pos.z + Math.cos(P.yaw) * 4;
        burst(cx, P.pos.y + 0.5, cz, '#70d6ff', 24, 6);
        for (const e of G.enemies) {
          if (!e.alive || e.faction === 'guard') continue;
          if (Math.hypot(e.pos.x - cx, e.pos.z - cz) <= 3.8) {
            hurtEnemy(e, 22, { stun: 2.0, burstColor: '#70d6ff', big: true });
          }
        }
      }
      if (t > 0.7) P.state = 'free';
      break;
    }

    // Fogo
    case 'bolaFogo': {
      if (t >= 0.32 && !a.done) {
        a.done = true;
        const y = assistYaw(P, P.yaw, 40);
        shoot({ x: P.pos.x + Math.sin(y) * 0.8, y: P.pos.y + 1.45, z: P.pos.z + Math.cos(y) * 0.8, yaw: y, speed: 45, dmg: 28, owner: 'player', kind: 'spell', school: 'fire', target: P.aimPoint });
        addCamRecoil(0.04, (Math.random() - 0.5) * 0.015);
        addCamShake(0.3);
      }
      if (t > 0.65) P.state = 'free';
      break;
    }
    case 'erupcaoFogo': {
      if (t >= 0.5 && !a.done) {
        a.done = true; sfx('magic');
        const cx = P.pos.x + Math.sin(P.yaw) * 4, cz = P.pos.z + Math.cos(P.yaw) * 4;
        burst(cx, P.pos.y + 0.5, cz, '#ff6b35', 26, 6);
        for (const e of G.enemies) {
          if (!e.alive || e.faction === 'guard') continue;
          if (Math.hypot(e.pos.x - cx, e.pos.z - cz) <= 3.8) {
            hurtEnemy(e, 32, { burn: 3.5, burstColor: '#ff6b35', knock: 1.5, big: true });
          }
        }
      }
      if (t > 0.75) P.state = 'free';
      break;
    }

    // Natureza
    case 'enraizar': {
      if (t >= 0.3 && !a.done) {
        a.done = true; sfx('magic');
        for (const e of G.enemies) {
          if (!e.alive || e.faction === 'guard') continue;
          const d = Math.hypot(e.pos.x - P.pos.x, e.pos.z - P.pos.z);
          if (d <= 14 && Math.abs(angleDiff(Math.atan2(e.pos.x - P.pos.x, e.pos.z - P.pos.z), P.yaw)) < 0.8) {
            burst(e.pos.x, e.pos.y + 0.2, e.pos.z, '#52b788', 16, 3);
            hurtEnemy(e, 16, { root: 2.8, burstColor: '#52b788', big: true });
            break;
          }
        }
      }
      if (t > 0.6) P.state = 'free';
      break;
    }
    case 'bencaoTerra': {
      if (!a.done) {
        a.done = true; sfx('pickup');
        P.vigor = Math.min(P.maxVigor, P.vigor + 45);
        P.hp = Math.min(P.maxHp, P.hp + 18);
        P.invuln = 1.2;
        burst(P.pos.x, P.pos.y + 1, P.pos.z, '#52b788', 15, 3);
        floatText(P.pos.x, P.pos.y + 2.3, P.pos.z, '+18 vida', 'heal');
      }
      if (t > 0.55) P.state = 'free';
      break;
    }

    // Vento
    case 'lufadaVento': {
      if (t >= 0.22 && !a.done) {
        a.done = true; sfx('magic');
        burst(P.pos.x + Math.sin(P.yaw) * 2, P.pos.y + 1, P.pos.z + Math.cos(P.yaw) * 2, '#90e0ef', 20, 5);
        meleeSweep(P.pos.x, P.pos.z, P.yaw, 5.0, 1.8, 15, { knock: 5.0, big: true });
      }
      if (t > 0.55) P.state = 'free';
      break;
    }
    case 'saltoVento': {
      if (!a.done) {
        a.done = true; sfx('dodge');
        P.invuln = 0.35;
        moveEntity(P, Math.sin(P.aimYaw) * 8.0, Math.cos(P.aimYaw) * 8.0, 0.45);
        burst(P.pos.x, P.pos.y + 1, P.pos.z, '#90e0ef', 14, 4);
      }
      if (t > 0.5) P.state = 'free';
      break;
    }

    // Maldição
    case 'marcaCorruptora': {
      if (t >= 0.28 && !a.done) {
        a.done = true; sfx('magic');
        const y = assistYaw(P, P.yaw, 35);
        shoot({ x: P.pos.x + Math.sin(y) * 0.8, y: P.pos.y + 1.45, z: P.pos.z + Math.cos(y) * 0.8, yaw: y, speed: 40, dmg: 14, owner: 'player', kind: 'spell', school: 'curse', target: P.aimPoint });
        addCamRecoil(0.025, (Math.random() - 0.5) * 0.01);
      }
      if (t > 0.6) P.state = 'free';
      break;
    }
    case 'drenoVital': {
      if (t >= 0.35 && !a.done) {
        a.done = true; sfx('magic');
        for (const e of G.enemies) {
          if (!e.alive || e.faction === 'guard') continue;
          const d = Math.hypot(e.pos.x - P.pos.x, e.pos.z - P.pos.z);
          if (d <= 12 && Math.abs(angleDiff(Math.atan2(e.pos.x - P.pos.x, e.pos.z - P.pos.z), P.yaw)) < 0.7) {
            hurtEnemy(e, 26, { healPlayer: 26, burstColor: '#9b5de5', big: true });
            burst(P.pos.x, P.pos.y + 1.2, P.pos.z, '#9b5de5', 12, 3);
            break;
          }
        }
      }
      if (t > 0.7) P.state = 'free';
      break;
    }

    // Metamorfose
    case 'formaFera': {
      if (!a.done) {
        a.done = true; sfx('roar');
        P.metamorphT = 14;
        P.hp = Math.min(P.maxHp, P.hp + 25);
        burst(P.pos.x, P.pos.y + 1, P.pos.z, '#bc6c25', 20, 4);
        floatText(P.pos.x, P.pos.y + 2.4, P.pos.z, 'Forma da Fera!', 'crit');
      }
      if (t > 0.6) P.state = 'free';
      break;
    }
    case 'rugidoFera': {
      if (t >= 0.25 && !a.done) {
        a.done = true; sfx('roar');
        burst(P.pos.x, P.pos.y + 1.2, P.pos.z, '#d4a373', 25, 5);
        for (const e of G.enemies) {
          if (!e.alive || e.faction === 'guard') continue;
          if (Math.hypot(e.pos.x - P.pos.x, e.pos.z - P.pos.z) <= 5.2) {
            hurtEnemy(e, 14, { stun: 1.8, knock: 1.5, big: true });
          }
        }
      }
      if (t > 0.65) P.state = 'free';
      break;
    }

    // Cura Vital
    case 'curaVital': {
      if (!a.done) {
        a.done = true; sfx('pickup');
        P.hp = Math.min(P.maxHp, P.hp + 50);
        burst(P.pos.x, P.pos.y + 1.2, P.pos.z, '#74c69d', 18, 3);
        floatText(P.pos.x, P.pos.y + 2.3, P.pos.z, '+50 vida', 'heal');
      }
      if (t > 0.6) P.state = 'free';
      break;
    }
    case 'auraRestauracao': {
      if (!a.done) {
        a.done = true; sfx('pickup');
        P.auraVitalT = 5.0;
        burst(P.pos.x, P.pos.y + 0.3, P.pos.z, '#a7c957', 20, 3);
        floatText(P.pos.x, P.pos.y + 2.3, P.pos.z, 'aura restauradora', 'heal');
      }
      if (t > 0.65) P.state = 'free';
      break;
    }

    // Sagrado
    case 'escudoRadiante': {
      if (!a.done) {
        a.done = true; sfx('magic');
        P.shieldHp = 50; P.shieldT = 8.0;
        burst(P.pos.x, P.pos.y + 1.2, P.pos.z, '#ffd166', 22, 4);
        floatText(P.pos.x, P.pos.y + 2.3, P.pos.z, '+50 escudo sagrado', 'block');
      }
      if (t > 0.55) P.state = 'free';
      break;
    }
    case 'purificacaoSagrada': {
      if (t >= 0.35 && !a.done) {
        a.done = true; sfx('magic');
        burst(P.pos.x, P.pos.y + 1.2, P.pos.z, '#ffd166', 28, 6);
        for (const e of G.enemies) {
          if (!e.alive || e.faction === 'guard') continue;
          if (Math.hypot(e.pos.x - P.pos.x, e.pos.z - P.pos.z) <= 5.0) {
            const isShadow = e.faction === 'shadow';
            hurtEnemy(e, isShadow ? 42 : 25, { knock: 2.5, burstColor: '#ffd166', big: true });
          }
        }
      }
      if (t > 0.7) P.state = 'free';
      break;
    }

    default: P.state = 'free';
  }
}

export function drinkPotion(P) {
  if (P.cd.pot > 0) return;
  if (!count(P.inv, 'pocao')) { toast('Sem poções na bolsa.', 'warn'); sfx('deny'); return; }
  if (P.hp >= P.maxHp) { toast('Vida já está cheia.', 'info'); return; }
  removeFrom(P.inv, 'pocao', 1);
  P.hp = Math.min(P.maxHp, P.hp + 45); P.cd.pot = 2;
  floatText(P.pos.x, P.pos.y + 2.4, P.pos.z, '+45', 'heal'); sfx('pickup');
}

// ---------- pulo ----------
// Espaço: pulo curto, mantendo o passo. Shift + Espaço: salto impulsionado, mais alto e com impulso
// para a frente (na direção do movimento, ou para onde o corpo olha se estiver parado).
function startJump(P, boosted, mx, mz, sp, ls) {
  const J = boosted ? BOOST : JUMP;
  if (ls !== 'normal') { toast('Sobrecarga: não é possível pular.', 'warn'); sfx('deny'); return; }
  if (P.vigor < J.vigor) { toast('Vigor insuficiente para pular.', 'warn'); sfx('deny'); return; }
  P.vigor -= J.vigor; P.vigorDelay = 0.5;
  let vx = mx * sp, vz = mz * sp;
  if (boosted) {
    const dx = mx || mz ? mx : Math.sin(P.yaw), dz = mx || mz ? mz : Math.cos(P.yaw);
    const speed = Math.max(sp, J.speed * armorSpeed(P));
    vx = dx * speed; vz = dz * speed;
  }
  P.air = { y: 0, vy: J.vy, vx, vz, boosted, t: 0, dur: 2 * J.vy / GRAVITY };
  P.cmd = null;
  sfx('dodge');
}

// ---------- atualização ----------
export function updatePlayer(dt) {
  const P = G.player;
  const canAct = !G.uiOpen && !G.cinematic;
  P.invuln = Math.max(0, P.invuln - dt);
  for (const k in P.cd) P.cd[k] = Math.max(0, P.cd[k] - dt);
  P.maxHp = 100 + (P.equip.armor && P.equip.armor.cond > 0 ? ITEMS[P.equip.armor.id].hp : 0);
  if (P.hp > P.maxHp) P.hp = P.maxHp;

  P.metamorphT = Math.max(0, (P.metamorphT || 0) - dt);
  P.shieldT = Math.max(0, (P.shieldT || 0) - dt);
  if (P.shieldT <= 0) P.shieldHp = 0;
  if (P.auraVitalT > 0) {
    P.auraVitalT = Math.max(0, P.auraVitalT - dt);
    P.hp = Math.min(P.maxHp, P.hp + 16 * dt);
  }

  if (P.vigorDelay > 0) P.vigorDelay -= dt;
  else P.vigor = Math.min(P.maxVigor, P.vigor + 24 * dt);
  if (P.state !== 'down' && P.state !== 'dead' && !inCombat() && P.hp < P.maxHp) P.hp = Math.min(P.maxHp, P.hp + (P.zone === 'protegida' ? 9 : 3) * dt);

  updateAim(P);
  // Mira (botão direito): só a pé, fora da interface e com o personagem em pé. Enquanto dura,
  // a câmera aproxima, o corpo aponta para o cursor e o clique dispara na hora.
  input.lockWanted = canAct && !P.mounted && (P.state === 'free' || P.state === 'attack' || P.state === 'skill');
  P.aiming = input.lockWanted && input.mouse.right;
  input.aimLook = P.aiming;   // mirando, o mouse gira a câmera e a mira fica no centro da tela
  if (!P.aiming && input.locked) releaseLook();
  P.stateT += dt;

  // intenção de movimento: WASD relativo à câmera, ou aproximação de um alvo clicado
  let mx = 0, mz = 0;
  if (canAct) {
    const cy = G.cam.yaw;
    const f = (down('KeyW') ? 1 : 0) - (down('KeyS') ? 1 : 0);
    const r = (down('KeyD') ? 1 : 0) - (down('KeyA') ? 1 : 0);
    mx = -Math.sin(cy) * f + Math.cos(cy) * r;
    mz = -Math.cos(cy) * f - Math.sin(cy) * r;
    const l = Math.hypot(mx, mz); if (l > 0) { mx /= l; mz /= l; P.cmd = null; }
    else if (P.cmd) [mx, mz] = followCommand(P);
  }
  const moving = mx !== 0 || mz !== 0;
  const ls = loadState(P);
  const shift = down('ShiftLeft') || down('ShiftRight');
  // agachar: enquanto Ctrl estiver pressionado, a pé, no chão e livre para agir
  P.crouch = canAct && !P.mounted && !P.air && P.state === 'free' && (down('ControlLeft') || down('ControlRight'));
  P.crouchW = (P.crouchW || 0) + ((P.crouch ? 1 : 0) - (P.crouchW || 0)) * Math.min(1, dt * 8);
  P.running = !P.aiming && !P.crouch && (shift || P.cmd?.type === 'attack');
  let sp = (P.running ? RUN : WALK) * armorSpeed(P);
  if (P.crouch) sp *= CROUCH;
  if (P.landT > 0) { P.landT -= dt; sp *= 0.6; }   // amortecimento do pouso
  if (P.aiming) sp *= 0.62;   // mirando, o passo é curto e o corpo continua voltado para o alvo
  if (P.metamorphT > 0) sp *= 1.25;
  if (ls === 'sobrecarga') sp *= 0.6; else if (ls === 'limite') sp *= 0.35;
  if (P.mounted) sp = (ls === 'normal' ? 12.5 : 9) ;
  // a lâmina do rio acompanha o leito: a profundidade vem do ponto, não de um nível global
  const surface = waterAt(P.pos.x, P.pos.z);
  const wading = surface - groundHeight(P.pos.x, P.pos.z) > 0.35;
  if (wading) sp *= 0.55;


  let v = 0;
  switch (P.state) {
    case 'free': {
      if (P.air) {
        // no ar: o impulso do salto segue, e o comando corrige só uma parte (35 %)
        const a = P.air, pull = 1 - Math.exp(-AIR_CONTROL * 4 * dt);
        if (moving) { a.vx += (mx * sp - a.vx) * pull; a.vz += (mz * sp - a.vz) * pull; }
        moveEntity(P, a.vx * dt, a.vz * dt, 0.45, a.y);
        v = Math.hypot(a.vx, a.vz);
      } else if (moving) {
        moveEntity(P, mx * sp * dt, mz * sp * dt, P.mounted ? 1 : 0.45);
        v = sp;
      }
      // orientação: mira ao lutar ou parado; direção do passo ao andar
      const fighting = P.aiming || P.aimLock > 0 || (P.cmd && P.cmd.type === 'attack' && P.cmd.inRange);
      if (P.mounted) { if (moving) P.yaw = lerpAngle(P.yaw, Math.atan2(mx, mz), dt * 8); }
      else if (fighting || !moving) P.yaw = lerpAngle(P.yaw, P.aimYaw, dt * 16);
      else P.yaw = lerpAngle(P.yaw, Math.atan2(mx, mz), dt * 12);
      P.aimLock = Math.max(0, (P.aimLock || 0) - dt);
      // Tab + 1 escolhe a câmera distante: com Tab pressionado, o 1 não é a poção
      if ((hit('Digit1') || hit('Numpad1')) && !down('Tab')) drinkPotion(P);
      if (!canAct) break;
      if (P.mounted) {
        if (hit('KeyQ') || hit('KeyE') || (P.cmd && P.cmd.type === 'attack')) { toast('Desmonte para lutar (F ou R).', 'info'); if (P.cmd && P.cmd.type === 'attack') P.cmd = null; }
        break;
      }
      if (P.air) break;   // no ar só o movimento: ataques, técnicas e esquiva esperam o pouso
      if (P.aiming && input.mouse.leftPressed) { basicAttack(P); P.aimLock = 0.6; }
      else if (hit('Space')) startJump(P, shift, moving ? mx : 0, moving ? mz : 0, sp, ls);
      else if (hit('KeyC')) {
        if (ls !== 'normal') { toast('Sobrecarga: não é possível esquivar.', 'warn'); sfx('deny'); }
        else if (P.vigor < 22) { toast('Vigor insuficiente para esquivar.', 'warn'); sfx('deny'); }
        else {
          P.vigor -= 22; P.vigorDelay = 0.6;
          P.state = 'dodge'; P.stateT = 0;
          // andando: na direção do passo; parado: na direção do cursor
          P.dodgeYaw = moving ? Math.atan2(mx, mz) : P.aimYaw;
          // clipe escolhido uma vez, pelo sentido do passo em relação a para onde o corpo estava virado
          const d = toLocal(P.yaw, Math.sin(P.dodgeYaw), Math.cos(P.dodgeYaw));
          P.dodgeKey = dodgeClip(d.fwd, d.left, (k) => !!P.model.actions?.[k], standingDodgeFits(weaponFamily(P)));
          P.dodgeVis = P.dodgeYaw - (DODGE_FACING[P.dodgeKey] || 0);
          P.yaw = P.dodgeYaw; P.cmd = null; sfx('dodge');
        }
      } else if (P.cmd && P.cmd.type === 'attack' && P.cmd.inRange) { basicAttack(P); P.aimLock = 0.6; }
      else if (hit('KeyQ')) { useSkill(P, 'Q'); P.aimLock = 0.6; }
      else if (hit('KeyE')) { useSkill(P, 'E'); P.aimLock = 0.6; }
      if (hit('KeyV')) toggleShoulder();
      break;
    }
    case 'attack': {
      const a = P.act;
      if (moving) { moveEntity(P, mx * sp * 0.3 * dt, mz * sp * 0.3 * dt, 0.45); v = sp * 0.3; }
      if (!a.done && P.stateT >= a.windup) { a.done = true; resolveBasic(P); }
      if (P.stateT >= a.windup + a.recover) { P.state = 'free'; P.stateT = 0; }
      break;
    }
    case 'skill': updateSkill(P, dt); break;
    case 'dodge': {
      if (P.stateT < 0.34) moveEntity(P, Math.sin(P.dodgeYaw) * 14 * dt, Math.cos(P.dodgeYaw) * 14 * dt, 0.45);
      P.invuln = P.stateT < 0.28 ? 0.05 : P.invuln;
      if (P.stateT > 0.42) { P.state = 'free'; P.stateT = 0; }
      break;
    }
    case 'stagger': if (P.stateT > (P.staggerT || 0.5)) { P.state = 'free'; P.stateT = 0; } break;
    case 'channel': {
      const c = P.channel;
      if (!c) { P.state = 'free'; break; }
      if (moving && canAct) { cancelChannel(P, 'Cancelado'); break; }
      c.t += dt;
      if (c.t >= c.dur) { P.channel = null; P.state = 'free'; c.onDone(); }
      break;
    }
    case 'down': {
      const guard = G.enemies.find((e) => e.alive && e.faction === 'guard' && Math.hypot(e.pos.x - P.pos.x, e.pos.z - P.pos.z) < 24);
      if (guard && P.stateT > 2.5) {
        P.state = 'free'; P.hp = Math.round(P.maxHp * 0.35); P.invuln = 2.5; P.lastCombat = G.time;
        toast('Um guarda ajudou você a se levantar.', 'info');
        tally('resgates', 'feitos', 'comunidade', (n) => ({ title: 'Resgatado em campo', text: n > 1 ? `Foi levantado por guardas ${n} vezes após ser derrubado.` : 'Foi levantado por um guarda após ser derrubado. A derrubada não virou perda.' }));
      } else if (P.stateT > 4.5) {
        P.state = 'dead'; P.stateT = 0;
        const report = handleDeath(P, P.downSrc);
        emit('show-death', report);
      }
      break;
    }
    case 'dead': break;
  }
  P.speedNow = v;

  // pulo: a gravidade vale em qualquer estado (derrubado no ar também cai)
  if (P.air) {
    const a = P.air;
    a.t += dt;
    a.vy -= GRAVITY * dt;
    a.y += a.vy * dt;
    if (a.y <= 0 && a.vy < 0) { P.air = null; P.landT = a.boosted ? 0.2 : 0.12; }
  }
  P.pos.y = groundHeight(P.pos.x, P.pos.z) + (P.air ? P.air.y : 0);
  if (wading) P.pos.y = Math.max(P.pos.y, surface - 0.9);

  // zona e lugar
  const zone = isTurbulentSpace(P.pos.x) ? 'turbulenta' : zoneAt(P.pos.x, P.pos.z);
  if (zone !== P.zone) { const from = P.zone; P.zone = zone; emit('zone-change', { from, to: zone }); }
  const place = placeAt(P.pos.x, P.pos.z);
  if (place !== P.place) { P.place = place; emit('place-change', place); }

  // modelo
  const r = P.model.root;
  r.position.copy(P.pos);
  if (P.mounted && G.mount) r.position.y += G.mount.model.seatY - P.model.hipsY;   // quadril do cavaleiro sobre a sela
  // Esquiva direcional: o corpo fica virado de forma que o clipe (de lado, para trás) mostre o mesmo
  // sentido do deslocamento, que segue P.dodgeYaw; depois volta a P.yaw em ~0,25 s. Só o modelo gira.
  if (P.state === 'dodge') P.visHold = 0.25;
  else P.visHold = Math.max(0, (P.visHold || 0) - dt);
  const visTarget = P.state === 'dodge' && P.dodgeVis != null ? P.dodgeVis : P.yaw;
  P.visYaw = P.visHold > 0 ? lerpAngle(P.visYaw ?? P.yaw, visTarget, dt * 22) : P.yaw;
  r.rotation.y = P.visYaw;
  let attack = -1, kind = 'swing';
  if (P.state === 'attack') { attack = Math.min(1, P.stateT / (P.act.windup + P.act.recover)); kind = P.act.kind; }
  if (P.state === 'skill') {
    const id = P.act.id;
    kind = id === 'giro' || id === 'ceifaCircular' ? 'spin'
      : id === 'investida' || id === 'estocadaLonga' || id === 'passoSombras' ? 'thrust'
      : id === 'tiroCarregado' ? 'charge'
      : id === 'recuo' ? 'bow'
      : id === 'tiroBesta' || id === 'disparoRepulsao' ? 'crossbow'
      : id === 'comboManopla' || id === 'avancoImpacto' ? 'rapid'
      : id === 'machadadaPesada' || id === 'golpeEsmagador' || id === 'ondaChoque' ? 'heavy'
      : id === 'dilacerar' || id === 'golpePreciso' || id === 'varrerDistancia' || id === 'puxaoFoice' ? 'slash'
      : id?.startsWith('seta') || id?.startsWith('bola') || id?.startsWith('erupcao') || id?.startsWith('prisao') || id?.startsWith('enraizar') || id?.startsWith('lufada') || id?.startsWith('marca') || id?.startsWith('cura') || id?.startsWith('escudo') || id?.startsWith('purificacao') ? 'cast'
      : 'swing';
    const dur = {
      investida: 0.55, giro: 0.75, tiroCarregado: 0.85, recuo: 0.5, atordoar: 0.85,
      comboManopla: 0.55, avancoImpacto: 0.6, golpeEsmagador: 0.7, ondaChoque: 0.75,
      machadadaPesada: 0.75, dilacerar: 0.6, passoSombras: 0.45, golpePreciso: 0.45,
      estocadaLonga: 0.6, varrerDistancia: 0.65, ceifaCircular: 0.75, puxaoFoice: 0.6,
      tiroBesta: 0.7, disparoRepulsao: 0.55, setaGelo: 0.55, prisaoGelo: 0.7,
      bolaFogo: 0.65, erupcaoFogo: 0.75, enraizar: 0.6, bencaoTerra: 0.55,
      lufadaVento: 0.55, saltoVento: 0.5, marcaCorruptora: 0.6, drenoVital: 0.7,
      formaFera: 0.6, rugidoFera: 0.65, curaVital: 0.6, auraRestauracao: 0.65,
      escudoRadiante: 0.55, purificacaoSagrada: 0.7,
    }[id] || 0.6;
    attack = Math.min(1, P.stateT / dur);
    if (id === 'giro' || id === 'ceifaCircular') attack = P.stateT < 0.35 ? 0 : Math.min(1, (P.stateT - 0.35) / 0.4);
  }
  let fwd = 1, strafe = 0;
  if (moving) {
    fwd = mx * Math.sin(P.visYaw) + mz * Math.cos(P.visYaw);
    strafe = mx * Math.cos(P.visYaw) - mz * Math.sin(P.visYaw);
  }
  animateHumanoid(P.model, {
    dt, mps: P.mounted ? 0 : v, attack, kind, mounted: P.mounted,
    dodge: P.state === 'dodge' ? Math.min(1, P.stateT / 0.42) : -1, aim: P.aimT,
    dodgeKey: P.state === 'dodge' ? P.dodgeKey : null,
    hit: P.hitAt != null && G.time - P.hitAt < 3 ? { side: P.hitDir, t: G.time - P.hitAt } : null,
    aimPitch: P.aimPitch || 0,
    fwd, strafe, moveDir: moving ? [mx, mz] : null,
    down: P.state === 'down' || P.state === 'dead', channel: P.state === 'channel' && P.channel && P.channel.anim,
    metamorph: P.metamorphT > 0,
    crouch: P.crouch ? 1 : 0,
    air: P.air ? { phase: Math.min(1, P.air.t / P.air.dur), boosted: P.air.boosted } : null,
  });
  // cintila durante invulnerabilidade de respawn
  r.visible = !(P.invuln > 0.3 && P.state === 'free' && Math.floor(G.time * 12) % 2 === 0);
}

function lerpAngle(a, b, t) { return a + angleDiff(b, a) * Math.min(1, t); }

// Comandos de clique (pointer.js): atacar um inimigo ou ir até um objeto e usá-lo.
// Retorna a direção de movimento [mx, mz]; ataque e uso disparam ao chegar.
function followCommand(P) {
  const c = P.cmd;
  let tx, tz, stop;
  if (c.type === 'attack') {
    const e = c.e;
    if (!e.alive || Math.hypot(e.pos.x - P.pos.x, e.pos.z - P.pos.z) > 60) { P.cmd = null; return [0, 0]; }
    tx = e.pos.x; tz = e.pos.z;
    const b = WEAPONS[weaponFamily(P)].basic;
    stop = b.type === 'ranged' ? 16 : b.range * 0.8 + e.radius;
    P.aimYaw = Math.atan2(tx - P.pos.x, tz - P.pos.z);
  } else {
    const t = c.t, p = t.live || t;
    if (t.valid && !t.valid()) { P.cmd = null; return [0, 0]; }
    tx = p.x; tz = p.z; stop = Math.max(1.4, Math.min(t.r * 0.8, 4));
  }
  const dx = tx - P.pos.x, dz = tz - P.pos.z, d = Math.hypot(dx, dz);
  if (c.type === 'attack') c.inRange = d <= stop;
  if (d <= stop) {
    if (c.type === 'use' && P.state === 'free') { P.cmd = null; if (c.t.run) c.t.run(); }
    return [0, 0];
  }
  // usar: desiste se ficar preso (sem se aproximar por 1,2 s); no ataque o alvo se move, então segue
  if (c.type !== 'attack') {
    if (d < c.best - 0.3) { c.best = d; c.since = G.time; }
    else if (G.time - c.since > 1.2) { P.cmd = null; return [0, 0]; }
  }
  return [dx / d, dz / d];
}

// ---------- câmera em terceira pessoa ----------
// Inclinação: a câmera orbita o jogador e olha para ele. Para ver céu ela teria que descer abaixo
// do peito do personagem, onde só há 1,7 m até o chão — então abaixo de ORBIT_MIN ela para de
// descer e quem sobe é o ponto para onde ela olha. O jogador escorrega para baixo no quadro, como
// em qualquer terceira pessoa, mas continua visível.
export const PITCH_DEFAULT = 0.67, PITCH_MIN = -0.45, PITCH_MAX = 1.35;
const ORBIT_MIN = 0.04;
// Mira over-the-shoulder: aproximação suave, FOV fechado e dinâmico, ombro alternável (V),
// mola anti-colisão (spring arm) contra muros e terreno, e amortecimento fino.
const AIM_DIST = 4.8, AIM_PITCH_MIN = -0.55, AIM_PITCH_MAX = 0.95;
const AIM_FOV = 35, AIM_SIDE = 0.88, FOV_BASE = 55;
// Suavização por tempo, não por quadro: `k(12, dt)` responde igual a 30, 60 ou 144 quadros por
// segundo. Com o fator antigo (dt × taxa) a câmera ficava mole em máquinas rápidas e brusca nas
// lentas — o mesmo movimento de mouse dava enquadramentos diferentes.
const k = (rate, dt) => 1 - Math.exp(-rate * dt);
const camTarget = new THREE.Vector3(), camPos = new THREE.Vector3(), lookAt = new THREE.Vector3();

export function updateCamera(dt, snap) {
  const P = G.player, cam = G.cam, camera = G.camera;
  // Câmera livre de depuração (?debug=1 → __demo.freecam): só para inspecionar o mundo.
  if (G.freeCam) {
    camera.position.set(G.freeCam.pos[0], G.freeCam.pos[1], G.freeCam.pos[2]);
    camera.lookAt(G.freeCam.look[0], G.freeCam.look[1], G.freeCam.look[2]);
    OCC.uTarget.value.copy(camera.position); OCC.uCam.value.copy(camera.position);
    return;
  }
  const aiming = !!P.aiming && !G.uiOpen;
  const a = P.aimT = snap ? (aiming ? 1 : 0) : (P.aimT || 0) + ((aiming ? 1 : 0) - (P.aimT || 0)) * Math.min(1, dt * 9);

  // Amortecimento do recuo e tremor da câmera
  cam.recoilPitch = (cam.recoilPitch || 0) * Math.max(0, 1 - dt * 14);
  cam.recoilYaw = (cam.recoilYaw || 0) * Math.max(0, 1 - dt * 14);
  cam.shake = Math.max(0, (cam.shake || 0) - dt * 2.8);

  // Alternância suave de ombro (Key V: 1 direito, -1 esquerdo)
  cam.shoulder = cam.shoulder || 1;
  cam.curShoulder = snap ? cam.shoulder : (cam.curShoulder || 1) + (cam.shoulder - (cam.curShoulder || 1)) * Math.min(1, dt * 8);

  if (!G.uiOpen) {
    // Zoom suave e contínuo
    cam.targetDist = clamp((cam.targetDist ?? cam.dist ?? 14) + input.mouse.wheel * 1.5, 6, 22);
    cam.dist += (cam.targetDist - cam.dist) * (snap ? 1 : k(11, dt));

    // Sensibilidade adaptativa ao FOV na mira (evita mira arisca)
    const sens = camSens() * (1 - a * 0.35), inv = CAM_PREFS.invert ? -1 : 1;

    // girar e inclinar: arrastar o chão com o botão esquerdo (ou o do meio), as setas, ou o mouse
    // com o ponteiro travado durante a mira.
    const turn = (down('ArrowRight') ? 1 : 0) - (down('ArrowLeft') ? 1 : 0);
    const tilt = (down('ArrowDown') ? 1 : 0) - (down('ArrowUp') ? 1 : 0);
    const olhando = input.locked || input.aimLook;   // mira: o mouse gira, com ou sem ponteiro travado
    const dx = input.mouse.dragDX + (cam.dragging ? input.mouse.leftDX : 0) + (olhando ? input.look.dx : 0);
    const dy = input.mouse.dragDY + (cam.dragging ? input.mouse.leftDY : 0) + (olhando ? input.look.dy : 0);
    if (turn || tilt || dx || dy) cam.resetting = false;
    cam.yaw -= (turn * dt * 1.9 + dx * 0.0052) * sens;
    cam.pitch = clamp(cam.pitch + (tilt * dt * 1.1 + dy * 0.0045) * sens * inv, PITCH_MIN, PITCH_MAX);

    // Sem o ponteiro travado o cursor de verdade continua andando e uma hora encosta na borda da
    // tela, onde o mouse deixa de relatar movimento. Nos últimos 6% de cada lado ele passa a
    // empurrar a câmera, para a volta não parar no fim do monitor; no miolo, quem manda é o giro 1:1.
    if (aiming && !input.locked) {
      const ex = input.mouse.rawX / innerWidth - 0.5, ey = input.mouse.rawY / innerHeight - 0.5;
      const overX = Math.abs(ex) - 0.44, overY = Math.abs(ey) - 0.44;
      if (overX > 0) { cam.yaw -= Math.sign(ex) * overX * dt * 26 * sens; cam.resetting = false; }
      if (overY > 0) { cam.pitch = clamp(cam.pitch + Math.sign(ey) * overY * dt * 16 * sens * inv, PITCH_MIN, PITCH_MAX); cam.resetting = false; }
    }
  }

  if (cam.resetting) {   // botão "N" do minimapa: volta ao norte e à inclinação padrão
    const target = Math.round(cam.yaw / (Math.PI * 2)) * Math.PI * 2;
    const kr = k(8, dt);
    cam.yaw += (target - cam.yaw) * kr;
    cam.pitch += (PITCH_DEFAULT - cam.pitch) * kr;
    if (Math.abs(target - cam.yaw) < 0.002 && Math.abs(PITCH_DEFAULT - cam.pitch) < 0.002) { cam.yaw = 0; cam.pitch = PITCH_DEFAULT; cam.resetting = false; }
  }

  const fov = cam.mode === 'sky' ? 26 : FOV_BASE + (AIM_FOV - FOV_BASE) * a;
  const fovK = cam.mode === 'sky' || (a < 0.01 && camera.fov < 40) ? 3 : 9;
  if (Math.abs(camera.fov - fov) > 0.05) { camera.fov += (fov - camera.fov) * Math.min(1, snap ? 1 : dt * fovK); camera.updateProjectionMatrix(); }
  if (cam.mode === 'sky' && cam.skyDir) {
    camPos.set(P.pos.x, P.pos.y + 14, P.pos.z);
    camera.position.lerp(camPos, snap ? 1 : Math.min(1, dt * 2));
    lookAt.copy(camera.position).addScaledVector(cam.skyDir, 100);
    cam.look.lerp(lookAt, snap ? 1 : Math.min(1, dt * 1.6));
    camera.lookAt(cam.look);
    OCC.uTarget.value.copy(camera.position); OCC.uCam.value.copy(camera.position);
    return;
  }

  // Mirando: transição de inclinação suave preservando alcance vertical
  const pAim = clamp(cam.pitch * 0.72 + 0.08, AIM_PITCH_MIN, AIM_PITCH_MAX);
  const pitch = cam.pitch + (pAim - cam.pitch) * a;
  const dist = cam.dist + (AIM_DIST - cam.dist) * a;

  const effPitch = pitch + (cam.recoilPitch || 0);
  const effYaw = cam.yaw + (cam.recoilYaw || 0);
  const orbit = Math.max(effPitch, ORBIT_MIN);
  const lift = Math.tan(Math.max(0, ORBIT_MIN - effPitch)) * dist;

  // O alvo acompanha o jogador na hora na horizontal e com atraso na vertical: degraus, pedras e
  // ondulações do terreno deixam de sacudir o enquadramento. Desnível grande (queda, montaria,
  // teleporte) é alcançado depressa, para o personagem não sair do quadro.
  // agachado, o alvo desce; no pulo, a câmera acompanha só metade da subida (menos balanço)
  const alvoY = P.pos.y - (P.air ? P.air.y * 0.5 : 0) + (P.mounted ? 3 : 1.7 - 0.55 * (P.crouchW || 0));
  if (snap || cam.tgtY == null) cam.tgtY = alvoY;
  else cam.tgtY += (alvoY - cam.tgtY) * k(Math.abs(alvoY - cam.tgtY) > 2 ? 16 : 6, dt);
  // correndo, a câmera olha um pouco à frente: aparece mais do caminho e menos das costas
  const quer = (1 - a) * clamp(((P.speedNow || 0) - WALK * 0.6) / (RUN - WALK * 0.6), 0, 1) * 1.1;
  cam.lead = snap ? quer : (cam.lead || 0) + (quer - (cam.lead || 0)) * k(3.5, dt);
  camTarget.set(P.pos.x + Math.sin(P.yaw) * cam.lead, cam.tgtY, P.pos.z + Math.cos(P.yaw) * cam.lead);
  if (a > 0.001) {   // ombro alternável por cima do ombro
    camTarget.x += Math.cos(effYaw) * (AIM_SIDE * cam.curShoulder) * a;
    camTarget.z -= Math.sin(effYaw) * (AIM_SIDE * cam.curShoulder) * a;
    camTarget.y += 0.14 * a;
  }

  // Spring-Arm de Colisão da Câmera: evita penetrar terreno, construções, rochas e muros
  const co = Math.cos(orbit), so = Math.sin(orbit);
  const dirX = Math.sin(effYaw) * co;
  const dirY = so;
  const dirZ = Math.cos(effYaw) * co;

  let clearDist = dist;
  const probeSteps = 12;
  for (let step = 1; step <= probeSteps; step++) {
    const testD = (step / probeSteps) * dist;
    const px = camTarget.x + dirX * testD;
    const py = camTarget.y + dirY * testD;
    const pz = camTarget.z + dirZ * testD;
    const gh = groundHeight(px, pz) + 0.65;
    if (py < gh || testPoint(px, pz, 0.42)) {
      clearDist = Math.max(1.8, testD - 0.45);
      break;
    }
  }

  // entra depressa (o muro chegou), volta devagar (não pula de volta ao passar da quina)
  if (snap || !cam.actualDist) cam.actualDist = clearDist;
  else cam.actualDist = lerp(cam.actualDist, clearDist, k(clearDist < cam.actualDist ? 25 : 6.5, dt));

  camPos.set(camTarget.x + dirX * cam.actualDist, camTarget.y + dirY * cam.actualDist, camTarget.z + dirZ * cam.actualDist);
  const minFloor = groundHeight(camPos.x, camPos.z) + 0.55;
  if (camPos.y < minFloor) camPos.y = minFloor;

  if (cam.shake > 0.01) {
    const sk = cam.shake * cam.shake * 0.16;
    camPos.x += (Math.random() - 0.5) * sk;
    camPos.y += (Math.random() - 0.5) * sk;
    camPos.z += (Math.random() - 0.5) * sk;
  }

  lookAt.copy(camTarget); lookAt.y += lift;
  camera.position.lerp(camPos, snap ? 1 : k(7.5 + 6.5 * a, dt));
  cam.look.lerp(lookAt, snap ? 1 : k(10 + 8 * a, dt));
  camera.lookAt(cam.look);
  OCC.uCam.value.copy(camera.position); OCC.uTarget.value.copy(camTarget);
}
