// Montaria de carga como item equipado (cap. 06): chamar, montar e desmontar exigem condição segura.
import * as THREE from 'three';
import { G } from '../state.js';
import { groundHeight } from '../engine/terrain.js';
import { makeMount, animateMount } from '../engine/characters.js';
import { sfx } from '../engine/audio.js';
import { placeAt } from './layout.js';
import { inCombat } from './combat.js';
import { startChannel, mountUp, dismount } from './player.js';
import { moveEntity } from './collide.js';
import { toast } from '../ui/hud.js';

export function initMount() {
  const model = makeMount();
  const m = {
    present: false, pos: new THREE.Vector3(), yaw: 0, model,
    spawnNear(P) {
      const side = P.yaw - Math.PI / 2;
      m.pos.set(P.pos.x + Math.sin(side) * 2.5, 0, P.pos.z + Math.cos(side) * 2.5);
      moveEntity(m, 0, 0, 1);
      m.pos.y = groundHeight(m.pos.x, m.pos.z);
      m.yaw = P.yaw;
      if (!m.present) G.scene.add(model.root);
      m.present = true;
    },
    despawn() {
      if (G.player.mounted) dismount(G.player, true);
      if (m.present) G.scene.remove(model.root);
      m.present = false;
      // alforjes voltam com o animal para o estábulo
      if (G.player.saddle.length) { G.player.stableBags = (G.player.stableBags || []).concat(G.player.saddle.splice(0)); }
    },
  };
  G.mount = m;
  return m;
}

export function mountAction(P) {
  const m = G.mount;
  if (P.mounted) { dismount(P); return; }
  if (!P.equip.mount) { toast('Você não tem montaria. O estábulo do Vale vende um cavalo de carga.', 'info'); return; }
  if (P.zone === 'turbulenta') { toast('Montarias não atravessam para a Região Turbulenta.', 'warn'); return; }
  if (inCombat()) { toast('Montar ou chamar a montaria exige estar fora de combate.', 'warn'); sfx('deny'); return; }
  if (P.state !== 'free') return;
  if (m.present) {
    const d = Math.hypot(m.pos.x - P.pos.x, m.pos.z - P.pos.z);
    if (d < 5) startChannel(P, 'Montando', 1.0, () => mountUp(P), { anim: false });
    else if (d < 80) startChannel(P, 'Chamando a montaria', 2.5, () => { m.spawnNear(P); mountUp(P); }, { anim: false });
    else toast(`Sua montaria está a ${Math.round(d)} m (${placeAt(m.pos.x, m.pos.z)}). Vá até ela.`, 'warn');
  } else {
    startChannel(P, 'Chamando a montaria', 2.0, () => { m.spawnNear(P); mountUp(P); }, { anim: false });
  }
}

export function updateMount(dt) {
  const m = G.mount, P = G.player;
  if (!m || !m.present) return;
  if (P.mounted) { m.pos.copy(P.pos); m.yaw = P.yaw; }
  m.pos.y = groundHeight(m.pos.x, m.pos.z);
  m.model.root.position.copy(m.pos);
  m.model.root.rotation.y = m.yaw;
  animateMount(m.model, { dt, speed: P.mounted ? P.speedNow / 12.5 : 0 });
}
