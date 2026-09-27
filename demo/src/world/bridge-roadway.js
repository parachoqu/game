// Tabuleiro de cantaria sobre o vão de pedra das pontes.
//
// A ponte de pedra do pacote tem a pista em corcova: 2,4 m mais baixa nas pontas do vão do que no
// meio. O tabuleiro de colisão do mundo é plano, com rampas até o terreno. Em vez de esticar a pedra
// (o que deformava parapeitos e borrava a textura), o vão fica como o modelo é e ganha por cima um
// tabuleiro montado aqui, com a forma exata da superfície de caminhada:
//   laje      topo = altura de caminhada (piso da ponte e rampas), de uma cabeceira à outra, onde o
//             terreno alcança a rampa; nas cabeceiras as bordas descem até o chão, como encontro;
//   paredes   uma caixa por segmento de parapeito do colisor (mesma posição, largura e comprimento),
//             1 m acima do piso, descendo até a pista antiga — os parapeitos em corcova ficam dentro.
// Texturas: recortes ladrilháveis das próprias texturas da ponte (`cantaria` e `calcamento`, feitos
// por `tools/build-extra-assets.mjs`), com coordenadas planas em metros: nada estica.
import * as THREE from 'three';
import { bridgeFrame } from './extra-seat.js';
import { regionHeightAt } from './heightfield.js';

const THICK = 0.4;              // espessura da laje
const RAIL = 1.0;               // altura das paredes acima do piso
// metros por repetição de cada recorte (proporção do recorte); o piso começa deslocado para a emenda
// do espelhamento não cair no eixo da ponte
const WALL_U = 6.4, WALL_V = 1.3;
const DECK_U = 5.8, DECK_V = 3.5, DECK_OFF = 0.37;

function builder() {
  const pos = [], nor = [], uv = [], idx = [];
  const _a = new THREE.Vector3(), _b = new THREE.Vector3(), _n = new THREE.Vector3();
  return {
    // quadrilátero p0-p1-p2-p3 com a normal `n` desejada; a ordem dos vértices se acerta sozinha
    quad(ps, uvs, n) {
      _a.subVectors(ps[1], ps[0]); _b.subVectors(ps[2], ps[0]); _n.crossVectors(_a, _b);
      const order = _n.dot(n) >= 0 ? [0, 1, 2, 3] : [0, 3, 2, 1];
      const k = pos.length / 3;
      for (const i of order) { pos.push(ps[i].x, ps[i].y, ps[i].z); nor.push(n.x, n.y, n.z); uv.push(uvs[i][0], uvs[i][1]); }
      idx.push(k, k + 1, k + 2, k, k + 2, k + 3);
    },
    geometry() {
      const g = new THREE.BufferGeometry();
      g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
      g.setAttribute('normal', new THREE.Float32BufferAttribute(nor, 3));
      g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
      g.setIndex(idx);
      g.computeBoundingSphere();
      return g;
    },
  };
}

// `oldTop(t)`: altura do topo da pista antiga em cada ponto do eixo (−Infinity onde não há ponte).
export function buildRoadway(name, mats, oldTop) {
  const f = bridgeFrame(name);
  if (!f || !mats.cantaria || !mats.calcamento) return null;
  const { b, alongZ, half, len, toWorld, walkY, tA, tB, rails, wall } = f;
  const V = (t, c, y) => { const [x, z] = toWorld(t, c); return new THREE.Vector3(x, y, z); };
  const U = alongZ ? new THREE.Vector3(0, 0, 1) : new THREE.Vector3(1, 0, 0);   // ao longo
  const C = alongZ ? new THREE.Vector3(1, 0, 0) : new THREE.Vector3(0, 0, 1);   // através
  const UP = new THREE.Vector3(0, 1, 0), DOWN = new THREE.Vector3(0, -1, 0);
  const ground = (t, c) => { const [x, z] = toWorld(t, c); return regionHeightAt(x, z); };

  // amostras ao longo do eixo, com as pontas do tabuleiro exatas
  const ts = [];
  for (let t = -tA; t < tB; t += 0.75) ts.push(t);
  ts.push(tB, -len, len);
  const T = [...new Set(ts.map((t) => Math.round(t * 1000) / 1000))].filter((t) => t >= -tA && t <= tB).sort((p, q) => p - q);
  const top = T.map((t) => walkY(t, 0));
  const inBox = (t) => Math.abs(t) <= len + 1e-6;

  const deck = builder(), stone = builder();
  // laje: topo, fundo e bordas (nas cabeceiras as bordas descem até o chão)
  for (let i = 0; i < T.length - 1; i++) {
    const t0 = T[i], t1 = T[i + 1], y0 = top[i], y1 = top[i + 1];
    const va = -half / DECK_V + DECK_OFF, vb = half / DECK_V + DECK_OFF;
    deck.quad([V(t0, -half, y0), V(t1, -half, y1), V(t1, half, y1), V(t0, half, y0)],
      [[t0 / DECK_U + DECK_OFF, va], [t1 / DECK_U + DECK_OFF, va], [t1 / DECK_U + DECK_OFF, vb], [t0 / DECK_U + DECK_OFF, vb]], UP);
    stone.quad([V(t0, -half, y0 - THICK), V(t1, -half, y1 - THICK), V(t1, half, y1 - THICK), V(t0, half, y0 - THICK)],
      [[t0 / WALL_U, -half / WALL_U], [t1 / WALL_U, -half / WALL_U], [t1 / WALL_U, half / WALL_U], [t0 / WALL_U, half / WALL_U]], DOWN);
    for (const side of [-1, 1]) {
      const c = side * half;
      const b0 = inBox(t0) && inBox(t1) ? y0 - THICK : Math.min(y0 - THICK, ground(t0, c) - 0.25);
      const b1 = inBox(t0) && inBox(t1) ? y1 - THICK : Math.min(y1 - THICK, ground(t1, c) - 0.25);
      const n = C.clone().multiplyScalar(side);
      stone.quad([V(t0, c, b0), V(t1, c, b1), V(t1, c, y1), V(t0, c, y0)],
        [[t0 / WALL_U, b0 / WALL_V], [t1 / WALL_U, b1 / WALL_V], [t1 / WALL_U, y1 / WALL_V], [t0 / WALL_U, y0 / WALL_V]], n);
    }
  }
  // testas da laje nas duas cabeceiras
  for (const [t, y, dir] of [[T[0], top[0], -1], [T.at(-1), top.at(-1), 1]]) {
    const g = Math.min(y - THICK, ground(t, 0) - 0.25);
    stone.quad([V(t, -half, g), V(t, half, g), V(t, half, y), V(t, -half, y)],
      [[-half / WALL_U, g / WALL_V], [half / WALL_U, g / WALL_V], [half / WALL_U, y / WALL_V], [-half / WALL_U, y / WALL_V]], U.clone().multiplyScalar(dir));
  }

  // paredes: uma caixa por segmento de parapeito do colisor
  const wallTop = b.y + RAIL;
  const bottomAt = (t) => {
    const o = oldTop(t);
    return Math.min(b.y - THICK, Number.isFinite(o) ? o - 0.1 : ground(t, half) - 0.25);
  };
  const joined = (r, t) => rails.some((q) => q !== r && Math.sign(q.c0 + q.c1) === Math.sign(r.c0 + r.c1) && Math.abs((t === r.t0 ? q.t1 : q.t0) - t) < 0.01);
  for (const r of rails) {
    const side = Math.sign(r.c0 + r.c1), outer = side > 0 ? r.c1 : r.c0, inner = side > 0 ? r.c0 : r.c1;
    const tm = (r.t0 + r.t1) / 2, seg = [[r.t0, bottomAt(r.t0)], [tm, bottomAt(tm)], [r.t1, bottomAt(r.t1)]];
    const nOut = C.clone().multiplyScalar(side), nIn = nOut.clone().negate();
    for (let i = 0; i < 2; i++) {
      const [t0, g0] = seg[i], [t1, g1] = seg[i + 1];
      stone.quad([V(t0, outer, g0), V(t1, outer, g1), V(t1, outer, wallTop), V(t0, outer, wallTop)],
        [[t0 / WALL_U, g0 / WALL_V], [t1 / WALL_U, g1 / WALL_V], [t1 / WALL_U, wallTop / WALL_V], [t0 / WALL_U, wallTop / WALL_V]], nOut);
    }
    stone.quad([V(r.t0, inner, b.y - 0.02), V(r.t1, inner, b.y - 0.02), V(r.t1, inner, wallTop), V(r.t0, inner, wallTop)],
      [[r.t0 / WALL_U, b.y / WALL_V], [r.t1 / WALL_U, b.y / WALL_V], [r.t1 / WALL_U, wallTop / WALL_V], [r.t0 / WALL_U, wallTop / WALL_V]], nIn);
    stone.quad([V(r.t0, r.c0, wallTop), V(r.t1, r.c0, wallTop), V(r.t1, r.c1, wallTop), V(r.t0, r.c1, wallTop)],
      [[r.t0 / WALL_U, r.c0 / WALL_U], [r.t1 / WALL_U, r.c0 / WALL_U], [r.t1 / WALL_U, r.c1 / WALL_U], [r.t0 / WALL_U, r.c1 / WALL_U]], UP);
    for (const [t, dir] of [[r.t0, -1], [r.t1, 1]]) {
      if (joined(r, t)) continue;
      const g = bottomAt(t);
      stone.quad([V(t, r.c0, g), V(t, r.c1, g), V(t, r.c1, wallTop), V(t, r.c0, wallTop)],
        [[r.c0 / WALL_U, g / WALL_V], [r.c1 / WALL_U, g / WALL_V], [r.c1 / WALL_U, wallTop / WALL_V], [r.c0 / WALL_U, wallTop / WALL_V]], U.clone().multiplyScalar(dir));
    }
  }

  const group = new THREE.Group();
  group.name = `tabuleiro:${name}`;
  for (const [bld, mat, tag] of [[deck, mats.calcamento, 'piso'], [stone, mats.cantaria, 'cantaria']]) {
    const m = new THREE.Mesh(bld.geometry(), mat);
    m.name = tag;
    m.castShadow = true; m.receiveShadow = true;
    group.add(m);
  }
  group.userData.extent = { tA, tB, half, wall };
  return group;
}
