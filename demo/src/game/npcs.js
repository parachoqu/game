// NPCs fixos (serviços) e viajantes que simulam outros jogadores na estrada.
import { G } from '../state.js';
import { groundHeight } from '../engine/terrain.js';
import { makeHumanoid, animateHumanoid } from '../engine/characters.js';
import { MAIN_ROAD, EAST_ROAD, WEST_PATH, LOC } from './layout.js';

export const NPCS = [];

export function addNpc(o) {
  // dois modelos alternados entre os NPCs
  const model = makeHumanoid({ race: o.race || 'humano', model: o.race === 'elfo' ? 'eve' : NPCS.length % 2 ? 'eve' : 'kachujin' });
  if (o.weapon) model.setWeapon(o.weapon);
  const n = { ...o, model, pos: { x: o.x, y: groundHeight(o.x, o.z), z: o.z }, yaw: o.yaw || 0, seg: 0, dir: 1, v: 0 };
  model.root.position.set(n.pos.x, n.pos.y, n.pos.z);
  model.root.rotation.y = n.yaw;
  G.scene.add(model.root);
  NPCS.push(n);
  return n;
}

// Viajantes: registros públicos que eles próprios escolheram exibir (nomes e marcos fictícios, provisórios)
export const TRAVELERS = [
  {
    name: 'Ilsa Varenne', race: 'humano', role: 'Artesã de ferramentas', cloth: '#7a4a36', weapon: 'martelo',
    gear: ['Martelo de oficina (bom)', 'Gibão de couro (usado)', 'Bolsa de viagem'],
    public: [
      ['Legado', 'Participou do reparo da ponte do Rio Largo, fornecendo pregos e ferragens.'],
      ['Feitos', 'Trabalho de bancada: 140 ferramentas fabricadas em três estações.'],
      ['Título exibido', 'Ofício de forja'],
    ],
    path: [...WEST_PATH].reverse().concat([[LOC.mercado.x - 6, LOC.mercado.z - 4]]),
  },
  {
    name: 'Tomek Arrudal', race: 'orc', role: 'Caravaneiro', cloth: '#5b4a6e', weapon: 'espada',
    gear: ['Espada de ferro (gasta)', 'Cota de malha (usada)', 'Cavalo de carga'],
    public: [
      ['História', 'Temporada de entregas: 32 cargas levadas ao Entreposto Alto pela Estrada Longa.'],
      ['Feitos', 'Derrota na Passagem Estreita. A carga foi recuperada dois dias depois.'],
      ['Título exibido', 'Pé na estrada'],
    ],
    path: EAST_ROAD,
  },
  {
    name: 'Siv de Aldren', race: 'elfo', role: 'Exploradora', cloth: '#476b4a', weapon: 'arco', hood: '#3a5a3c',
    gear: ['Arco de freixo (bom)', 'Gibão de couro (bom)'],
    public: [
      ['Descobertas', 'Registrou a ravina a oeste da Passagem antes de ela aparecer nos mapas do Vale.'],
      ['História', 'Voltou de duas Regiões Turbulentas. Adversários não identificados.'],
      ['Título exibido', 'Olhar do Observatório'],
    ],
    path: MAIN_ROAD.slice(0, Math.max(6, Math.round(MAIN_ROAD.length * 0.28))),
  },
];

export function initTravelers() {
  for (const t of TRAVELERS) {
    const [x, z] = t.path[0];
    const n = addNpc({ ...t, x, z, kind: 'traveler' });
    n.travel = t;
    n.seg = 0; n.dir = 1; n.wait = Math.random() * 3;
  }
}

export function updateNpcs(dt) {
  const P = G.player;
  for (const n of NPCS) {
    const near = Math.hypot(P.pos.x - n.pos.x, P.pos.z - n.pos.z) < 70;
    let v = 0;
    if (n.kind === 'traveler') {
      const path = n.travel.path;
      if (n.wait > 0) n.wait -= dt;
      else if (Math.hypot(P.pos.x - n.pos.x, P.pos.z - n.pos.z) < 2.6) { n.yaw = Math.atan2(P.pos.x - n.pos.x, P.pos.z - n.pos.z); }
      else {
        const nextI = n.seg + n.dir;
        if (nextI < 0 || nextI >= path.length) { n.dir *= -1; n.wait = 4; }
        else {
          const [tx, tz] = path[nextI];
          const dx = tx - n.pos.x, dz = tz - n.pos.z, d = Math.hypot(dx, dz);
          if (d < 0.6) n.seg = nextI;
          else { const s = Math.min(d, 3.4 * dt); n.pos.x += dx / d * s; n.pos.z += dz / d * s; n.yaw = Math.atan2(dx, dz); v = 3.4; }
        }
      }
      n.pos.y = groundHeight(n.pos.x, n.pos.z);
      n.model.root.position.set(n.pos.x, n.pos.y, n.pos.z);
    } else if (n.face && near) {
      const d = Math.hypot(P.pos.x - n.pos.x, P.pos.z - n.pos.z);
      if (d < 7) n.yaw += Math.atan2(Math.sin(Math.atan2(P.pos.x - n.pos.x, P.pos.z - n.pos.z) - n.yaw), Math.cos(Math.atan2(P.pos.x - n.pos.x, P.pos.z - n.pos.z) - n.yaw)) * Math.min(1, dt * 4);
    }
    n.model.root.rotation.y = n.yaw;
    n.model.root.visible = near;
    if (near) animateHumanoid(n.model, { dt, mps: v, attack: -1, dodge: -1, channel: n.work && Math.sin(G.time * 0.7 + n.pos.x) > 0.2 });
  }
}
