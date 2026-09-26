// Colisores do cenário a partir dos volumes já medidos no pacote de runtime.
//
// Nada aqui consulta triângulos: o pipeline mede a caixa envolvente de cada construção (juntando
// fachada, fundo e telhado) e este módulo transforma o resultado em caixas e círculos da mesma
// broadphase em grade que a demo já usa. As rotas ficam livres por construção — qualquer volume
// que invada a largura útil de uma estrada é registrado e descartado.
import { addBox, addCircle, testPoint } from '../game/collide.js';
import { PLACEMENTS, TURB_PLACEMENTS } from './runtime-manifest.js';
import { PLAN_Z_SIGN, TURB_RUNTIME_OFFSET } from './coordinates.js';
import { routeDistance, initRouteField } from './world-routes.js';
import { regionHeightAt } from './heightfield.js';

// Volumes que colidem como círculo (silhueta redonda) em vez de caixa.
const ROUND = new Set(['tower', 'rock', 'observatory', 'dead_tree']);
// Largura livre garantida em cada lado da estrada, além da meia-largura declarada.
const ROUTE_CLEARANCE = 1.2;

export const WORLD_COLLIDERS = [];
export const WORLD_BRIDGES = [];
export const COLLIDER_REPORT = { region: 0, turbulent: 0, skippedOnRoute: [], bridges: 0 };

function place(p, offset) {
  const x = p.plan[0] + offset.x;
  const z = PLAN_Z_SIGN * p.plan[1] + offset.z;
  const hw = p.half[0], hd = p.half[1];
  if (ROUND.has(p.kind)) {
    const r = Math.max(0.35, Math.min(hw, hd) * 0.92);
    addCircle(x, z, r);
    WORLD_COLLIDERS.push({ x, z, r, name: p.name, kind: p.kind });
  } else {
    addBox(x, z, hw, hd, 0);
    WORLD_COLLIDERS.push({ x, z, hw, hd, rot: 0, name: p.name, kind: p.kind });
  }
}

// Rampa de aproximação: sem ela o piso da ponte vira um degrau e a rota fica intransitável.
// A faixa lateral é generosa de propósito — a estrada tem 5 m de largura e o tabuleiro 4,5 m,
// então o eixo da rota nem sempre cai exatamente sobre a ponte.
const BRIDGE_APRON = 12;          // ao longo da travessia
const BRIDGE_APRON_SIDE = 4;      // para os lados

function bridgeOf(p, offset) {
  const x = p.plan[0] + offset.x;
  const z = PLAN_Z_SIGN * p.plan[1] + offset.z;
  // O tabuleiro é o topo da madeira, já corrigido pelo pipeline quando a fonte o deixou enterrado.
  const y = p.deck_final ?? p.deck ?? (p.base + (p.top - p.base) * 0.55);
  const hw = p.half[0] - 0.25, hd = p.half[1];
  const b = { x, z, hw, hd, y, rot: 0, name: p.name, apron: BRIDGE_APRON, apronSide: BRIDGE_APRON_SIDE, lift: p.lift || 0 };
  WORLD_BRIDGES.push(b);
  // Parapeitos em segmentos: impedem cair no rio, mas abrem onde a estrada cruza. Na ponte
  // secundária a rota chega em diagonal — um parapeito inteiriço fecharia a travessia.
  const SEG = 1.5;
  for (const side of [-1, 1]) {
    const px = x + side * (hw + 0.3);
    for (let oz = -hd + SEG; oz < hd; oz += SEG * 2) {
      const pz = z + oz;
      if (offset.x === 0 && routeDistance(px, pz) < 2.4) continue;
      addBox(px, pz, 0.3, SEG, 0);
      WORLD_COLLIDERS.push({ x: px, z: pz, hw: 0.3, hd: SEG, rot: 0, name: `${p.name}#parapeito`, kind: 'bridge' });
    }
  }
  COLLIDER_REPORT.bridges++;
  return b;
}

// Corredor livre que toda rota mantém, medido do eixo para cada lado.
const CORRIDOR_HALF = 3.2;
const FILL_STEP = 3;        // amostragem da pegada, em metros
const FILL_RADIUS = 1.7;    // raio de cada círculo de preenchimento

// A fonte coloca mina, caverna, acampamento, observatório e ruínas dos Ermos em cima da própria
// estrada — eles são o destino ou a passagem da rota. Em vez de descartar o colisor (o jogador
// atravessaria a construção) ou de fechá-la (a rota ficaria intransitável), o volume é preenchido
// por círculos pequenos, pulando as células que caem dentro do corredor da estrada.
function corridorColliders(p, x, z) {
  const hw = p.half[0], hd = p.half[1];
  let made = 0;
  for (let oz = -hd + FILL_STEP / 2; oz <= hd; oz += FILL_STEP) {
    for (let ox = -hw + FILL_STEP / 2; ox <= hw; ox += FILL_STEP) {
      const cx = x + ox, cz = z + oz;
      if (routeDistance(cx, cz) < CORRIDOR_HALF + FILL_RADIUS) continue;
      addCircle(cx, cz, FILL_RADIUS);
      WORLD_COLLIDERS.push({ x: cx, z: cz, r: FILL_RADIUS, name: `${p.name}#${made}`, kind: p.kind });
      made++;
    }
  }
  return made;
}

function apply(list, offset, counter) {
  for (const p of list) {
    if (p.bridge) { bridgeOf(p, offset); continue; }
    if (!p.collider) continue;
    const x = p.plan[0] + offset.x, z = PLAN_Z_SIGN * p.plan[1] + offset.z;
    if (offset.x === 0) {
      const reach = Math.max(p.half[0], p.half[1]) + ROUTE_CLEARANCE;
      const d = routeDistance(x, z);
      if (d < reach) {
        const made = corridorColliders(p, x, z);
        if (made) COLLIDER_REPORT[counter] += made;
        else COLLIDER_REPORT.skippedOnRoute.push(p.name);
        continue;
      }
    }
    place(p, offset);
    COLLIDER_REPORT[counter]++;
  }
}

export function buildWorldColliders() {
  initRouteField();
  WORLD_COLLIDERS.length = 0;
  WORLD_BRIDGES.length = 0;
  COLLIDER_REPORT.region = 0; COLLIDER_REPORT.turbulent = 0; COLLIDER_REPORT.bridges = 0;
  COLLIDER_REPORT.skippedOnRoute.length = 0;
  apply(PLACEMENTS, { x: 0, z: 0 }, 'region');
  apply(TURB_PLACEMENTS, TURB_RUNTIME_OFFSET, 'turbulent');
  return { colliders: WORLD_COLLIDERS, bridges: WORLD_BRIDGES, report: COLLIDER_REPORT };
}

// Ponto livre para vegetação, pontos de coleta e NPCs: sem colisor, fora da estrada e em chão firme.
export function isClearGround(x, z, radius = 0.6, routeMargin = 0) {
  if (testPoint(x, z, radius)) return false;
  if (routeMargin && routeDistance(x, z) < routeMargin) return false;
  const h = regionHeightAt(x, z);
  const slope = Math.abs(regionHeightAt(x + 1.5, z) - h) + Math.abs(regionHeightAt(x, z + 1.5) - h);
  return slope < 2.6;
}
