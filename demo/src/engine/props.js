// Construtores procedurais de arquitetura e vegetação. Os elementos do mapa usam materiais PBR com texturas
// procedurais (engine/materials.js); personagens e NPCs mantêm o material estilizado (charMat).
import * as THREE from 'three';
import { surfaceMat, foliageMat, foliageDepth } from './materials.js';
import { TEX } from './textures.js';
export { OCC } from './materials.js';

export const COL = {
  pedra: '#9b9388', pedraEsc: '#6f6a63', adobe: '#b8906a', terra: '#a8764b', madeira: '#5a3f2c', madeiraClara: '#8a6443',
  metal: '#6e6a5e', bronze: '#9a7b45', cobalto: '#2f64a3', turquesa: '#2f9c95', ocre: '#b9793a', telha: '#9c4f35',
  tecido1: '#b24a3a', tecido2: '#d6b25a', tecido3: '#3e7a9a', tecido4: '#6c8a4a', escuro: '#2a2420',
};
// cor da paleta → superfície texturizada
const SURF_OF = {
  [COL.pedra]: 'alvenaria', [COL.pedraEsc]: 'alvenaria', [COL.adobe]: 'reboco', [COL.ocre]: 'reboco', [COL.terra]: 'reboco',
  [COL.madeira]: 'madeira', [COL.madeiraClara]: 'madeira', [COL.metal]: 'metal', [COL.bronze]: 'metal',
  [COL.cobalto]: 'telhas', [COL.turquesa]: 'telhas', [COL.telha]: 'telhas',
  [COL.tecido1]: 'tecido', [COL.tecido2]: 'tecido', [COL.tecido3]: 'tecido', [COL.tecido4]: 'tecido', [COL.escuro]: 'reboco',
};
export function mat(color, extra, surface) { return surfaceMat(surface || SURF_OF[color] || 'reboco', color, extra); }

export function mesh(geo, color, { cast = true, receive = true, extra, surface } = {}) {
  const m = new THREE.Mesh(geo, typeof color === 'string' ? mat(color, extra, surface) : color);
  m.castShadow = cast; m.receiveShadow = receive;
  return m;
}
function at(o, x, y, z) { o.position.set(x, y, z); return o; }
export function box(w, h, d, color, x = 0, y = 0, z = 0, opts) {
  const m = mesh(new THREE.BoxGeometry(w, h, d), color, opts);
  m.position.set(x, y, z); return m;
}
function cyl(rt, rb, h, seg, color, x = 0, y = 0, z = 0, opts) {
  const m = mesh(new THREE.CylinderGeometry(rt, rb, h, seg), color, opts);
  m.position.set(x, y, z); return m;
}
function gableRoof(w, d, rh, color) {
  const s = new THREE.Shape();
  s.moveTo(-w / 2, 0); s.lineTo(w / 2, 0); s.lineTo(0, rh); s.closePath();
  const g = new THREE.ExtrudeGeometry(s, { depth: d, bevelEnabled: false });
  g.translate(0, 0, -d / 2);
  return mesh(g, color, { surface: 'telhas' });
}

// Casa: corpo de pedra/adobe com telhado de cerâmica esmaltada
export function house({ w = 6, d = 5, h = 3.4, roof = COL.cobalto, wall = COL.adobe, base = COL.pedra, door = true } = {}) {
  const g = new THREE.Group();
  g.add(box(w + 0.3, 0.7, d + 0.3, base, 0, 0.35, 0));
  g.add(box(w, h, d, wall, 0, h / 2 + 0.3, 0));
  const r = gableRoof(w + 0.9, d + 0.8, h * 0.55, roof); r.position.y = h + 0.3; g.add(r);
  if (door) g.add(box(1.1, 1.9, 0.12, COL.madeira, 0, 1.25, d / 2 + 0.03));
  for (const sx of [-1, 1]) g.add(box(0.8, 0.7, 0.1, COL.escuro, sx * w * 0.3, h * 0.62 + 0.3, d / 2 + 0.03, { cast: false }));
  // faixa de padrão repetido (inscrições recorrentes)
  g.add(box(w + 0.05, 0.22, d + 0.05, COL.ocre, 0, h + 0.1, 0, { cast: false }));
  return g;
}

// Torre escalonada
export function tower({ r = 2.4, h = 12, tiers = 3, roof = COL.turquesa, wall = COL.pedra, seg = 14 } = {}) {
  const g = new THREE.Group();
  let y = 0, rr = r;
  const th = h / tiers;
  for (let i = 0; i < tiers; i++) {
    g.add(cyl(rr * 0.92, rr, th, seg, i % 2 ? COL.adobe : wall, 0, y + th / 2, 0));
    g.add(cyl(rr * 1.02, rr * 1.02, 0.3, seg, COL.ocre, 0, y + th - 0.1, 0, { cast: false }));
    y += th; rr *= 0.78;
  }
  g.add(at(mesh(new THREE.ConeGeometry(rr * 1.5, th * 0.9, seg), roof, { surface: 'telhas' }), 0, y + th * 0.45, 0));
  return g;
}

export function arch({ w = 5, h = 5, depth = 1.6, color = COL.pedra } = {}) {
  const g = new THREE.Group();
  const pw = 1.1;
  for (const sx of [-1, 1]) g.add(box(pw, h, depth, color, sx * (w / 2 + pw / 2), h / 2, 0));
  const a = mesh(new THREE.TorusGeometry(w / 2 + pw / 2, pw / 2, 6, 18, Math.PI), color);
  a.scale.z = depth / pw; a.position.y = h; g.add(a);
  g.add(box(w + pw * 2 + 0.4, 0.5, depth + 0.2, COL.ocre, 0, h + w / 2 + 0.6, 0));
  return g;
}

export function wall(len, h = 3.2, color = COL.pedra) {
  const g = new THREE.Group();
  g.add(box(len, h, 1.2, color, 0, h / 2, 0));
  const n = Math.max(1, Math.floor(len / 1.6));
  for (let i = 0; i < n; i += 2) g.add(box(0.8, 0.6, 1.3, color, -len / 2 + 0.4 + i * (len / n), h + 0.3, 0));
  return g;
}

export function stall(cloth = COL.tecido1) {
  const g = new THREE.Group();
  for (const [x, z] of [[-1.4, -1], [1.4, -1], [-1.4, 1], [1.4, 1]]) g.add(box(0.15, z < 0 ? 2.6 : 2.2, 0.15, COL.madeira, x, z < 0 ? 1.3 : 1.1, z));
  const aw = box(3.3, 0.08, 2.5, cloth, 0, 2.45, 0); aw.rotation.x = -0.18; g.add(aw);
  g.add(box(2.8, 0.9, 0.9, COL.madeiraClara, 0, 0.45, 0.9));
  g.add(box(0.6, 0.5, 0.6, COL.bronze, -0.8, 1.15, 0.9), box(0.5, 0.35, 0.5, COL.tecido2, 0.6, 1.07, 0.9));
  return g;
}

export function forge() {
  const g = new THREE.Group();
  g.add(box(3, 2.2, 2.4, COL.pedraEsc, 0, 1.1, -0.8));
  g.add(box(1.2, 0.9, 0.2, '#ff7a2a', 0, 0.9, 0.42, { extra: { emissive: '#ff5a10', emissiveIntensity: 1.4 }, cast: false }));
  g.add(cyl(0.45, 0.6, 3.2, 6, COL.pedraEsc, 0.8, 3.5, -1.2));
  g.add(box(1, 0.45, 0.5, COL.metal, 0, 0.9, 1.6), box(0.5, 0.7, 0.4, COL.madeira, 0, 0.35, 1.6));
  // cobertura
  for (const x of [-2.2, 2.2]) g.add(box(0.2, 3.2, 0.2, COL.madeira, x, 1.6, 2.4));
  const rf = box(5, 0.15, 3.4, COL.telha, 0, 3.3, 1.2); rf.rotation.x = 0.12; g.add(rf);
  return g;
}

export function stable() {
  const g = new THREE.Group();
  g.add(box(8, 0.2, 6, COL.madeiraClara, 0, 0.1, 0, { cast: false }));
  for (const x of [-3.8, 0, 3.8]) for (const z of [-2.8, 2.8]) g.add(box(0.25, 3.4, 0.25, COL.madeira, x, 1.7, z));
  const rf = gableRoof(8.8, 6.8, 1.8, COL.telha); rf.position.y = 3.4; rf.rotation.y = Math.PI / 2; g.add(rf);
  g.add(box(3, 1, 0.6, COL.madeira, -2, 0.5, -2.2), box(1.2, 1.1, 1.2, COL.tecido2, 2.6, 0.55, -2));
  return g;
}

export function crate(color = COL.madeiraClara, s = 1) { return box(s, s, s, color, 0, s / 2, 0); }
export function barrel() { return cyl(0.45, 0.4, 1, 8, COL.madeira, 0, 0.5, 0); }

export function banner(color, shape = 'rect') {
  const g = new THREE.Group();
  g.add(cyl(0.08, 0.1, 4.2, 5, COL.madeira, 0, 2.1, 0));
  const c = box(1.1, shape === 'rect' ? 1.5 : 1.1, 0.05, color, 0.6, 3.3, 0, { cast: false });
  if (shape === 'diamond') { c.rotation.z = Math.PI / 4; c.position.x = 0.75; }
  g.add(c);
  g.add(box(0.18, 0.18, 0.18, COL.bronze, 0, 4.25, 0));
  return g;
}

export function noticeBoard() {
  const g = new THREE.Group();
  for (const x of [-1, 1]) g.add(box(0.18, 2.4, 0.18, COL.madeira, x, 1.2, 0));
  g.add(box(2.4, 1.3, 0.12, COL.madeiraClara, 0, 1.7, 0));
  for (const [x, y, c] of [[-0.6, 1.9, '#e6dcc4'], [0.3, 1.6, '#d8cba8'], [0.7, 2, '#efe4c8']]) g.add(box(0.55, 0.45, 0.04, c, x, y, 0.08, { cast: false }));
  const rf = box(2.9, 0.1, 0.6, COL.telha, 0, 2.5, 0.05); rf.rotation.x = 0.3; g.add(rf);
  return g;
}

export function bridge(len, width = 6) {
  const g = new THREE.Group();
  g.add(box(width, 0.5, len, COL.pedra, 0, 0, 0));
  for (const sx of [-1, 1]) {
    g.add(box(0.4, 0.9, len, COL.pedraEsc, sx * (width / 2 - 0.2), 0.7, 0));
    for (let z = -len / 2 + 1; z < len / 2; z += 3) g.add(box(0.6, 1.3, 0.6, COL.pedra, sx * (width / 2 - 0.2), 0.9, z));
  }
  const a = mesh(new THREE.TorusGeometry(len / 3, 0.8, 4, 10, Math.PI), COL.pedraEsc);
  a.rotation.y = Math.PI / 2; a.position.y = -len / 3 + 0.1; a.scale.set(1, 0.55, width / 1.6); g.add(a);
  return g;
}

export function observatory() {
  const g = new THREE.Group();
  g.add(cyl(7.2, 7.6, 0.8, 10, COL.pedraEsc, 0, 0.4, 0));
  g.add(cyl(5.8, 6.2, 0.6, 10, COL.pedra, 0, 1.1, 0));
  // anel de colunas, algumas quebradas
  for (let i = 0; i < 10; i++) {
    const a = i / 10 * Math.PI * 2, hh = [5, 5, 2.1, 5, 3.2, 5, 5, 1.2, 5, 4][i];
    g.add(box(0.8, hh, 0.8, COL.pedra, Math.cos(a) * 5.3, 1.4 + hh / 2, Math.sin(a) * 5.3));
  }
  const dome = mesh(new THREE.SphereGeometry(5.6, 12, 6, 0, Math.PI * 1.3, 0, Math.PI / 2), COL.turquesa, { extra: { side: THREE.DoubleSide } });
  dome.position.y = 6.3; dome.rotation.y = 0.6; g.add(dome);
  // instrumento: anel de bronze inclinado e luneta
  const ring = mesh(new THREE.TorusGeometry(2.2, 0.14, 5, 20), COL.bronze); ring.position.set(0, 3.6, 0); ring.rotation.set(1.1, 0.4, 0); g.add(ring);
  const ring2 = mesh(new THREE.TorusGeometry(1.7, 0.1, 5, 18), COL.bronze); ring2.position.set(0, 3.6, 0); ring2.rotation.set(0.2, 1.2, 0.4); g.add(ring2);
  const scope = cyl(0.25, 0.4, 3.6, 7, COL.metal, 0, 3.4, 0); scope.rotation.set(-0.9, 0, 0.3); g.add(scope);
  g.add(cyl(0.3, 0.5, 2, 6, COL.pedraEsc, 0, 2.3, 0));
  return g;
}

export function watchtowerRuin() {
  const g = new THREE.Group();
  g.add(box(4, 5.5, 4, COL.pedra, 0, 2.75, 0));
  g.add(box(2.2, 2.5, 4, COL.pedra, -0.9, 6.8, 0));
  g.add(box(4, 2.2, 1, COL.pedraEsc, 0, 6.6, 1.5));
  g.add(box(1.2, 2, 0.2, COL.escuro, 0, 1, 2.02, { cast: false }));
  for (const [x, z, s] of [[2.8, 1.5, 0.9], [3.4, -0.6, 0.6], [-2.9, 2.2, 0.7]]) g.add(box(s, s * 0.6, s, COL.pedraEsc, x, s * 0.3, z));
  return g;
}

export function tent(color = COL.madeiraClara) {
  const g = new THREE.Group();
  g.add(at(mesh(new THREE.ConeGeometry(2.2, 3.2, 5), color), 0, 1.6, 0));
  g.add(box(0.1, 3.8, 0.1, COL.madeira, 0, 1.9, 0));
  return g;
}
export function bonfire() {
  const g = new THREE.Group();
  for (let i = 0; i < 6; i++) { const a = i / 6 * Math.PI * 2; g.add(box(0.35, 0.3, 0.35, COL.pedraEsc, Math.cos(a) * 0.8, 0.15, Math.sin(a) * 0.8)); }
  const f = mesh(new THREE.ConeGeometry(0.55, 1.3, 5), '#ff8a30', { extra: { emissive: '#ff5a10', emissiveIntensity: 1.5 }, cast: false });
  f.position.y = 0.7; f.name = 'flame'; g.add(f);
  return g;
}
export function palisade(len) {
  const g = new THREE.Group();
  for (let x = -len / 2; x <= len / 2; x += 0.7) {
    const s = box(0.4, 2.4 + Math.sin(x * 7) * 0.3, 0.4, COL.madeira, x, 1.2, 0);
    g.add(s);
  }
  return g;
}

export function ruinColumn(h = 4) {
  const g = new THREE.Group();
  g.add(box(1.4, 0.5, 1.4, COL.pedraEsc, 0, 0.25, 0));
  g.add(cyl(0.5, 0.55, h, 12, COL.pedra, 0, 0.5 + h / 2, 0));
  if (h > 3.5) g.add(box(1.3, 0.4, 1.3, COL.pedra, 0, 0.7 + h, 0));
  return g;
}

export function portalMesh() {
  const g = new THREE.Group();
  for (const sx of [-1, 1]) { const s = box(0.9, 5.5, 0.9, '#3a3346', sx * 2.6, 2.75, 0); s.rotation.z = sx * -0.08; g.add(s); }
  const ring = mesh(new THREE.TorusGeometry(2.3, 0.22, 6, 24), '#c8a8ff', { extra: { emissive: '#8a5cff', emissiveIntensity: 1.6 }, cast: false });
  ring.position.y = 3; g.add(ring);
  const disc = new THREE.Mesh(new THREE.CircleGeometry(2.15, 24), new THREE.MeshBasicMaterial({ color: '#6b3fd6', transparent: true, opacity: 0.6, side: THREE.DoubleSide, depthWrite: false }));
  disc.position.y = 3; disc.name = 'disc'; g.add(disc);
  for (let i = 0; i < 5; i++) {
    const sh = mesh(new THREE.OctahedronGeometry(0.28), '#d9c4ff', { extra: { emissive: '#9a6cff', emissiveIntensity: 1.2 }, cast: false });
    sh.userData.a = i / 5 * Math.PI * 2; sh.name = 'shard'; g.add(sh);
  }
  return g;
}

export function shrine() {
  const g = new THREE.Group();
  g.add(cyl(1.3, 1.6, 0.8, 6, '#4b4058', 0, 0.4, 0));
  g.add(cyl(0.5, 0.7, 1.2, 6, '#5a4d6b', 0, 1.4, 0));
  const c = mesh(new THREE.OctahedronGeometry(0.55), '#c9a8ff', { extra: { emissive: '#8f5cff', emissiveIntensity: 1.3 }, cast: false });
  c.position.y = 2.7; c.name = 'crystal'; g.add(c);
  return g;
}
export function extractionStone() {
  const g = new THREE.Group();
  const s = box(1.4, 6, 1, '#3d3a4a', 0, 3, 0); s.rotation.y = 0.3; g.add(s);
  for (let i = 0; i < 4; i++) g.add(box(0.5, 0.3, 0.05, '#9ff2e4', 0.15, 1.5 + i * 1.1, 0.55, { extra: { emissive: '#4fe0c8', emissiveIntensity: 1.4 }, cast: false }));
  const ring = new THREE.Mesh(new THREE.RingGeometry(3, 3.4, 32).rotateX(-Math.PI / 2), new THREE.MeshBasicMaterial({ color: '#4fe0c8', transparent: true, opacity: 0.55, depthWrite: false }));
  ring.position.y = 0.15; g.add(ring);
  return g;
}

export function resourceNode(kind) {
  const g = new THREE.Group();
  const add = (m, cast = true) => { m.castShadow = cast; m.receiveShadow = true; g.add(m); return m; };
  if (kind === 'minerio') {
    const r = add(new THREE.Mesh(GEO().rocks[1], surfaceMat('rocha', '#8a8279'))); r.scale.set(1.35, 1.1, 1.35); r.position.y = 0.25;
    for (let i = 0; i < 5; i++) { const v = add(new THREE.Mesh(new THREE.OctahedronGeometry(0.24), mat('#c07a45', null, 'metal')), false); v.position.set(Math.cos(i * 1.7) * 0.95, 0.55 + (i % 2) * 0.4, Math.sin(i * 1.7) * 0.95); v.rotation.set(i, i * 2, 0); }
  } else if (kind === 'madeira') {
    g.add(treeMeshes('broad'));
    g.add(box(1.0, 0.16, 0.16, '#e6d9a8', 0, 1.3, 0.3, { cast: false, surface: 'tecido' }));   // marca de corte
  } else if (kind === 'erva') {
    const m = add(new THREE.Mesh(GEO().grass, M().herb), false); m.scale.set(1.4, 1.3, 1.4);
    for (let i = 0; i < 4; i++) { const f = add(new THREE.Mesh(new THREE.OctahedronGeometry(0.09), mat('#f3f0a0', { emissive: '#d8d060', emissiveIntensity: 0.9 })), false); f.position.set(Math.cos(i * 1.6) * 0.3, 0.8 + (i % 2) * 0.15, Math.sin(i * 1.6) * 0.3); }
  } else if (kind === 'cristal') {
    const r = add(new THREE.Mesh(GEO().rocks[2], surfaceMat('rocha', '#5f5866'))); r.scale.set(0.9, 0.7, 0.9); r.position.y = 0.15;
    for (let i = 0; i < 6; i++) { const c = add(new THREE.Mesh(new THREE.ConeGeometry(0.26, 1.5 + (i % 3) * 0.55, 6), M().crystal)); c.position.set(Math.cos(i * 1.1) * 0.45, 1, Math.sin(i * 1.1) * 0.45); c.rotation.set(Math.cos(i) * 0.45, 0, Math.sin(i) * 0.45); }
  }
  return g;
}

// ---------- vegetação e rochas realistas ----------
// Sementes locais: a variação visual não consome a sequência de geração do jogo.
function rng(seed) { return () => { seed = (seed * 16807) % 2147483647; return seed / 2147483647; }; }
const V = (x, y, z) => new THREE.Vector3(x, y, z);
const FULL_UV = { u0: 0, v0: 0, u1: 1, v1: 1, aspect: 1 };
function region(kind, i) {
  const regions = TEX.foliageRegions?.[kind];
  return regions?.length ? regions[i % regions.length] : FULL_UV;
}
// Cada cartão recorta uma folha/ramo do atlas, nunca a prancha inteira.
function cards(list) {
  const pos = [], nor = [], uv = [], idx = [];
  for (const k of list) {
    const b = pos.length / 3, tex = k.uv || FULL_UV;
    for (const [sx, sy, tu, tv] of [[-1, -1, 0, 0], [1, -1, 1, 0], [1, 1, 1, 1], [-1, 1, 0, 1]]) {
      pos.push(k.c.x + k.r.x * sx + k.u.x * sy, k.c.y + k.r.y * sx + k.u.y * sy, k.c.z + k.r.z * sx + k.u.z * sy);
      nor.push(k.n.x, k.n.y, k.n.z);
      const su = tex.rotation === 'cw' ? 1 - tv : tu, sv = tex.rotation === 'cw' ? tu : tv;
      uv.push(tex.u0 + (tex.u1 - tex.u0) * su, tex.v0 + (tex.v1 - tex.v0) * sv);
    }
    idx.push(b, b + 1, b + 2, b, b + 2, b + 3);
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('normal', new THREE.Float32BufferAttribute(nor, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setIndex(idx); g.computeBoundingSphere();
  // Margem conservadora para o balanço no shader, inclusive nos mapas de sombra.
  g.boundingSphere.radius += 0.65;
  return g;
}
function joinGeos(list) {
  const parts = list.map((g) => (g.index ? g.toNonIndexed() : g));
  let n = 0; for (const p of parts) n += p.attributes.position.count;
  const pos = new Float32Array(n * 3), nor = new Float32Array(n * 3), uv = new Float32Array(n * 2);
  let o = 0;
  for (const p of parts) {
    pos.set(p.attributes.position.array, o * 3); nor.set(p.attributes.normal.array, o * 3);
    if (p.attributes.uv) uv.set(p.attributes.uv.array, o * 2);
    o += p.attributes.position.count;
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(pos, 3));
  g.setAttribute('normal', new THREE.BufferAttribute(nor, 3));
  g.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
  for (let i = 0; i < parts.length; i++) { parts[i].dispose(); if (parts[i] !== list[i]) list[i].dispose(); }
  g.computeBoundingSphere();
  return g;
}
// Troncos e ramificações com afunilamento contínuo e seção discretamente irregular.
function curvedLimb(points, r0, r1, rings = 7, sides = 8, seed = 1) {
  const curve = new THREE.CatmullRomCurve3(points), pos = [], uv = [], idx = [];
  const axis = V(0, 0, 1), along = V(0, 1, 0), side = V(1, 0, 0), normal = V(0, 0, 1);
  for (let j = 0; j <= rings; j++) {
    const t = j / rings, p = curve.getPoint(t);
    curve.getTangent(t, along).normalize();
    side.crossVectors(along, Math.abs(along.z) > 0.92 ? V(0, 1, 0) : axis).normalize();
    normal.crossVectors(side, along).normalize();
    const radius = r0 * Math.pow(1 - t, 1.12) + r1 * t;
    for (let k = 0; k <= sides; k++) {
      const a = k / sides * Math.PI * 2;
      const rr = radius * (1 + 0.055 * Math.sin(a * 3 + seed) + 0.025 * Math.sin(a * 5 + t * 4));
      pos.push(p.x + (side.x * Math.cos(a) + normal.x * Math.sin(a)) * rr,
        p.y + (side.y * Math.cos(a) + normal.y * Math.sin(a)) * rr,
        p.z + (side.z * Math.cos(a) + normal.z * Math.sin(a)) * rr);
      uv.push(k / sides, t);
      if (j < rings && k < sides) {
        const b = j * (sides + 1) + k;
        idx.push(b, b + sides + 1, b + 1, b + 1, b + sides + 1, b + sides + 2);
      }
    }
  }
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  g.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  g.setIndex(idx); g.computeVertexNormals();
  return g;
}
function n3(x, y, z, s) {
  const h = (i, j, k) => { let t = Math.imul(i, 374761393) ^ Math.imul(j, 668265263) ^ Math.imul(k, 2147483647) ^ Math.imul(s, 1442695041); t = Math.imul(t ^ (t >>> 13), 1274126177); return ((t ^ (t >>> 16)) >>> 0) / 4294967296; };
  const xi = Math.floor(x), yi = Math.floor(y), zi = Math.floor(z), xf = x - xi, yf = y - yi, zf = z - zi;
  const u = xf * xf * (3 - 2 * xf), v = yf * yf * (3 - 2 * yf), w = zf * zf * (3 - 2 * zf);
  const l = (a, b, t) => a + (b - a) * t;
  return l(l(l(h(xi, yi, zi), h(xi + 1, yi, zi), u), l(h(xi, yi + 1, zi), h(xi + 1, yi + 1, zi), u), v),
    l(l(h(xi, yi, zi + 1), h(xi + 1, yi, zi + 1), u), l(h(xi, yi + 1, zi + 1), h(xi + 1, yi + 1, zi + 1), u), v), w);
}
function rockGeo(seed, detail = 3) {
  const src = new THREE.IcosahedronGeometry(1, detail), p = src.attributes.position;
  const map = new Map(), verts = [], idx = [];
  for (let i = 0; i < p.count; i++) {
    const key = `${p.getX(i).toFixed(5)},${p.getY(i).toFixed(5)},${p.getZ(i).toFixed(5)}`;
    if (!map.has(key)) { map.set(key, verts.length / 3); verts.push(p.getX(i), p.getY(i), p.getZ(i)); }
    idx.push(map.get(key));
  }
  const a = seed * 0.39, ca = Math.cos(a), sa = Math.sin(a);
  for (let i = 0; i < verts.length; i += 3) {
    const x = verts[i], y = verts[i + 1], z = verts[i + 2];
    const d = 1 + (n3(x * 1.6 + 5, y * 1.6, z * 1.6, seed) - 0.5) * 0.44;
    let nx = x * d, nz = z * d, ny = y * d * (0.61 + seed % 3 * 0.09);
    // Faces de fratura oblíquas e estratos finos; a base permanece enterrada.
    const cut = nx * ca + nz * sa + ny * 0.2;
    if (cut > 0.68) { nx -= (cut - 0.68) * ca; nz -= (cut - 0.68) * sa; }
    const cut2 = nx * -sa + nz * ca - ny * 0.3;
    if (cut2 > 0.82) { nx += (cut2 - 0.82) * sa; nz -= (cut2 - 0.82) * ca; }
    const strata = Math.sin((ny + nx * 0.24) * 21 + seed) * 0.028;
    nx *= 1 + strata; nz *= 1 + strata;
    if (ny < -0.22) ny = -0.22 + (ny + 0.22) * 0.14;
    verts[i] = nx; verts[i + 1] = ny + 0.22; verts[i + 2] = nz;
  }
  src.dispose();
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.Float32BufferAttribute(verts, 3));
  g.setIndex(idx); g.computeVertexNormals(); g.computeBoundingSphere();
  return g;
}
function rootFlares(parts, seed, radius, detailed) {
  const count = detailed ? 5 : 3;
  for (let i = 0; i < count; i++) {
    const a = seed + i * 2.39996, dir = V(Math.cos(a), 0, Math.sin(a));
    parts.push(curvedLimb([V(dir.x * 0.10, 0.34, dir.z * 0.10), V(dir.x * radius * 0.58, 0.08, dir.z * radius * 0.58), V(dir.x * radius, -0.12, dir.z * radius)], 0.13, 0.018, detailed ? 4 : 2, 5, i));
  }
}
function treeGeo(kind, variant, detailed) {
  const R = rng(127 + variant * 701 + (kind === 'pine' ? 211 : kind === 'dead' ? 907 : 0));
  const woody = [], foliage = [], pine = kind === 'pine', dead = kind === 'dead';
  const h = pine ? 7.6 + R() * 0.6 : dead ? 4.6 + R() * 0.7 : 5.2 + R() * 0.8;
  const bend = V((R() - 0.5) * 0.48, 0, (R() - 0.5) * 0.48);
  const base = pine ? 0.36 : dead ? 0.33 : 0.43;
  const trunkAt = (t) => V(bend.x * t * t + Math.sin(t * 4 + variant) * 0.045 * t, h * t, bend.z * t * t);
  woody.push(curvedLimb([V(0, -0.23, 0), trunkAt(0.27), trunkAt(0.63), trunkAt(1)], base, 0.045, detailed ? 11 : 5, detailed ? 10 : 6, variant));
  rootFlares(woody, variant, base + 0.035, detailed);
  if (pine) {
    for (let lv = 0; lv < 9; lv++) {
      const frac = 0.22 + lv * 0.078, start = trunkAt(frac), length = (1 - frac) * 2.6 + 0.28;
      const nb = 5;
      for (let b = 0; b < nb; b++) {
        const angle = b / nb * Math.PI * 2 + lv * 2.39996 + variant + R() * 0.25;
        const dir = V(Math.cos(angle), 0, Math.sin(angle)), side = V(dir.z, 0, -dir.x);
        const reach = length * (0.75 + R() * 0.3);
        const end = start.clone().addScaledVector(dir, reach).add(V(0, -0.13 + R() * 0.19, 0));
        woody.push(curvedLimb([start, start.clone().lerp(end, 0.5).add(V(0, -0.11, 0)), end], 0.06 * (1 - frac) + 0.018, 0.012, detailed ? 4 : 2, 5, b));
        const FR = rng(973 + variant * 201 + lv * 53 + b * 11);
        const clusters = 4;
        for (let j = 0; j < clusters; j++) {
          const tex = region('needles', lv * 7 + b + j), t = 0.30 + j / clusters * 0.70;
          const c = start.clone().lerp(end, t).addScaledVector(side, (FR() - 0.5) * 0.28);
          if (!detailed && j % 2 === 0) continue;
          const width = (detailed ? 0.86 : 1.15) * (1 - frac * 0.45);
          const radial = dir.clone().multiplyScalar(width / 2);
          const tangent = side.clone().multiplyScalar(width / Math.max(0.7, tex.aspect) / 2);
          tangent.y = Math.sin(j * 1.7 + angle) * width * 0.18;
          foliage.push({ c, r: radial, u: tangent, n: radial.clone().cross(tangent).normalize(), uv: tex });
          if (detailed || j === 3) foliage.push({ c: c.clone().add(V(0, 0.035, 0)), r: radial.clone(), u: V(0, width / Math.max(0.7, tex.aspect) * 0.44, 0), n: radial.clone().cross(V(0, 1, 0)).normalize(), uv: tex });
        }
      }
    }
    // Ponteiro vivo estreito, ligado ao final do tronco (sem um metro de haste nua).
    for (let k = 0; k < 3; k++) {
      const a = k * Math.PI / 3 + variant, tex = region('needles', k), length = 0.72 - k * 0.09;
      const r = V(0, length / 2, 0), u = V(Math.cos(a), 0, Math.sin(a)).multiplyScalar(length / Math.max(0.7, tex.aspect) / 2);
      foliage.push({ c: trunkAt(0.94 + k * 0.012), r, u, n: r.clone().cross(u).normalize(), uv: tex });
    }
  } else {
    const limbs = dead ? 7 : 9;
    for (let b = 0; b < limbs; b++) {
      const angle = b * 2.39996 + variant * 0.8 + R() * 0.35;
      const frac = 0.38 + b / limbs * 0.42, start = trunkAt(frac);
      const reach = (dead ? 1.2 : 1.5) * (0.76 + R() * 0.48) * (1 - b / limbs * 0.30);
      const dir = V(Math.cos(angle), 0, Math.sin(angle));
      const tip = start.clone().addScaledVector(dir, reach).add(V(0, 0.35 + R() * 0.65, 0));
      const mid = start.clone().lerp(tip, 0.52).add(V(0, -0.12, 0));
      woody.push(curvedLimb([start, mid, tip], 0.10 * (1 - frac) + 0.04, 0.018, detailed ? 5 : 2, detailed ? 7 : 5, b));
      if (dead) continue;
      const FR = rng(387 + variant * 409 + b * 107);
      for (let j = 0; j < 3; j++) {
        const a = angle + (j - 1) * 0.7;
        const end = tip.clone().add(V(Math.cos(a) * 0.5, 0.15 + FR() * 0.38, Math.sin(a) * 0.5));
        if (detailed) woody.push(curvedLimb([mid.clone().lerp(tip, 0.5), tip, end], 0.036, 0.009, 3, 5, j));
        const sprayTips = [];
        for (let k = 0; k < 4; k++) {
          const twigAngle = a + (k - 1.5) * 0.67;
          const twigEnd = end.clone().add(V(Math.cos(twigAngle) * (0.34 + FR() * 0.2), 0.08 + FR() * 0.22, Math.sin(twigAngle) * (0.34 + FR() * 0.2)));
          sprayTips.push(twigEnd);
          if (detailed) woody.push(curvedLimb([end, end.clone().lerp(twigEnd, 0.45).add(V(0, 0.04, 0)), twigEnd], 0.012, 0.003, 2, 4, k));
        }
        for (let n = 0; n < 48; n++) {
          const spray = Math.floor(n / 12), along = (n % 12) / 12 * 0.9 + 0.1, twigEnd = sprayTips[spray];
          const normal = V(FR() * 2 - 1, FR() * 1.2 + 0.12, FR() * 2 - 1).normalize();
          const twigDir = twigEnd.clone().sub(end).normalize(), leafDir = V(-twigDir.z, 0.35 + FR() * 0.3, twigDir.x).normalize().multiplyScalar(n % 2 ? 1 : -1);
          const tex = region('leaves', b * 13 + n + j), length = 0.14 + FR() * 0.11;
          const c = end.clone().lerp(twigEnd, along).addScaledVector(leafDir, length * 0.43);
          const side = leafDir.clone().cross(normal).normalize();
          normal.copy(side).cross(leafDir).normalize();
          if (!detailed && n % 2 === 0) continue;
          foliage.push({ c, r: side.multiplyScalar(length * tex.aspect / 2), u: leafDir.multiplyScalar(length / 2), n: normal, uv: tex });
        }
      }
    }
  }
  return { trunk: joinGeos(woody), foliage: foliage.length ? cards(foliage) : null };
}
function bushGeo(variant, detailed) {
  const R = rng(809 + variant * 67), list = [], stems = [];
  const branches = Array.from({ length: 7 }, (_, i) => {
    const a = i * 2.39996 + variant, end = V(Math.cos(a) * 0.58, 0.47 + R() * 0.3, Math.sin(a) * 0.58);
    if (detailed) stems.push(curvedLimb([V(0, -0.1, 0), V(end.x * 0.3, 0.35, end.z * 0.3), end], 0.026, 0.004, 3, 4, i));
    return end;
  });
  for (let i = 0; i < 216; i++) {
    const end = branches[i % branches.length], angle = Math.atan2(end.z, end.x) + (R() - 0.5) * 0.7;
    const t = 0.38 + R() * 0.62, c = end.clone().multiplyScalar(t).add(V((R() - 0.5) * 0.3, (R() - 0.5) * 0.22, (R() - 0.5) * 0.3));
    const n = V(Math.cos(angle) * 0.5, 0.8, Math.sin(angle) * 0.5).normalize();
    const side = V(-Math.sin(angle), 0, Math.cos(angle)), up = n.clone().cross(side).normalize();
    const tex = region('leaves', i), length = 0.14 + R() * 0.09;
    if (!detailed && i % 2 === 0) continue;
    list.push({ c, r: side.multiplyScalar(length * tex.aspect / 2), u: up.multiplyScalar(length / 2), n, uv: tex });
  }
  return { foliage: cards(list), stems: detailed ? joinGeos(stems) : null };
}
let geoCache = null;
function GEO() {
  if (geoCache) return geoCache;
  const grassCards = [0, 1, 2].map((i) => { const a = i / 3 * Math.PI; return { c: V(0, 0.38, 0), r: V(Math.cos(a) * 0.45, 0, Math.sin(a) * 0.45), u: V(0, 0.38, 0), n: V(0, 1, 0) }; });
  geoCache = { trees: {}, bushes: [], grass: cards(grassCards), rocks: [], rocksFar: [], shard: new THREE.ConeGeometry(0.3, 1.8, 5).translate(0, 0.7, 0) };
  for (const kind of ['pine', 'broad', 'dead']) geoCache.trees[kind] = Array.from({ length: 3 }, (_, i) => ({ near: treeGeo(kind, i, true), far: treeGeo(kind, i, false) }));
  for (let i = 0; i < 3; i++) {
    geoCache.bushes.push({ near: bushGeo(i, true), far: bushGeo(i, false) });
    geoCache.rocks.push(rockGeo([3, 17, 29][i], 4)); geoCache.rocksFar.push(rockGeo([3, 17, 29][i], 1));
  }
  return geoCache;
}
let matCache2 = null;
function M() {
  if (matCache2) return matCache2;
  matCache2 = {
    bark: surfaceMat('cascaNatural', '#f2ede5'), pineBark: surfaceMat('cascaPinheiro', '#f3efe8'), deadBark: surfaceMat('cascaNatural', '#c4bbb0'),
    rock: surfaceMat('rochaNatural', '#efede8'), darkRock: surfaceMat('rochaEscura', '#ebe7e5'),
    needles: foliageMat(TEX.needles, { wind: 0.7 }), needlesDepth: foliageDepth(TEX.needles, 0.7),
    leaves: foliageMat(TEX.leaves, { wind: 1 }), leavesDepth: foliageDepth(TEX.leaves, 1),
    bush: foliageMat(TEX.leaves, { wind: 0.5, color: '#e4edcf' }),
    grass: foliageMat(TEX.grass, { grass: true }), herb: foliageMat(TEX.herb, { wind: 0.6 }),
    crystal: new THREE.MeshStandardMaterial({ color: '#9fe2f2', emissive: '#3fa8c8', emissiveIntensity: 0.9, roughness: 0.18, metalness: 0.1 }),
    shard: new THREE.MeshStandardMaterial({ color: '#7a58c0', emissive: '#5a2aa0', emissiveIntensity: 0.8, roughness: 0.22 }),
  };
  return matCache2;
}
// Provedor externo de protótipos (kit de natureza). Quando preenchido, ele responde primeiro;
// os construtores procedurais abaixo continuam como fallback se o kit não carregar.
export const KIT = { parts: null, partsAt: null, impostor: null, collider: null, variants: null, has: null, metrics: null, tint: null };

// [geometria, material, projeta sombra, profundidade com vento, cor por instância]
function parts(kind, variant = 0, near = true, dark = false) {
  if (KIT.parts) { const p = KIT.parts(kind, variant, near, dark); if (p) return p; }
  // fallback procedural: ele só conhece três variantes e os tipos originais da demo
  variant = ((variant | 0) % 3 + 3) % 3;
  const geo = GEO(), m = M(), lod = near ? 'near' : 'far';
  if (!geo.trees[kind] && !['bush', 'grass', 'crystalShard', 'rock'].includes(kind)) kind = 'rock';
  if (geo.trees[kind]) {
    const t = geo.trees[kind][variant][lod];
    return [[t.trunk, kind === 'pine' ? m.pineBark : kind === 'dead' ? m.deadBark : m.bark, true],
    ...(t.foliage ? [[t.foliage, kind === 'pine' ? m.needles : m.leaves, true, kind === 'pine' ? m.needlesDepth : m.leavesDepth, true]] : [])];
  }
  if (kind === 'bush') {
    const b = geo.bushes[variant][lod];
    return [[b.foliage, m.bush, false, null, true], ...(b.stems ? [[b.stems, m.bark, false]] : [])];
  }
  if (kind === 'grass') return [[geo.grass, m.grass, false, null, true]];
  if (kind === 'crystalShard') return [[geo.shard, m.shard, false]];
  return [[(near ? geo.rocks : geo.rocksFar)[variant], dark ? m.darkRock : m.rock, true, null, true]];
}
// Peças de um nível exato (0 perto, 1 médio, 2 longe) para o LOD por instância. Sem o kit, os
// protótipos procedurais só têm dois níveis: 0 e 1 usam o de perto, 2 o de longe.
export function levelParts(kind, variant = 0, lv = 0) {
  if (KIT.partsAt) { const p = KIT.partsAt(kind, variant, lv); if (p) return p; }
  return parts(kind, variant, lv < 2);
}
// Pontos de corte usam a mesma árvore fixa; não participam das trocas por distância.
export function treeMeshes(kind) {
  const g = new THREE.Group();
  for (const [geo, material, cast, depth] of parts(kind)) {
    const m = new THREE.Mesh(geo, material);
    m.castShadow = cast; m.receiveShadow = true;
    if (depth) m.customDepthMaterial = depth;
    g.add(m);
  }
  return g;
}
// Raio de TODA a geometria, não apenas o tronco; inclui copas e seu movimento.
const footprintCache = new Map();
export function natureFootprint(kind, scale = 1) {
  if (!footprintCache.has(kind)) {
    let radius = 0;
    const nv = KIT.variants ? (KIT.variants(kind) || 3) : 3;
    for (let variant = 0; variant < nv; variant++) for (const near of [true, false]) {
      for (const [g] of parts(kind, variant, near)) {
        const p = g.attributes.position;
        for (let i = 0; i < p.count; i++) radius = Math.max(radius, Math.hypot(p.getX(i), p.getZ(i)));
      }
    }
    footprintCache.set(kind, radius + (['pine', 'broad', 'bush'].includes(kind) ? 0.55 : kind === 'grass' ? 0.25 : 0.02));
  }
  return footprintCache.get(kind) * scale;
}
export function natureCollider(kind, variant = 0) {
  if (KIT.collider) { const r = KIT.collider(kind, variant); if (r != null) return r; }
  if (kind === 'broad') return 0.49;
  if (kind === 'pine') return 0.42;
  if (kind === 'dead') return 0.39;
  if (kind === 'rock') {
    const p = GEO().rocks[variant].attributes.position;
    let radius = 0;
    for (let i = 0; i < p.count; i++) if (p.getY(i) < 0.58) radius = Math.max(radius, Math.hypot(p.getX(i), p.getZ(i)));
    return radius;
  }
  return 0;
}
export const QREG = {
  grass: [], grassOn: true, nature: [], natureShadow: true,
  natureFar: 360, canopyFar: 700, groundFar: 140,
};
export function updateGrassLOD(cam, far) {
  for (const r of QREG.grass) {
    const bs = r.mesh.boundingSphere;
    r.mesh.visible = QREG.grassOn && Math.hypot(bs.center.x - cam.x, bs.center.z - cam.z) - bs.radius < far;
  }
}
// Blocos limitam o custo próximo. Troca exclusiva + histerese evita duplicação e oscilação.
const NATURE_LOD_DISTANCE = { baixa: 34, media: 56, alta: 78 };
const NATURE_SHADOW_DISTANCE = 42;   // além disso a sombra da árvore cai fora do enquadramento útil
function setGroupShadow(group, on) {
  for (const m of group.children) if (m.isInstancedMesh) m.castShadow = on && m.userData.castShadow !== false;
}
export function updateNatureLOD(cam, qualityLevel = 'alta') {
  const distance = typeof qualityLevel === 'number'
    ? [38, 64, 94][Math.max(0, Math.min(2, Math.round(qualityLevel)))]
    : (NATURE_LOD_DISTANCE[qualityLevel] ?? NATURE_LOD_DISTANCE.alta);
  const cutCanopy = QREG.canopyFar || QREG.natureFar || 700;
  const cutNature = QREG.natureFar || 360;
  const cutGround = QREG.groundFar || 140;

  for (const r of QREG.nature) {
    const d = Math.hypot(r.x - cam.x, r.z - cam.z) - r.radius;
    const cut = r.category === 'canopy' ? cutCanopy : r.category === 'ground' ? cutGround : cutNature;
    if (d > cut) {
      if (r.isNear !== null) { r.near.visible = false; r.far.visible = false; r.isNear = null; r.sombra = false; }
      continue;
    }
    const near = d < distance + (r.isNear ? 5 : -5);
    if (near !== r.isNear) { r.near.visible = near; r.far.visible = !near; r.isNear = near; }
    // sombra por distância, com histerese (a qualidade pode desligá-la de vez)
    const sombra = QREG.natureShadow && d < NATURE_SHADOW_DISTANCE + (r.sombra ? 6 : 0);
    if (sombra !== r.sombra) { r.sombra = sombra; setGroupShadow(r.near, sombra); setGroupShadow(r.far, sombra); }
  }
}

const CHUNK = 40;
const CANOPY_KINDS = new Set(['pine', 'broad', 'dead', 'mountain', 'cliff']);
const GROUND_KINDS = new Set(['grass', 'flower', 'mushroom', 'pebble']);
const LOD_KINDS = new Set(['pine', 'broad', 'dead', 'bush', 'grass', 'flower', 'mushroom', 'rock', 'cliff', 'pebble', 'log', 'stump', 'branch', 'mountain']);

function spatialRank(it) {
  let h = (Math.round(it.x * 100) * 73856093) ^ (Math.round(it.z * 100) * 19349663);
  h = Math.imul(h ^ (h >>> 16), 0x85ebca6b);
  h = Math.imul(h ^ (h >>> 13), 0xc2b2ae35);
  return (h ^ (h >>> 16)) >>> 0;
}

export function instanced(kind, list, { cast = true, chunk } = {}) {
  const group = new THREE.Group(), buckets = new Map();
  const hasLOD = LOD_KINDS.has(kind);
  const category = CANOPY_KINDS.has(kind) ? 'canopy' : GROUND_KINDS.has(kind) ? 'ground' : 'understory';
  const effectiveChunk = chunk || (CANOPY_KINDS.has(kind) ? 128 : GROUND_KINDS.has(kind) ? 40 : 64);

  list.forEach((it, i) => {
    const variant = kind === 'grass' || kind === 'crystalShard' ? 0 : (it.variant ?? i % 3);
    const k = `${variant}|${it.dark ? 1 : 0}|${Math.floor(it.x / effectiveChunk)},${Math.floor(it.z / effectiveChunk)}`;
    if (!buckets.has(k)) buckets.set(k, []);
    buckets.get(k).push(it);
  });
  const mtx = new THREE.Matrix4(), q = new THREE.Quaternion(), e = new THREE.Euler(), v = new THREE.Vector3(), sc = new THREE.Vector3(), c = new THREE.Color();
  const kitTint = KIT.tint ? KIT.tint(kind) : null;

  for (const [k, items] of buckets) {
    // Ordena determinísticamente por hash espacial para redução uniforme entre níveis de qualidade
    items.sort((a, b) => spatialRank(a) - spatialRank(b));
    const [variant, dark] = k.split('|').map(Number), nearGroup = new THREE.Group(), farGroup = new THREE.Group();
    const choices = hasLOD ? [[true, nearGroup], [false, farGroup]] : [[true, nearGroup]];
    for (const [near, target] of choices) for (const [geo, material, partCast, depth, tinted] of parts(kind, variant, near, !!dark)) {
      const im = new THREE.InstancedMesh(geo, material, items.length);
      items.forEach((it, i) => {
        e.set(it.tilt || 0, it.rot || 0, 0); q.setFromEuler(e);
        v.set(it.x, it.y, it.z); sc.set(it.s, it.sy || it.s, it.s);
        mtx.compose(v, q, sc); im.setMatrixAt(i, mtx);
        if (tinted) {
          const tintVal = it.tint ?? kitTint;
          if (tintVal) {
            c.set(tintVal).multiplyScalar(it.shade || 1);
          } else {
            c.setScalar(it.shade || 1);
          }
          im.setColorAt(i, c);
        }
      });
      im.castShadow = cast && partCast; im.receiveShadow = true;
      im.userData.castShadow = im.castShadow;   // lembrado para a sombra por distância
      im.userData.full = items.length;
      if (depth) im.customDepthMaterial = depth;
      im.instanceMatrix.needsUpdate = true;
      if (im.instanceColor) im.instanceColor.needsUpdate = true;
      im.computeBoundingSphere(); target.add(im);
      if (kind === 'grass') QREG.grass.push({ mesh: im, full: items.length });
    }
    group.add(nearGroup);
    if (hasLOD) {
      group.add(farGroup); farGroup.visible = false;
      const x = items.reduce((s, it) => s + it.x, 0) / items.length, z = items.reduce((s, it) => s + it.z, 0) / items.length;
      const radius = Math.max(...items.map((it) => Math.hypot(it.x - x, it.z - z) + natureFootprint(kind, it.s)));
      QREG.nature.push({ near: nearGroup, far: farGroup, x, z, radius, isNear: true, sombra: true, category });
    }
  }
  return group;
}
