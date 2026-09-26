// Sinais de preparação, partículas, textos flutuantes e barras sobre entidades.
import * as THREE from 'three';
import { groundHeight } from './terrain.js';

let scene, camera, layer;
const tele = [], parts = [], floats = [], bars = new Map();
const v3 = new THREE.Vector3();

export function initFx(s, c) { scene = s; camera = c; layer = document.getElementById('fx-layer'); }

function groundGeo(pointsLocal, cx, cz, rot, lift) {
  // pointsLocal: [[x,z], ...] como leque de triângulos a partir do primeiro ponto
  const cs = Math.cos(rot), sn = Math.sin(rot);
  const pos = [];
  const wy = (lx, lz) => groundHeight(cx + lx * cs + lz * sn, cz - lx * sn + lz * cs) + lift;
  for (let i = 1; i < pointsLocal.length - 1; i++) {
    for (const p of [pointsLocal[0], pointsLocal[i], pointsLocal[i + 1]]) pos.push(p[0], wy(p[0], p[1]), p[1]);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  return g;
}
function shapePoints(o) {
  const pts = [[0, 0]];
  if (o.shape === 'line') {
    const w = o.width / 2;
    return [[-w, 0], [w, 0], [w, o.length], [-w, o.length]];
  }
  const arc = o.shape === 'circle' ? Math.PI * 2 : o.arc;
  const seg = o.shape === 'circle' ? 28 : 14;
  for (let i = 0; i <= seg; i++) {
    const a = -arc / 2 + arc * i / seg;
    pts.push([Math.sin(a) * o.radius, Math.cos(a) * o.radius]);
  }
  return pts;
}

// Sinal no chão: vermelho = ameaça. Preenchimento cresce até o momento do golpe.
export function telegraph(o) {
  const color = o.color || '#ff5a3c';
  const rot = o.facing || 0;
  const pts = shapePoints(o);
  const ring = new THREE.Mesh(groundGeo(pts, o.x, o.z, rot, 0.12), new THREE.MeshBasicMaterial({ color, transparent: true, opacity: 0.18, depthWrite: false, side: THREE.DoubleSide, fog: false }));
  const fill = new THREE.Mesh(groundGeo(pts, o.x, o.z, rot, 0.14), new THREE.MeshBasicMaterial({ color, transparent: true, opacity: 0.42, depthWrite: false, side: THREE.DoubleSide, fog: false }));
  const g = new THREE.Group();
  g.add(ring, fill); g.position.set(o.x, 0, o.z); g.rotation.y = rot;
  g.renderOrder = 5; ring.renderOrder = 5; fill.renderOrder = 6;
  scene.add(g);
  const t = { g, fill, ring, t: 0, dur: o.duration, line: o.shape === 'line', cancelled: false };
  fill.scale.set(0.01, 1, 0.01);
  tele.push(t);
  return t;
}
export function cancelTelegraph(t) { if (t) t.cancelled = true; }

const pGeo = new THREE.BoxGeometry(0.14, 0.14, 0.14);
const pMats = new Map();
export function burst(x, y, z, color = '#ffd27a', n = 10, speed = 5) {
  if (!pMats.has(color)) pMats.set(color, new THREE.MeshBasicMaterial({ color }));
  for (let i = 0; i < n; i++) {
    const m = new THREE.Mesh(pGeo, pMats.get(color));
    m.position.set(x, y, z);
    const a = Math.random() * Math.PI * 2, u = Math.random();
    parts.push({ m, vx: Math.cos(a) * speed * u, vy: 2 + Math.random() * speed, vz: Math.sin(a) * speed * u, life: 0.5 + Math.random() * 0.4 });
    scene.add(m);
  }
}

export function floatText(x, y, z, text, cls = '') {
  const el = document.createElement('div');
  el.className = 'float ' + cls; el.textContent = text;
  layer.appendChild(el);
  floats.push({ el, x, y, z, t: 0 });
}

// Barra de vida sobre inimigo (só aparece depois de combate)
export function showBar(ent) {
  if (bars.has(ent)) return;
  const el = document.createElement('div');
  el.className = 'ebar' + (ent.faction === 'shadow' ? ' shadow' : '');
  el.innerHTML = '<i></i>' + (ent.faction === 'shadow' ? '' : `<span>${ent.cfg.name}</span>`);
  layer.appendChild(el);
  bars.set(ent, el);
}
export function removeBar(ent) { const el = bars.get(ent); if (el) { el.remove(); bars.delete(ent); } }

function project(x, y, z) {
  v3.set(x, y, z).project(camera);
  if (v3.z > 1) return null;
  return [(v3.x * 0.5 + 0.5) * innerWidth, (-v3.y * 0.5 + 0.5) * innerHeight];
}

// Anel de seleção sob o que está sob o cursor
const ringGeo = new THREE.RingGeometry(0.72, 1, 28).rotateX(-Math.PI / 2);
let hoverRing = null;
export function setHoverRing(x, z, r, color) {
  if (!hoverRing) {
    hoverRing = new THREE.Mesh(ringGeo, new THREE.MeshBasicMaterial({ transparent: true, opacity: 0.75, depthWrite: false, fog: false }));
    hoverRing.renderOrder = 6;
    scene.add(hoverRing);
  }
  if (color == null) { hoverRing.visible = false; return; }
  hoverRing.visible = true;
  hoverRing.material.color.set(color);
  hoverRing.position.set(x, groundHeight(x, z) + 0.14, z);
  hoverRing.scale.setScalar(r);
}

export function updateFx(dt) {
  if (hoverRing && hoverRing.visible) hoverRing.rotation.y += dt * 1.5;
  for (let i = tele.length - 1; i >= 0; i--) {
    const t = tele[i]; t.t += dt;
    const k = Math.min(1, t.t / t.dur);
    if (t.line) t.fill.scale.set(1, 1, Math.max(0.01, k)); else t.fill.scale.set(Math.max(0.01, k), 1, Math.max(0.01, k));
    if (k >= 1) t.fill.material.opacity = 0.65;
    if (t.cancelled || t.t > t.dur + 0.12) {
      scene.remove(t.g); t.ring.geometry.dispose(); t.fill.geometry.dispose(); t.ring.material.dispose(); t.fill.material.dispose();
      tele.splice(i, 1);
    }
  }
  for (let i = parts.length - 1; i >= 0; i--) {
    const p = parts[i]; p.life -= dt;
    p.vy -= 14 * dt; p.m.position.x += p.vx * dt; p.m.position.y += p.vy * dt; p.m.position.z += p.vz * dt;
    p.m.rotation.x += dt * 8; p.m.scale.setScalar(Math.max(0.05, p.life * 1.6));
    if (p.life <= 0) { scene.remove(p.m); parts.splice(i, 1); }
  }
  for (let i = floats.length - 1; i >= 0; i--) {
    const f = floats[i]; f.t += dt;
    const s = project(f.x, f.y + f.t * 1.6, f.z);
    if (!s || f.t > 1.1) { f.el.remove(); floats.splice(i, 1); continue; }
    f.el.style.transform = `translate(${s[0]}px, ${s[1]}px) translate(-50%, -50%)`;
    f.el.style.opacity = String(1 - Math.max(0, f.t - 0.6) / 0.5);
  }
  for (const [ent, el] of bars) {
    const s = ent.alive !== false ? project(ent.pos.x, ent.pos.y + (ent.barY || 2.6), ent.pos.z) : null;
    const d = camera.position.distanceTo(v3.set(ent.pos.x, ent.pos.y, ent.pos.z));
    if (!s || d > 55 || ent.hidden) { el.style.display = 'none'; continue; }
    el.style.display = '';
    el.style.transform = `translate(${s[0]}px, ${s[1]}px) translate(-50%, -100%)`;
    el.firstChild.style.width = Math.max(0, ent.hp / ent.maxHp * 100) + '%';
  }
}

export function clearFxFor(ent) { removeBar(ent); }
