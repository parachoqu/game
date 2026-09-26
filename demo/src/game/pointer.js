// Ponteiro: o que está sob o cursor (inimigo > objeto > chão) e os comandos de clique.
//   clique no inimigo → atacar até cair · clique no objeto → ir até ele e usar · arrastar o chão → girar a câmera
import * as THREE from 'three';
import { G } from '../state.js';
import { input } from '../engine/input.js';
import { groundHeight } from '../engine/terrain.js';
import { setHoverRing } from '../engine/fx.js';
import { targetsNear } from './interact.js';

const PICK_PX = 46;
const v = new THREE.Vector3();

function screenOf(x, y, z) {
  v.set(x, y, z).project(G.camera);
  if (v.z > 1) return null;
  return [(v.x * 0.5 + 0.5) * innerWidth, (-v.y * 0.5 + 0.5) * innerHeight];
}

function pick(P) {
  const mx = input.mouse.x, my = input.mouse.y;
  let best = null, bd = PICK_PX;
  for (const e of G.enemies) {
    if (!e.alive || e.faction === 'guard' || !e.model.m.root.visible) continue;
    if (Math.hypot(e.pos.x - P.pos.x, e.pos.z - P.pos.z) > 60) continue;
    const s = screenOf(e.pos.x, e.pos.y + (e.barY || 2.4) * 0.45, e.pos.z);
    if (!s) continue;
    const d = Math.hypot(s[0] - mx, s[1] - my);
    if (d < bd) { bd = d; best = { kind: 'enemy', e }; }
  }
  if (best) return best;
  bd = PICK_PX;
  for (const t of targetsNear(P, 45)) {
    if (!t.run) continue;
    const s = screenOf(t.x, t.y ?? groundHeight(t.x, t.z) + 1, t.z);
    if (!s) continue;
    const d = Math.hypot(s[0] - mx, s[1] - my);
    if (d < bd) { bd = d; best = { kind: 'use', t }; }
  }
  return best;
}

export function updatePointer() {
  const P = G.player;
  const cv = G.three.domElement;
  if (G.uiOpen || G.cinematic || P.state === 'down' || P.state === 'dead') {
    G.hover = null; G.cam.dragging = false; setHoverRing(0, 0, 1, null); cv.style.cursor = ''; return;
  }
  const hov = pick(P);
  G.hover = hov;

  // anel: o que está sob o cursor; se nada, o alvo de ataque atual
  const tgt = P.cmd && P.cmd.type === 'attack' && P.cmd.e.alive ? P.cmd.e : null;
  if (hov && hov.kind === 'enemy') setHoverRing(hov.e.pos.x, hov.e.pos.z, hov.e.radius + 0.6, '#ff5a3c');
  else if (hov) { const p = hov.t.live || hov.t; setHoverRing(p.x, p.z, 1.3, '#ece4d2'); }
  else if (tgt) setHoverRing(tgt.pos.x, tgt.pos.z, tgt.radius + 0.6, '#ff5a3c');
  else setHoverRing(0, 0, 1, null);

  // Mirando (botão direito), o clique dispara na direção da mira: nada de comandar deslocamento
  // nem de girar a câmera arrastando. O ponteiro sai e a mira desenhada toma o lugar dele.
  if (P.aiming) {
    P.cmd = null; G.cam.dragging = false;
    cv.style.cursor = 'none';
    return;
  }

  if (input.mouse.leftPressed) {
    if (hov && hov.kind === 'enemy') P.cmd = { type: 'attack', e: hov.e };
    else if (hov) P.cmd = { type: 'use', t: hov.t, best: Infinity, since: G.time };
    else G.cam.dragging = true;   // começou no chão: arrastar gira a câmera
  }
  if (!input.mouse.left) G.cam.dragging = false;
  cv.style.cursor = G.cam.dragging ? 'grabbing' : hov ? (hov.kind === 'enemy' ? 'crosshair' : 'pointer') : 'grab';
}
