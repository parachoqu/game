// Personagens, criaturas e montaria com modelos realistas (Mixamo, leoa e cavalo da pasta de modelos).
// Animação: clipes reais (caminhada do Mixamo; ocioso/andar da leoa e do cavalo) misturados por velocidade,
// com poses procedurais por cima (golpes, arco, rolamento, montado, derrubado) aplicadas nos ossos
// em espaço do modelo — assim a mesma pose serve para qualquer esqueleto do Mixamo.
import * as THREE from 'three';
import { G } from '../state.js';
import { charMat as mat, patchMaterial } from './materials.js';
import { TEX } from './textures.js';
import { MODELS, instantiate, boneKey } from './models.js';
import { groundHeight } from './terrain.js';
import { locomotionWeights, hitClip, deathClip } from './anim-select.js';

const B = (w, h, d) => new THREE.BoxGeometry(w, h, d);
const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
const approach = (v, t, k) => v + (t - v) * Math.min(1, k);

// ---------- sombra por distância (cada personagem pesa no mapa de sombra) ----------
const RIGS = new Set();
const SHADOW_NEAR = 34, SHADOW_FAR = 38;   // histerese evita liga-desliga na borda
function track(root) { root.userData.castShadow = true; RIGS.add(root); return root; }
function setCast(root, on) {
  root.userData.castShadow = on;
  root.traverse((o) => { if (o.isMesh) o.castShadow = on; });
}
// inimigo removido de vez: sai do registro e libera a textura de ossos do esqueleto clonado
export function releaseCharacter(root) {
  RIGS.delete(root);
  root.traverse((o) => { if (o.isSkinnedMesh) o.skeleton.dispose(); });
}
// O three atualiza o esqueleto (e reenvia a textura de ossos) a cada chamada de render; o passe de
// normais do GTAO é outra chamada. Com este contador, cada esqueleto é atualizado uma vez por quadro.
let FRAME = 0;
function oncePerFrame(pivot) {
  pivot.traverse((o) => {
    if (!o.isSkinnedMesh || o.skeleton.userData) return;
    const sk = o.skeleton, up = sk.update;
    sk.userData = { f: -1 };
    sk.update = function () { if (this.userData.f === FRAME) return; this.userData.f = FRAME; up.call(this); };
  });
}
export function updateCharacterShadows(cx, cz) {
  FRAME++;
  for (const root of RIGS) {
    if (!root.parent) continue;   // fora da cena (montaria no estábulo, por exemplo)
    // personagem oculto (longe) não precisa das matrizes dos ossos atualizadas a cada quadro
    root.matrixWorldAutoUpdate = root.visible;
    if (!root.visible) continue;
    const e = root.matrixWorld.elements, d = Math.hypot(e[12] - cx, e[14] - cz);
    const on = root.userData.castShadow;
    if (on && d > SHADOW_FAR) setCast(root, false);
    else if (!on && d < SHADOW_NEAR) setCast(root, true);
  }
}

// ---------- materiais das variantes ----------
// o tom multiplica a textura (facção, roupa do NPC); a sombra da Turbulenta vira silhueta escura com brilho
const variants = new Map();
function variant(base, key, make) {
  const k = base.uuid + key;
  if (!variants.has(k)) { const m = make(base); patchMaterial(m, ['occ']); variants.set(k, m); }
  return variants.get(k);
}
function tinted(base, color) {
  return variant(base, 't' + color, (b) => { const m = b.clone(); m.color.set(color); return m; });
}
function shadowed(base) {
  return variant(base, 'shadow', (b) => new THREE.MeshStandardMaterial({
    color: '#2a2238', emissive: '#5a34a0', emissiveIntensity: 0.8, roughness: 0.5, metalness: 0.1, normalMap: b.normalMap,
  }));
}
function paint(pivot, fn) { pivot.traverse((o) => { if (o.isMesh) o.material = fn(o.material); }); }

// ---------- armas (PBR com as texturas procedurais do mapa, mapeadas por UV) ----------
const wmCache = new Map();
const WPARAM = { metal: [0.32, 0.9], madeira: [0.78, 0], tecido: [0.95, 0] };
function weaponMat(surface, color) {
  const k = surface + color;
  if (!wmCache.has(k)) {
    const t = TEX.surf[surface], [r, m] = WPARAM[surface];
    wmCache.set(k, patchMaterial(new THREE.MeshStandardMaterial({ color, map: t.alb, normalMap: t.nrm, roughness: r, metalness: m }), ['occ']));
  }
  return wmCache.get(k);
}
function wpart(geo, material, x, y, z) {
  const m = new THREE.Mesh(geo, material);
  m.position.set(x, y, z); m.castShadow = true;
  return m;
}
// Arma no "espaço da empunhadura": punho na origem, lâmina/cabo ao longo de +Z; o arco tem o eixo
// longo em Z e a curvatura para -Y (fica de pé quando o braço aponta para a frente)
// Armas do pacote PurePoly (só visual): modelo por família e a cor/superfície de cada zona. As cores
// são as das armas procedurais, para o equipamento continuar com a mesma leitura na mão.
const PP_WEAPONS = {
  espada: ['arma_espada', { lamina: ['metal', '#e2e6ea'], guarda: ['metal', '#b08d57'], cabo: ['tecido', '#4a3426'], pomo: ['metal', '#b08d57'] }],
  machado: ['arma_machado', { lamina: ['metal', '#d0d8e0'], guarda: ['metal', '#9a7b45'], cabo: ['madeira', '#6e4c30'], pomo: ['metal', '#9a7b45'] }],
  martelo: ['arma_martelo', { lamina: ['metal', '#7a7568'], cabo: ['madeira', '#6a4a32'] }],
  lanca: ['arma_lanca', { lamina: ['metal', '#d8dee3'], cabo: ['madeira', '#8a623c'], pomo: ['metal', '#a88548'] }],
  adaga: ['arma_adaga', { lamina: ['metal', '#e4e8ec'], guarda: ['metal', '#9a7b45'], cabo: ['tecido', '#3a281c'], pomo: ['metal', '#9a7b45'] }],
  foice: ['arma_foice', { lamina: ['metal', '#9b84b8'], cabo: ['madeira', '#5a3d28'], pomo: ['metal', '#7a6095'] }],
  arco: ['arma_arco', { arco: ['madeira', '#8a5a32'], cabo: ['tecido', '#5a3a22'], corda: ['tecido', '#e8e0c8'] }],
  manoplas: ['arma_manopla', { lamina: ['metal', '#c89d5c'], guarda: ['metal', '#9da6ad'] }],
  tomo_fogo: ['arma_tomo', { capa: ['tecido', '#943118'], ornamento: ['metal', '#ff7a36'] }],
  tomo_vento: ['arma_tomo', { capa: ['tecido', '#28688e'], ornamento: ['metal', '#90e0ef'] }],
  tomo_maldicao: ['arma_tomo', { capa: ['tecido', '#27173b'], ornamento: ['metal', '#9b5de5'] }],
  tomo_sagrado: ['arma_tomo', { capa: ['tecido', '#ebe4d8'], ornamento: ['metal', '#ffd166'] }],
  cajado_gelo: ['arma_cajado', { cabo: ['madeira', '#484352'], ornamento: ['metal', '#70d6ff'], pomo: ['metal', '#4a7ea8'] }],
  cajado_natureza: ['arma_cajado', { cabo: ['madeira', '#543d26'], ornamento: ['metal', '#489c62'], pomo: ['metal', '#489c62'] }],
  cajado_vital: ['arma_cajado', { cabo: ['madeira', '#73583c'], ornamento: ['metal', '#74c69d'], pomo: ['metal', '#74c69d'] }],
};
const ppGeo = new Map();
// geometria com UV projetada ao longo da peça, como a das armas procedurais (a textura acompanha)
function ppGeometry(geo) {
  if (ppGeo.has(geo)) return ppGeo.get(geo);
  const g = geo.clone(), p = g.attributes.position, uv = new Float32Array(p.count * 2);
  for (let i = 0; i < p.count; i++) { uv[i * 2] = (p.getX(i) + p.getY(i)) * 4; uv[i * 2 + 1] = p.getZ(i) * 2; }
  g.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
  ppGeo.set(geo, g);
  return g;
}
function ppWeapon(family, M) {
  const [id, zones] = PP_WEAPONS[family] || [];
  const src = id && MODELS.weapons?.[id];
  if (!src) return null;
  const g = new THREE.Group();
  src.traverse((o) => {
    if (!o.isMesh) return;
    const [surface, color] = zones[o.material.name] || ['metal', '#9da6ad'];
    const m = new THREE.Mesh(ppGeometry(o.geometry), M(surface, color));
    o.updateWorldMatrix(true, false);
    m.applyMatrix4(o.matrixWorld);
    m.castShadow = true;
    g.add(m);
  });
  return g;
}

export function weaponMesh(family, glow) {
  const g = new THREE.Group();
  const M = (surface, color) => (glow ? mat('#e9dcff', { emissive: '#b89cff', emissiveIntensity: 1.1 }) : weaponMat(surface, color));
  const pp = ppWeapon(family, M);
  if (pp) return pp;
  if (family === 'espada') {
    g.add(wpart(B(0.03, 0.07, 0.96), M('metal', '#e2e6ea'), 0, 0, 0.63));
    g.add(wpart(B(0.05, 0.26, 0.045), M('metal', '#b08d57'), 0, 0, 0.13));
    g.add(wpart(B(0.042, 0.042, 0.22), M('madeira', '#4a3426'), 0, 0, 0.01));
    g.add(wpart(new THREE.SphereGeometry(0.035, 8, 6), M('metal', '#b08d57'), 0, 0, -0.11));
  } else if (family === 'arco') {
    const R = 0.62, pts = [];
    for (let i = 0; i <= 16; i++) { const f = (i / 16 - 0.5) * 0.9 * Math.PI; pts.push(new THREE.Vector3(0, R - R * Math.cos(f), R * Math.sin(f))); }
    g.add(wpart(new THREE.TubeGeometry(new THREE.CatmullRomCurve3(pts), 24, 0.019, 5), M('madeira', '#8a5a32'), 0, 0, 0));
    const tipY = R - R * Math.cos(0.45 * Math.PI), tipZ = R * Math.sin(0.45 * Math.PI);
    g.add(wpart(new THREE.CylinderGeometry(0.004, 0.004, tipZ * 2, 3).rotateX(Math.PI / 2), M('tecido', '#e8e0c8'), 0, tipY, 0));
    g.add(wpart(B(0.048, 0.048, 0.13), M('tecido', '#5a3a22'), 0, 0.012, 0));
  } else if (family === 'martelo') {
    g.add(wpart(B(0.045, 0.045, 0.86), M('madeira', '#6a4a32'), 0, 0, 0.3));
    g.add(wpart(B(0.15, 0.3, 0.15), M('metal', '#7a7568'), 0, 0, 0.7));
  } else if (family === 'manoplas') {
    g.add(wpart(B(0.12, 0.12, 0.19), M('metal', '#9da6ad'), 0, 0, 0.03));
    g.add(wpart(B(0.14, 0.06, 0.09), M('metal', '#c89d5c'), 0, 0.04, 0.1));
    g.add(wpart(B(0.13, 0.13, 0.06), M('tecido', '#4a3426'), 0, 0, -0.05));
  } else if (family === 'maca') {
    g.add(wpart(B(0.04, 0.04, 0.8), M('madeira', '#5c4033'), 0, 0, 0.28));
    g.add(wpart(new THREE.CylinderGeometry(0.09, 0.09, 0.26, 6).rotateX(Math.PI / 2), M('metal', '#8f816c'), 0, 0, 0.68));
    g.add(wpart(B(0.24, 0.24, 0.08), M('metal', '#c29b4e'), 0, 0, 0.68));
  } else if (family === 'machado') {
    g.add(wpart(B(0.042, 0.042, 0.92), M('madeira', '#6e4c30'), 0, 0, 0.32));
    g.add(wpart(B(0.038, 0.34, 0.26), M('metal', '#d0d8e0'), 0, 0.12, 0.74));
    g.add(wpart(B(0.06, 0.08, 0.14), M('metal', '#9a7b45'), 0, 0, 0.74));
  } else if (family === 'adaga') {
    g.add(wpart(B(0.02, 0.045, 0.44), M('metal', '#e4e8ec'), 0, 0, 0.29));
    g.add(wpart(B(0.035, 0.15, 0.03), M('metal', '#9a7b45'), 0, 0, 0.08));
    g.add(wpart(B(0.03, 0.03, 0.14), M('tecido', '#3a281c'), 0, 0, 0.0));
    g.add(wpart(new THREE.SphereGeometry(0.025, 6, 6), M('metal', '#9a7b45'), 0, 0, -0.08));
  } else if (family === 'lanca') {
    g.add(wpart(B(0.035, 0.035, 2.2), M('madeira', '#8a623c'), 0, 0, 0.65));
    g.add(wpart(B(0.02, 0.09, 0.48), M('metal', '#d8dee3'), 0, 0, 1.82));
    g.add(wpart(B(0.05, 0.05, 0.12), M('metal', '#a88548'), 0, 0, 1.6));
    g.add(wpart(B(0.06, 0.18, 0.08), M('tecido', '#b24a3a'), 0, -0.06, 1.5));
  } else if (family === 'foice') {
    g.add(wpart(B(0.038, 0.038, 1.5), M('madeira', '#5a3d28'), 0, 0, 0.5));
    g.add(wpart(B(0.022, 0.58, 0.18), M('metal', '#9b84b8'), 0.22, 0.16, 1.2));
    g.add(wpart(B(0.05, 0.05, 0.14), M('metal', '#7a6095'), 0, 0, 1.2));
  } else if (family === 'besta') {
    g.add(wpart(B(0.07, 0.11, 0.74), M('madeira', '#734e32'), 0, 0, 0.22));
    g.add(wpart(B(0.72, 0.04, 0.05), M('metal', '#84807a'), 0, 0.04, 0.56));
    g.add(wpart(B(0.66, 0.015, 0.22), M('tecido', '#e8e0c8'), 0, 0.04, 0.44));
    g.add(wpart(B(0.025, 0.025, 0.36), M('metal', '#d0d8e0'), 0, 0.07, 0.46));
  } else if (family === 'cajado_gelo') {
    g.add(wpart(B(0.038, 0.038, 1.65), M('madeira', '#484352'), 0, 0, 0.48));
    g.add(wpart(new THREE.ConeGeometry(0.12, 0.4, 6).rotateX(-Math.PI / 2), M('metal', '#70d6ff'), 0, 0, 1.4));
    g.add(wpart(B(0.07, 0.07, 0.12), M('metal', '#4a7ea8'), 0, 0, 1.2));
  } else if (family === 'tomo_fogo') {
    g.add(wpart(B(0.12, 0.32, 0.42), M('tecido', '#943118'), 0, 0, 0.15));
    g.add(wpart(B(0.08, 0.28, 0.36), M('metal', '#ff7a36'), 0, 0, 0.15));
    g.add(wpart(B(0.13, 0.08, 0.1), M('metal', '#d4a342'), 0, 0, 0.15));
  } else if (family === 'cajado_natureza') {
    g.add(wpart(B(0.04, 0.04, 1.6), M('madeira', '#543d26'), 0, 0, 0.46));
    g.add(wpart(B(0.14, 0.26, 0.22), M('tecido', '#489c62'), 0, 0.06, 1.3));
  } else if (family === 'tomo_vento') {
    g.add(wpart(B(0.12, 0.32, 0.42), M('tecido', '#28688e'), 0, 0, 0.15));
    g.add(wpart(B(0.08, 0.28, 0.36), M('metal', '#90e0ef'), 0, 0, 0.15));
  } else if (family === 'tomo_maldicao') {
    g.add(wpart(B(0.12, 0.32, 0.42), M('tecido', '#27173b'), 0, 0, 0.15));
    g.add(wpart(B(0.08, 0.28, 0.36), M('metal', '#9b5de5'), 0, 0, 0.15));
  } else if (family === 'metamorfose') {
    g.add(wpart(B(0.08, 0.18, 0.24), M('metal', '#bc6c25'), 0, 0, 0.1));
    g.add(wpart(B(0.12, 0.12, 0.18), M('tecido', '#4a3426'), 0, 0, 0.02));
  } else if (family === 'cajado_vital') {
    g.add(wpart(B(0.038, 0.038, 1.6), M('madeira', '#73583c'), 0, 0, 0.45));
    g.add(wpart(new THREE.SphereGeometry(0.11, 8, 8), M('metal', '#74c69d'), 0, 0, 1.3));
  } else if (family === 'tomo_sagrado') {
    g.add(wpart(B(0.12, 0.32, 0.42), M('tecido', '#ebe4d8'), 0, 0, 0.15));
    g.add(wpart(B(0.13, 0.22, 0.22), M('metal', '#ffd166'), 0, 0, 0.15));
  }
  return mergeParts(g);
}
// junta as peças da arma por material (menos chamadas de desenho)
function mergeParts(g) {
  const byMat = new Map();
  for (const c of g.children) { if (!byMat.has(c.material)) byMat.set(c.material, []); byMat.get(c.material).push(c); }
  for (const [m, parts] of byMat) {
    if (parts.length < 2) continue;
    const geos = parts.map((c) => { c.updateMatrix(); const x = (c.geometry.index ? c.geometry.toNonIndexed() : c.geometry.clone()).applyMatrix4(c.matrix); x.deleteAttribute('uv'); return x; });
    const geo = new THREE.BufferGeometry();
    for (const name of ['position', 'normal']) {
      const arr = new Float32Array(geos.reduce((n, x) => n + x.attributes[name].array.length, 0)); let o = 0;
      for (const x of geos) { arr.set(x.attributes[name].array, o); o += x.attributes[name].array.length; }
      geo.setAttribute(name, new THREE.BufferAttribute(arr, 3));
    }
    // UV: projeção simples ao longo do comprimento (a textura acompanha a peça)
    const p = geo.attributes.position, uv = new Float32Array(p.count * 2);
    for (let i = 0; i < p.count; i++) { uv[i * 2] = (p.getX(i) + p.getY(i)) * 4; uv[i * 2 + 1] = p.getZ(i) * 2; }
    geo.setAttribute('uv', new THREE.BufferAttribute(uv, 2));
    geo.computeBoundingSphere();
    const merged = new THREE.Mesh(geo, m); merged.castShadow = true;
    for (const c of parts) g.remove(c);
    g.add(merged);
  }
  return g;
}

// ---------- ossos em espaço do modelo ----------
const _qa = new THREE.Quaternion(), _qb = new THREE.Quaternion(), _ax = new THREE.Vector3();
function chainQ(pivot, node, out) {       // orientação acumulada do nó até o pivot (exclusive)
  out.identity();
  for (let o = node; o && o !== pivot; o = o.parent) out.premultiply(o.quaternion);
  return out;
}
// gira o osso em torno de um eixo do modelo (x: lado esquerdo, y: cima, z: frente)
function bend(r, key, x, y, z, angle) {
  const b = r.bones[key];
  if (!b || !angle) return;
  chainQ(r.pivot, b.parent, _qa).invert();
  _ax.set(x, y, z).applyQuaternion(_qa);
  b.quaternion.premultiply(_qb.setFromAxisAngle(_ax, angle));
}
function boneMap(pivot) {
  const bones = {};
  pivot.traverse((o) => { if (o.isBone) bones[boneKey(o.name)] = o; });
  return bones;
}
// Ossos que recebem pose procedural. O mixer do three só regrava um osso quando o valor do clipe muda
// (e nunca regrava os que o clipe não anima), então a pose procedural se acumularia de um quadro para
// o outro: antes do mixer, cada osso volta à pose que o próprio mixer deixou; depois, essa pose é guardada.
const BENT = ['Hips', 'Spine', 'Spine1', 'Spine2', 'Neck', 'Head', 'LeftArm', 'RightArm', 'LeftForeArm', 'RightForeArm', 'LeftUpLeg', 'RightUpLeg', 'LeftLeg', 'RightLeg', 'LeftFoot', 'RightFoot', 'Jaw'];
function basePose(bones) {
  const base = [];
  for (const k of BENT) if (bones[k]) base.push([bones[k], new THREE.Quaternion(), new THREE.Vector3()]);
  return base;
}
function mixerStep(r, dt) {
  for (const [b, q, p] of r.base) { b.quaternion.copy(q); b.position.copy(p); }
  r.mixer.update(dt);
  for (const [b, q, p] of r.base) { q.copy(b.quaternion); p.copy(b.position); }
}

// ---------- humanoides (Mixamo) ----------
// pose ociosa: média de dois instantes opostos da caminhada (pés juntos, braços soltos)
function prepareHumanoid(M) {
  if (M.idleClip) return;
  const tmp = instantiate(M), bones = boneMap(tmp), mixer = new THREE.AnimationMixer(tmp), act = mixer.clipAction(M.walk).play();
  const T = M.walk.duration, fl = bones.LeftFoot, fr = bones.RightFoot, a = new THREE.Vector3(), b = new THREE.Vector3();
  let best = 0, bestD = Infinity;
  for (let i = 0; i < 40; i++) {
    act.time = (i / 40) * T; mixer.update(0); tmp.updateMatrixWorld(true);
    a.setFromMatrixPosition(fl.matrixWorld); b.setFromMatrixPosition(fr.matrixWorld);
    const d = Math.abs(a.z - b.z);
    if (d < bestD) { bestD = d; best = act.time; }
  }
  const tracks = M.walk.tracks.map((t) => {
    const it = t.createInterpolant(), n = t.getValueSize();
    const v1 = Array.from(it.evaluate(best)), v2 = Array.from(it.evaluate((best + T / 2) % T)), v = new Float32Array(n);
    if (n === 4) {
      const s = v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2] + v1[3] * v2[3] < 0 ? -1 : 1;
      for (let i = 0; i < 4; i++) v[i] = v1[i] + v2[i] * s;
      const l = Math.hypot(...v); for (let i = 0; i < 4; i++) v[i] /= l;
      return new THREE.QuaternionKeyframeTrack(t.name, [0], v);
    }
    for (let i = 0; i < n; i++) v[i] = (v1[i] + v2[i]) / 2;
    return new THREE.VectorKeyframeTrack(t.name, [0], v);
  });
  M.idleClip = new THREE.AnimationClip('idle', 1, tracks);   // duração 0 faria o laço dividir por zero
  // referência da pose ociosa para ampliar a passada na corrida
  const m2 = new THREE.AnimationMixer(tmp); m2.clipAction(M.idleClip).play(); m2.update(0);
  M.idleRef = {};
  for (const k of AMP) if (bones[k]) M.idleRef[k] = bones[k].quaternion.clone();
  const hips = bones.Hips; tmp.updateMatrixWorld(true);
  M.hipsY = new THREE.Vector3().setFromMatrixPosition(hips.matrixWorld).y;
  // altura média do quadril no ciclo: a corrida amplia a oscilação em torno dela
  const hipTrack = M.walk.tracks.find((t) => t.name === hips.name + '.position');
  M.hipsBase = hips.position.y;
  if (hipTrack) { let sum = 0, n = hipTrack.values.length / 3; for (let i = 0; i < n; i++) sum += hipTrack.values[i * 3 + 1]; M.hipsBase = sum / n; }
  calibrateContact(M, tmp, bones);
  M.cycle = cycleInfo(M, tmp, bones);
}

// Contato com o chão: clipes gravados com outra postura (a guarda, agachar, pular, o lote de combate)
// não ficam com os pés no chão só pela escala do quadril feita no carregamento — a guarda deixava o
// personagem ~12 cm acima do solo. O registro diz como assentar cada clipe (`contactMode`):
//   'feet'  o pé mais baixo do clipe inteiro vai à altura do pé mais baixo da pose ociosa;
//   'body'  poses que passam pelo chão (morte, levantar): o mesmo, medido só nos quadros em que o
//           corpo está de pé sobre os pés (quadril acima de 60 % da altura ociosa). Deitado, os pés
//           não são o apoio; sem quadro de pé, vale o osso mais baixo do corpo.
// Em ambos, a correção é uma altura constante na trilha do quadril, calculada por modelo.
const FEET = ['LeftToeBase', 'RightToeBase', 'LeftFoot', 'RightFoot'];
function lowestFoot(tmp, bones, clip, samples, mode = 'feet', standY = 0) {
  const mixer = new THREE.AnimationMixer(tmp), act = mixer.clipAction(clip).play(), v = new THREE.Vector3();
  let low = Infinity, lowAny = Infinity;
  for (let i = 0; i < samples; i++) {
    act.time = clip.duration * i / Math.max(1, samples - 1);
    mixer.update(0); tmp.updateMatrixWorld(true);
    if (mode === 'body') {
      for (const b of Object.values(bones)) { b.getWorldPosition(v); lowAny = Math.min(lowAny, v.y); }
      if (bones.Hips.getWorldPosition(v).y < standY * 0.6) continue;
    }
    for (const k of FEET) if (bones[k]) { bones[k].getWorldPosition(v); low = Math.min(low, v.y); }
  }
  mixer.stopAllAction();
  return low === Infinity ? lowAny : low;
}
export const contactModeOf = (key) => MODELS.clipMeta?.[key]?.contactMode || 'none';
function calibrateContact(M, tmp, bones) {
  const hips = bones.Hips;
  if (!M.anims || !hips) return;
  const todo = Object.keys(M.anims).filter((k) => contactModeOf(k) === 'feet' || contactModeOf(k) === 'body');
  if (!todo.length) return;
  const ref = lowestFoot(tmp, bones, M.anims.idle || M.idleClip, 6);
  // quanto o quadril sobe no mundo por unidade da trilha de posição
  tmp.updateMatrixWorld(true);
  const a = hips.getWorldPosition(new THREE.Vector3()).y;
  hips.position.y += 1; tmp.updateMatrixWorld(true);
  const unit = hips.getWorldPosition(new THREE.Vector3()).y - a;
  hips.position.y -= 1;
  M.contact = {};
  for (const k of todo) {
    const clip = M.anims[k];
    const track = clip.tracks.find((t) => t.name === hips.name + '.position');
    if (!track || !unit) continue;
    const delta = (ref - lowestFoot(tmp, bones, clip, 24, contactModeOf(k), M.hipsY)) / unit;
    for (let i = 1; i < track.values.length; i += 3) track.values[i] += delta;
    M.contact[k] = +(delta * unit).toFixed(3);
  }
}
// Ciclos de locomoção (caminhada, para trás, laterais): duração, velocidade que o clipe cobre a 1x e
// fase do contato do pé esquerdo. Os clipes estão no lugar; a velocidade nativa é a do pé de apoio
// deslizando sob o corpo. A caminhada guarda BASE_MPS, que é a calibração de sempre dela.
const CYCLES = ['walk', 'walkBack', 'strafeLeft', 'strafeRight'];
function cycleInfo(M, tmp, bones) {
  const out = {};
  const fl = bones.LeftFoot, fr = bones.RightFoot;
  if (!fl || !fr) return out;
  const a = new THREE.Vector3(), b = new THREE.Vector3();
  for (const key of CYCLES) {
    const clip = key === 'walk' ? M.walk : M.anims?.[key];
    if (!clip) continue;
    const mixer = new THREE.AnimationMixer(tmp), act = mixer.clipAction(clip).play();
    const N = 60, T = clip.duration, L = [], R = [];
    for (let i = 0; i < N; i++) {
      act.time = T * i / N; mixer.update(0); tmp.updateMatrixWorld(true);
      L.push(fl.getWorldPosition(a).clone()); R.push(fr.getWorldPosition(b).clone());
    }
    mixer.stopAllAction();
    let off = 0, minY = Infinity;
    for (let i = 0; i < N; i++) if (L[i].y < minY) { minY = L[i].y; off = i / N; }
    // velocidade do pé de apoio (o mais baixo) enquanto ele segue sendo o mais baixo
    const sp = [];
    for (let i = 0; i < N; i++) {
      const j = (i + 1) % N, low = L[i].y < R[i].y ? L : R;
      if ((low === L) !== (L[j].y < R[j].y)) continue;
      sp.push(Math.hypot(low[j].x - low[i].x, low[j].z - low[i].z) / (T / N));
    }
    sp.sort((x, y) => x - y);
    const measured = sp.length ? sp[Math.floor(sp.length / 2)] : 0;
    out[key] = { dur: T, off, speed: key === 'walk' ? BASE_MPS : measured > 0.3 ? measured : BASE_MPS };
  }
  return out;
}

const AMP = ['LeftUpLeg', 'RightUpLeg', 'LeftLeg', 'RightLeg', 'LeftArm', 'RightArm', 'LeftForeArm', 'RightForeArm'];

// suporte da arma preso ao osso da mão, calibrado na pose ociosa para a arma sair na direção `want`
function grip(h, key, want) {
  const hand = h.bones[key], g = new THREE.Group();
  chainQ(h.pivot, hand, _qa);
  g.quaternion.copy(_qa).invert().multiply(want);
  let s = 1; for (let o = hand; o; o = o.parent) { s *= o.scale.x; if (o === h.pivot) break; }
  g.scale.setScalar(1 / s);
  const mid = hand.children.find((c) => /Middle1$/.test(c.name)) || hand.children[0];
  if (mid) g.position.copy(mid.position).multiplyScalar(0.6);
  hand.add(g);
  return g;
}
const WANT_R = new THREE.Quaternion().setFromRotationMatrix(new THREE.Matrix4().makeBasis(
  new THREE.Vector3(1, 0, 0), new THREE.Vector3(0, 0.89, 0.45), new THREE.Vector3(0, -0.45, 0.89)));
const WANT_L = new THREE.Quaternion();

const RACE = { humano: [1, 1], elfo: [0.95, 1.04], anao: [1.12, 0.78], orc: [1.08, 1.06] };

// opts: {model, race, tint, shadow, guard, bandit, scale}
export function makeHumanoid(opts = {}) {
  const race = opts.race || 'humano';
  const id = opts.model || (opts.bandit || race === 'elfo' ? 'eve' : 'kachujin');
  const M = MODELS[id];
  prepareHumanoid(M);
  const root = track(new THREE.Group());
  const body = new THREE.Group(); root.add(body);
  const pivot = instantiate(M); body.add(pivot); oncePerFrame(pivot);
  const [sx, sy] = RACE[race] || RACE.humano, k = opts.scale || 1;
  body.scale.set(sx * k, sy * k, sx * k);
  if (opts.shadow) paint(pivot, shadowed);
  else if (opts.tint) paint(pivot, (m) => tinted(m, opts.tint));   // tom só por facção: cada tom é um material a mais
  const bones = boneMap(pivot), base = basePose(bones);
  for (const [b, q, p] of base) { q.copy(b.quaternion); p.copy(b.position); }
  const mixer = new THREE.AnimationMixer(pivot);
  const actions = {};
  if (M.anims) {
    for (const [key, clip] of Object.entries(M.anims)) {
      if (clip) {
        const act = mixer.clipAction(clip);
        act.play();
        act.setEffectiveWeight(0);
        actions[key] = act;
      }
    }
  }
  // clipes do lote de combate: o animador escreve o instante a cada quadro (o mixer não avança)
  for (const [key, act] of Object.entries(actions)) if (MODELS.clipMeta?.[key]?.group === 'combat') act.timeScale = 0;
  const idle = actions.idle || mixer.clipAction(M.idleClip).play();
  const walk = actions.walk || mixer.clipAction(M.walk).play();
  idle.play(); idle.setEffectiveWeight(1);
  walk.play(); walk.setEffectiveWeight(0);
  walk.time = Math.random() * (M.walk ? M.walk.duration : 1);
  if (actions.run) { actions.run.play(); actions.run.setEffectiveWeight(0); }
  const h = {
    root, body, pivot, M, mixer, idle, walk, actions, bones, base, walkW: 0, acc: 0, tick: 0,
    mps: 0, lean: 0, tiltX: 0, tiltZ: 0, prevYaw: root.rotation.y,
    phase: Math.random() * 6, weapon: null, family: null, hipsY: M.hipsY * sy * k, glow: !!opts.shadow,
    walkDur: walk.getClip().duration || 1.333,
    runDur: actions.run ? actions.run.getClip().duration : 0.542,
    smoothAccel: 0, strafe: 0, fwd: 1, prevRunK: 0,
    lookX: 0, lookY: 0, glanceTimer: 1.5 + Math.random() * 2.5, targetGlanceX: 0, targetGlanceY: 0,
    layers: {},
  };
  h.actionList = [...new Set([idle, walk, ...Object.values(actions)])];
  // ações avulsas do lote (impacto, esquiva, morte, levantar, transições): zeradas a cada quadro e
  // ligadas só enquanto o estado pede
  h.oneShots = Object.entries(actions)
    .filter(([key]) => MODELS.clipMeta?.[key]?.group === 'combat' && MODELS.clipMeta[key].locomotionMode !== 'cycle')
    .map(([, act]) => act);
  // camada de tronco e braços da mira com o arco, usada andando (as pernas seguem a caminhada)
  if (actions.bowAim) {
    const byName = {};
    pivot.traverse((o) => { if (o.isBone) byName[o.name] = o; });
    h.bowLayer = actions.bowAim.getClip().tracks
      .filter((t) => t.name.endsWith('.quaternion'))
      .map((t) => ({ bone: byName[t.name.slice(0, -'.quaternion'.length)], it: t.createInterpolant() }))
      .filter((l) => l.bone && !UPPER_SKIP.test(boneKey(l.bone.name)));
  }
  mixerStep(h, 0);   // pose ociosa aplicada antes de calibrar as empunhaduras
  h.gripR = grip(h, 'RightHand', WANT_R);
  h.gripL = grip(h, 'LeftHand', WANT_L);
  h.setWeapon = (family) => {
    if (h.weapon) { h.weapon.parent?.remove(h.weapon); h.weapon = null; }
    if (h.weaponL) { h.weaponL.parent?.remove(h.weaponL); h.weaponL = null; }
    h.family = family;
    if (!family || family === 'punhos') return;
    h.weapon = weaponMesh(family, h.glow);
    if (!root.userData.castShadow) h.weapon.traverse((o) => { if (o.isMesh) o.castShadow = false; });
    const isLeft = family === 'arco' || family === 'tomo_fogo' || family === 'tomo_vento' || family === 'tomo_maldicao' || family === 'tomo_sagrado';
    (isLeft ? h.gripL : h.gripR).add(h.weapon);
    if (family === 'manoplas' || family === 'adaga') {
      h.weaponL = weaponMesh(family, h.glow);
      if (!root.userData.castShadow) h.weaponL.traverse((o) => { if (o.isMesh) o.castShadow = false; });
      h.gripL.add(h.weaponL);
    }
  };
  return h;
}

// distância à câmera → de quantos em quantos quadros o esqueleto é atualizado
function lodStep(r, dt) {
  r.acc += dt;
  const cam = G.camera;
  if (cam) {
    const p = r.root.position, d2 = (p.x - cam.position.x) ** 2 + (p.z - cam.position.z) ** 2;
    const every = d2 > 90 * 90 ? 6 : d2 > 45 * 45 ? 3 : 1;
    if (++r.tick % every) return 0;
  }
  const s = r.acc; r.acc = 0;
  return s;
}

// s: {speed(0..1), mps (m/s), attack(-1|0..1), kind, dodge(-1|0..1), down, mounted, channel, metamorph, craft, dt}
const BASE_MPS = 1.05;   // velocidade que o clipe cobre sozinho, a 1x
const CROUCH_CLIP_MPS = 1.2;   // idem para a caminhada agachada
// Mira com o arco (clipe Mixamo "Shooting Arrow"): instantes do clipe, em segundos. Antes de 2 s o
// arqueiro pega e encaixa a flecha; a mira começa com a flecha já encaixada.
const BOW = { draw: 2.05, redraw: 2.3, full: 2.8, hold: 3.6, release: 3.93, after: 4.2 };
const BOW_RELEASE_AT = 0.28;              // fração do disparo em que a flecha sai (0,16 s de 0,58 s)
const BOW_TWIST = -1.83;                  // andando, o tronco gira o que o clipe gira o corpo inteiro (~105°)
// Lote de combate (tempos do jogo em player.js: a esquiva dura 0,42 s e desloca o corpo nos primeiros
// 0,34 s). As taxas encurtam clipes longos do Mixamo sem mudar a pose.
const DODGE_MOVE = 0.34 / 0.42;
const HIT_RATE = 1.4, DEATH_RATE = 1.25, POSTURE_RATE = 1.3, DODGE_TAIL = 0.25;
const SIDE_CYCLES = ['walkBack', 'strafeLeft', 'strafeRight'];
// As esquivas "Standing Dodge" do Mixamo foram gravadas segurando um arco. Conferidas no jogo: com
// arco, sem arma e com besta a pose dos braços combina; com espada a lâmina fica solta e cruza o
// peito na recuperação. As famílias fora da lista usam o rolamento de sempre.
const STANDING_DODGE = new Set(['arco', 'punhos', 'besta']);
export const standingDodgeFits = (family) => STANDING_DODGE.has(family || 'punhos');

// dá à ação `act` a parcela `w` do corpo: as demais ações ativas cedem na mesma proporção
function overlay(h, act, w) {
  if (w > 0.001) for (const a of h.actionList) if (a !== act) a.setEffectiveWeight(a.getEffectiveWeight() * (1 - w));
  act.setEffectiveWeight(Math.max(0, w));
}
// tronco e braços de um clipe, para tocar por cima da locomoção (ossos de quadril e pernas ficam de fora)
function upperLayer(h, key) {
  if (h.layers[key] !== undefined) return h.layers[key];
  const act = h.actions?.[key];
  if (!act) return (h.layers[key] = null);
  const byName = {};
  h.pivot.traverse((o) => { if (o.isBone) byName[o.name] = o; });
  return (h.layers[key] = act.getClip().tracks
    .filter((t) => t.name.endsWith('.quaternion'))
    .map((t) => ({ bone: byName[t.name.slice(0, -'.quaternion'.length)], it: t.createInterpolant() }))
    .filter((l) => l.bone && !UPPER_SKIP.test(boneKey(l.bone.name))));
}
const UPPER_SKIP = /^(Hips|LeftUpLeg|RightUpLeg|LeftLeg|RightLeg|LeftFoot|RightFoot|LeftToeBase|RightToeBase|LeftToe_End|RightToe_End)$/;
const _bq = new THREE.Quaternion();
const _amp = new THREE.Quaternion();
function ampBone(h, key, f) {
  const b = h.bones[key], ref = h.M.idleRef[key];
  if (!b || !ref || Math.abs(f - 1) < 0.001) return;
  _amp.copy(b.quaternion);
  b.quaternion.copy(ref).slerp(_amp, f);   // f > 1 amplia a pose, f < 1 contém
}

export function animateHumanoid(h, s) {
  const dt = lodStep(h, s.dt || 0.016);
  if (!dt) return;
  h.phase += dt * 4;
  const A = h.actions || {};
  const has = (k) => !!A[k];
  for (const a of h.oneShots || []) a.setEffectiveWeight(0);
  // morte direcional (inimigos): separada da derrubada recuperável; sem o clipe, vale a pose de derrubado
  const deathKey = s.death ? (h.deathKey || (h.deathKey = deathClip(s.death.fall, has))) : (h.deathKey = null);
  const deathAct = deathKey && deathKey !== 'down' ? A[deathKey] : null;
  const down = !!(s.down || s.death);

  // Inércia e taxa de aceleração instantânea
  const prevMps = h.mps;
  h.mps = approach(h.mps, s.mps != null ? s.mps : (s.speed || 0) * 7, dt * 9);
  const mps = h.mps;
  const rawAccel = (mps - prevMps) / Math.max(dt, 1e-3);
  h.smoothAccel = approach(h.smoothAccel, clamp(rawAccel, -18, 18), dt * 8);

  // Direção de movimento relativo ao facing do corpo
  let targetFwd = 1, targetStrafe = 0;
  if (s.fwd != null) {
    targetFwd = s.fwd;
    targetStrafe = s.strafe || 0;
  } else if (s.moveDir) {
    const [mx, mz] = s.moveDir;
    const yaw = h.root.rotation.y;
    targetFwd = mx * Math.sin(yaw) + mz * Math.cos(yaw);
    targetStrafe = mx * Math.cos(yaw) - mz * Math.sin(yaw);
  }
  h.fwd = approach(h.fwd, targetFwd, dt * 9);
  h.strafe = approach(h.strafe, targetStrafe, dt * 8);

  const moving = !s.mounted && !down && !(s.dodge >= 0) && mps > 0.35;
  h.walkW = approach(h.walkW, moving ? 1 : 0, dt * 8);
  const runK = clamp((mps - 3.4) / 2.6, 0, 1);                                  // 0 andando, 1 correndo
  const stride = 1 + 0.35 * clamp((mps - 1) / 2.6, 0, 1);

  // Direção da passada: para frente a caminhada (e a corrida); para trás e de lado os clipes do lote de
  // combate, com os vizinhos dividindo o peso nas diagonais. Sem eles, a caminhada tocada ao contrário
  // e a inclinação lateral de antes.
  const dir = locomotionWeights(h.fwd, h.strafe, has);
  const moveSign = dir.reverse ? -1 : 1;
  const crouchSign = h.fwd < -0.15 ? -1 : 1;
  const cyc = h.M.cycle || {};
  let walkTs = clamp(mps / (BASE_MPS * stride), 0.7, 2.6);
  if (dir.walk < 0.999 && cyc.walk) {
    // cadência: média dos ciclos na velocidade atual, pesada pela direção (fase e velocidade juntas)
    let T = dir.walk * h.walkDur / walkTs;
    for (const k of SIDE_CYCLES) if (dir[k] > 0 && cyc[k]) T += dir[k] * cyc[k].dur / clamp(mps / cyc[k].speed, 0.6, 2.6);
    walkTs = h.walkDur / T;
  }
  h.walk.timeScale = moveSign * walkTs;
  if (h.actions?.run) {
    h.actions.run.timeScale = moveSign * clamp(mps / 4.6, 0.8, 2.0);
    // Ao transicionar para a corrida, sincroniza a fase com a caminhada uma única vez
    if ((h.prevRunK || 0) <= 0.05 && runK > 0.05) {
      const normPhase = (((h.walk.time % h.walkDur) + h.walkDur) % h.walkDur) / h.walkDur;
      h.actions.run.time = normPhase * h.runDur;
    }
    h.actions.run.setEffectiveWeight(h.walkW * runK * dir.walk);
    h.walk.setEffectiveWeight(h.walkW * (1 - runK) * dir.walk);
  } else {
    h.walk.setEffectiveWeight(h.walkW * dir.walk);
  }
  h.prevRunK = runK;
  // para trás e de lado seguem a fase da caminhada, alinhados pelo contato do pé esquerdo
  if (cyc.walk) {
    let ph = ((h.walk.time + dt * h.walk.timeScale) / h.walkDur) % 1;
    if (ph < 0) ph += 1;
    if (moveSign < 0) ph = 1 - ph;
    for (const k of SIDE_CYCLES) {
      const a = A[k];
      if (!a) continue;
      a.setEffectiveWeight(h.walkW * dir[k]);
      if (cyc[k]) a.time = (((ph - cyc.walk.off + cyc[k].off) % 1) + 1) % 1 * cyc[k].dur;
    }
  }
  const idleW = 1 - h.walkW;
  const inGuard = (h.family && h.family !== 'punhos' && mps < 0.35 && !down && s.dodge < 0);
  if (h.actions?.guard) {
    h.actions.guard.setEffectiveWeight(idleW * (inGuard ? 0.85 : 0));
    h.idle.setEffectiveWeight(idleW * (inGuard ? 0.15 : 1));
  } else {
    h.idle.setEffectiveWeight(idleW);
  }

  // Agachar e pular (clipes Mixamo opcionais, `extra-clips.js`): agachado, CrouchIdle/CrouchWalk
  // tomam o lugar do ocioso e da caminhada; no ar, o clipe de pulo é tocado pela fase do voo.
  // Sem os clipes, as mecânicas seguem funcionando com a locomoção normal.
  h.crouchW = approach(h.crouchW || 0, s.crouch ? 1 : 0, dt * 7);
  h.airW = approach(h.airW || 0, s.air ? 1 : 0, dt * 14);
  const crouchClips = A.crouchIdle || A.crouchWalk;
  const cw = crouchClips ? h.crouchW : 0;
  const jumpAct = s.air && s.air.boosted && A.jumpBoost ? A.jumpBoost : A.jump;
  const aw0 = jumpAct ? h.airW : 0;
  const keep = (1 - cw) * (1 - aw0);
  if (keep < 0.999) {
    for (const a of [h.idle, h.walk, A.run, A.guard, A.walkBack, A.strafeLeft, A.strafeRight]) if (a) a.setEffectiveWeight(a.getEffectiveWeight() * keep);
  }
  if (A.crouchIdle) A.crouchIdle.setEffectiveWeight(idleW * cw * (1 - aw0));
  if (A.crouchWalk) {
    A.crouchWalk.setEffectiveWeight(h.walkW * cw * (1 - aw0));
    A.crouchWalk.timeScale = crouchSign * clamp(mps / CROUCH_CLIP_MPS, 0.6, 1.8);
  }
  for (const a of [A.jump, A.jumpBoost]) if (a && a !== jumpAct) a.setEffectiveWeight(0);
  if (jumpAct) {
    jumpAct.setEffectiveWeight(aw0);
    if (s.air) jumpAct.time = clamp(s.air.phase, 0, 1) * jumpAct.getClip().duration;
  }

  // Mira com o arco (clipe opcional bowAim): ao começar a mirar o arqueiro ergue e puxa o arco, segura
  // puxado e, no disparo, solta a corda no mesmo instante em que a flecha sai; mirando ainda, puxa de
  // novo. Parado vale o corpo inteiro do clipe; andando, só tronco e braços (camada depois do mixer).
  const bowAct = A.bowAim;
  const bowOn = !!bowAct && h.family === 'arco' && (s.aim || 0) > 0.01 && !down && !(s.dodge >= 0) && !s.air && !s.mounted;
  h.bowW = approach(h.bowW || 0, bowOn ? s.aim : 0, dt * 10);
  let bowShot = false;
  if (bowAct) {
    if (h.bowW > 0.001) {
      if (s.attack >= 0 && (s.kind === 'bow' || s.kind === 'charge')) {
        if (h.bowShotFrom == null) h.bowShotFrom = h.bowT ?? BOW.draw;
        const t = s.attack, r = BOW_RELEASE_AT;
        h.bowT = t < r ? h.bowShotFrom + (BOW.release - h.bowShotFrom) * (t / r) : BOW.release + (BOW.after - BOW.release) * ((t - r) / (1 - r));
        bowShot = true;
      } else {
        if (h.bowShotFrom != null) { h.bowShotFrom = null; h.bowT = BOW.redraw; }
        if (h.bowT == null || h.bowT < BOW.draw) h.bowT = BOW.draw;
        h.bowT = h.bowT < BOW.full ? Math.min(BOW.full, h.bowT + dt * 1.6) : Math.min(BOW.hold, h.bowT + dt * 0.5);
      }
    } else { h.bowT = null; h.bowShotFrom = null; }
    const full = h.bowW * (1 - h.walkW);
    if (full > 0.001) for (const a of [h.idle, h.walk, A.run, A.guard, A.crouchIdle, A.crouchWalk, A.walkBack, A.strafeLeft, A.strafeRight]) if (a) a.setEffectiveWeight(a.getEffectiveWeight() * (1 - full));
    bowAct.setEffectiveWeight(full);
    if (h.bowT != null) bowAct.time = h.bowT;
  }

  // ---------- camadas do lote de combate: seguem o estado do jogo, nunca o conduzem ----------
  const busy = s.attack >= 0 || s.dodge >= 0 || down || s.mounted || s.air || s.metamorph || s.craft || s.channel;
  // transição de postura: agachar ou levantar parado toca o clipe de transição uma vez, por cima; o
  // controle e a velocidade de agachado mudam na hora, como sempre
  const crouchOn = !!s.crouch;
  if (h.prevCrouch !== undefined && crouchOn !== h.prevCrouch && h.walkW < 0.3 && !busy) {
    const act = A[crouchOn ? 'standToCrouch' : 'crouchToStand'];
    h.posture = act ? { act, t: 0 } : null;
  }
  h.prevCrouch = crouchOn;
  if (h.posture) {
    const p = h.posture, d = p.act.getClip().duration;
    p.t += dt * POSTURE_RATE;
    if (busy || p.t >= d) h.posture = null;
    else { p.act.time = p.t; overlay(h, p.act, Math.min(1, (d - p.t) / (0.15 * POSTURE_RATE)) * (1 - h.walkW)); }
  }
  // impacto pelo lado de onde veio o golpe: parado, o corpo inteiro; andando, tronco e braços
  if (s.hit && !busy && (h.bowW || 0) < 0.3) {
    const key = hitClip(s.hit.side, has);
    if (key) {
      const act = A[key], d = act.getClip().duration, t = s.hit.t * HIT_RATE;
      if (t < d) {
        act.time = t;
        const env = Math.min(1, s.hit.t / 0.06) * clamp((d - t) / (0.3 * d), 0, 1);
        overlay(h, act, env * (1 - h.walkW));
        if (h.walkW > 0.01) h.hitLayer = { key, t, w: env * h.walkW * 0.85 };
      }
    }
  }
  // fim da esquiva direcional: o clipe termina a recuperação enquanto a locomoção volta
  if (s.dodge < 0 && h.dodgeTail) {
    const tl = h.dodgeTail;
    tl.t += dt; tl.age += dt;
    const w = busy ? 0 : (1 - tl.age / DODGE_TAIL) * (1 - h.walkW);
    if (w <= 0 || tl.t >= tl.act.getClip().duration) h.dodgeTail = null;
    else { tl.act.time = tl.t; overlay(h, tl.act, w); }
  }

  // Ações de combate e habilidades via clipes 3D mocap
  let activeCombatAct = null;
  if (s.attack >= 0 && !bowShot) {
    const t = s.attack;
    if (s.kind === 'slash' || s.kind === 'spin') activeCombatAct = h.actions?.slash;
    else if (s.kind === 'thrust') activeCombatAct = h.actions?.thrust;
    else if (s.kind === 'punch' || s.kind === 'rapid') activeCombatAct = h.actions?.punch;
    else if (s.kind === 'smash' || s.kind === 'heavy') activeCombatAct = h.actions?.smash;
    else if (s.kind === 'bow' || s.kind === 'crossbow' || s.kind === 'charge') activeCombatAct = h.actions?.bow;
    else if (s.kind === 'cast') activeCombatAct = h.actions?.cast;
    else if (s.kind === 'spell_area') activeCombatAct = h.actions?.spell_area;
    if (activeCombatAct) {
      // Envelope suave de peso para transições limpas sem estalo
      const env = t < 0.12 ? (t / 0.12) : (t > 0.86 ? Math.max(0, (1 - t) / 0.14) : 1);
      activeCombatAct.setEffectiveWeight(0.96 * env);
      activeCombatAct.time = clamp(t, 0, 1) * activeCombatAct.getClip().duration;
    }
  }

  const dodgeAct = s.dodge >= 0 && s.dodgeKey && s.dodgeKey !== 'roll' ? A[s.dodgeKey] : null;
  if (deathAct) {
    activeCombatAct = deathAct;
    deathAct.time = Math.min(deathAct.getClip().duration - 1e-3, s.death.t * DEATH_RATE);
    overlay(h, deathAct, 1);
  } else if (dodgeAct) {
    // esquiva direcional: a janela em que o quadril se desloca no clipe acompanha os 0,34 s em que o
    // jogo desloca o corpo; o resto do estado avança em tempo real e a sobra vira a cauda
    activeCombatAct = dodgeAct;
    const m = MODELS.clipMeta?.[s.dodgeKey]?.motion || [0, dodgeAct.getClip().duration];
    const a = Math.max(0, m[0] - 0.12), p = clamp(s.dodge, 0, 1);
    dodgeAct.time = p <= DODGE_MOVE ? a + (m[1] - a) * p / DODGE_MOVE : m[1] + (p - DODGE_MOVE) * 0.42;
    overlay(h, dodgeAct, 1);
    h.dodgeTail = { act: dodgeAct, t: dodgeAct.time, age: 0 };
  } else if (s.dodge >= 0 && h.actions?.roll) {
    activeCombatAct = h.actions.roll;
    h.actions.roll.setEffectiveWeight(1.0);
    h.actions.roll.time = clamp(s.dodge, 0, 1) * h.actions.roll.getClip().duration;
  } else if (down && h.actions?.down) {
    activeCombatAct = h.actions.down;
    h.actions.down.setEffectiveWeight(1.0);
    h.actions.down.time = Math.min(h.actions.down.getClip().duration, 1.4);
  } else if (s.metamorph && h.actions?.roar) {
    h.actions.roar.setEffectiveWeight(0.85);
  } else if (s.craft && h.actions?.craft) {
    h.actions.craft.setEffectiveWeight(1.0);
  }

  const clipsToZero = ['slash', 'thrust', 'punch', 'smash', 'bow', 'cast', 'spell_area', 'roll', 'down', 'roar', 'craft'];
  for (const k of clipsToZero) {
    const a = h.actions?.[k];
    if (!a) continue;
    if (a !== activeCombatAct && !(k === 'roar' && s.metamorph) && !(k === 'craft' && s.craft)) {
      a.setEffectiveWeight(0);
    }
  }

  mixerStep(h, dt);

  // andando com o arco puxado: tronco e braços do clipe por cima da caminhada, e o giro de lado que o
  // clipe faz com o corpo inteiro passa para a coluna
  const upperW = (h.bowW || 0) * h.walkW;
  if (upperW > 0.001 && h.bowLayer && h.bowT != null) {
    for (const l of h.bowLayer) l.bone.quaternion.slerp(_bq.fromArray(l.it.evaluate(h.bowT)), upperW);
    const tw = BOW_TWIST * upperW;
    bend(h, 'Spine', 0, 1, 0, tw * 0.35); bend(h, 'Spine1', 0, 1, 0, tw * 0.35); bend(h, 'Spine2', 0, 1, 0, tw * 0.3);
  }
  if (h.hitLayer) {
    const L = upperLayer(h, h.hitLayer.key);
    if (L) for (const l of L) l.bone.quaternion.slerp(_bq.fromArray(l.it.evaluate(h.hitLayer.t)), h.hitLayer.w);
    h.hitLayer = null;
  }

  h.body.rotation.set(0, 0, 0); h.body.position.set(0, 0, 0);
  const w = h.walkW, rk = runK * w;
  const walkK = (1 - runK) * w;
  // ampBone nas pernas SOMENTE quando não houver o clipe mocap de corrida
  // O clipe 'run.glb' do Mixamo já possui biomecânica completa e passada atlética aberta
  if (!h.actions?.run && walkK > 0.05) {
    const k = (g) => 1 + (stride - 1) * g * walkK;
    ampBone(h, 'LeftUpLeg', k(1)); ampBone(h, 'RightUpLeg', k(1));
    ampBone(h, 'LeftLeg', k(0.8)); ampBone(h, 'RightLeg', k(0.8));
    ampBone(h, 'LeftArm', k(0.75)); ampBone(h, 'RightArm', k(0.75));
    ampBone(h, 'LeftForeArm', k(0.5)); ampBone(h, 'RightForeArm', k(0.5));
  }
  // A mão que segura a arma fica firme para a arma não chicotear
  if (w > 0.01 && h.family && h.family !== 'punhos') {
    const arm = (h.family === 'arco' || h.family?.startsWith('tomo_')) ? 'Left' : 'Right';
    ampBone(h, arm + 'Arm', 0.55); ampBone(h, arm + 'ForeArm', 0.5);
    bend(h, arm + 'ForeArm', 1, 0, 0, -0.45 * w);
  }
  if (rk > 0.01) {
    // Corrida: tronco à frente com peso do centro de massa e olhar no horizonte
    bend(h, 'Spine', 1, 0, 0, 0.14 * rk); bend(h, 'Spine1', 1, 0, 0, 0.06 * rk);
    bend(h, 'Neck', 1, 0, 0, -0.10 * rk); bend(h, 'Head', 1, 0, 0, -0.08 * rk);
    if (!h.actions?.run) {
      bend(h, 'LeftForeArm', 1, 0, 0, -0.85 * rk); bend(h, 'RightForeArm', 1, 0, 0, -0.85 * rk);
      bend(h, 'LeftArm', 0, 0, 1, 0.12 * rk); bend(h, 'RightArm', 0, 0, 1, -0.12 * rk);
      const hips = h.bones.Hips;
      if (hips) hips.position.y = h.M.hipsBase + (hips.position.y - h.M.hipsBase) * (1 + 0.55 * rk);
    }
  }

  // Dinâmica de aceleração e frenagem (inércia do centro de massa)
  if (h.smoothAccel > 0.5) {
    const accelPitch = clamp(h.smoothAccel * 0.012, 0, 0.13) * (h.fwd >= 0 ? 1 : -0.7);
    bend(h, 'Spine', 1, 0, 0, accelPitch);
    bend(h, 'Spine1', 1, 0, 0, accelPitch * 0.5);
  } else if (h.smoothAccel < -2.0) {
    const brakeWeight = clamp(-h.smoothAccel * 0.012, 0, 0.14);
    bend(h, 'Spine', 1, 0, 0, -brakeWeight * 0.6);
    if (h.bones.Hips) h.bones.Hips.position.y -= brakeWeight * 0.08;
  }

  // Equilíbrio lateral em strafe
  if (Math.abs(h.strafe) > 0.05 && moving && !(A.strafeLeft && A.strafeRight)) {
    const st = clamp(h.strafe, -1, 1);
    bend(h, 'Hips', 0, 0, 1, st * 0.04);
    bend(h, 'Spine', 0, 0, 1, -st * 0.07);
    bend(h, 'Spine1', 0, 0, 1, -st * 0.04);
  }

  // Living Idle: respiração realista, peso orgânico entre os pés e micro-olhares
  if (idleW > 0.05 && !down && s.dodge < 0) {
    const br = Math.sin(h.phase * 0.38);
    const brChest = Math.cos(h.phase * 0.38);
    bend(h, 'Spine', 1, 0, 0, br * 0.016 * idleW);
    bend(h, 'Spine1', 1, 0, 0, brChest * 0.024 * idleW);
    bend(h, 'Spine2', 1, 0, 0, brChest * 0.016 * idleW);
    bend(h, 'LeftArm', 0, 0, 1, brChest * 0.018 * idleW);
    bend(h, 'RightArm', 0, 0, 1, -brChest * 0.018 * idleW);

    const sway = Math.sin(h.phase * 0.14);
    bend(h, 'Hips', 0, 0, 1, sway * 0.022 * idleW);
    bend(h, 'Spine', 0, 0, 1, -sway * 0.028 * idleW);
    bend(h, 'LeftUpLeg', 0, 0, 1, -sway * 0.015 * idleW);
    bend(h, 'RightUpLeg', 0, 0, 1, -sway * 0.015 * idleW);

    h.glanceTimer -= dt;
    if (h.glanceTimer <= 0) {
      h.glanceTimer = 2.2 + Math.random() * 3.2;
      h.targetGlanceX = (Math.random() - 0.5) * 0.22;
      h.targetGlanceY = (Math.random() - 0.5) * 0.12;
    }
    h.lookX = approach(h.lookX, s.aim ? 0 : h.targetGlanceX, dt * 3.5);
    h.lookY = approach(h.lookY, s.aim ? 0 : h.targetGlanceY, dt * 3.5);
    if (!s.aim && idleW > 0.4) {
      bend(h, 'Neck', 0, 1, 0, h.lookX * idleW);
      bend(h, 'Head', 0, 1, 0, h.lookX * 0.65 * idleW);
      bend(h, 'Head', 1, 0, 0, h.lookY * idleW);
    }
  }

  // Mirando (s.aim de 0 a 1): ombros de lado, arma erguida na linha do olhar e cabeça voltada ao alvo
  const aw = s.aim || 0;
  const bw = h.bowW || 0;
  if (bw > 0.01 && !down && s.dodge < 0) {
    // arco com o clipe: os braços vêm dele; aqui só a mira vertical inclina o tronco e a cabeça
    const ap = s.aimPitch || 0;
    if (Math.abs(ap) > 0.001) {
      bend(h, 'Spine', 1, 0, 0, -ap * 0.35 * bw);
      bend(h, 'Spine1', 1, 0, 0, -ap * 0.25 * bw);
      bend(h, 'Neck', 1, 0, 0, -ap * 0.18 * bw);
      bend(h, 'Head', 1, 0, 0, -ap * 0.12 * bw);
    }
  } else if (aw > 0.01 && !down && s.dodge < 0 && !(s.attack >= 0)) {
    const lead = (h.family === 'arco' || h.family?.startsWith('tomo_')) ? 'Left' : 'Right';   // mão da arma
    const off = lead === 'Left' ? 'Right' : 'Left';
    const sg = lead === 'Left' ? -1 : 1;
    // arma de alcance: braço estendido na linha do tiro. Corpo a corpo: guarda alta, cotovelo dobrado
    const tiro = h.family === 'arco' || h.family === 'besta' || h.family?.startsWith('tomo_') || h.family?.startsWith('cajado_');
    // os braços partem da pose de repouso: assim a pose não depende do ponto do ciclo da caminhada
    for (const k of [lead + 'Arm', lead + 'ForeArm', off + 'Arm', off + 'ForeArm']) ampBone(h, k, 1 - aw);
    bend(h, 'Spine', 0, 1, 0, sg * 0.10 * aw);            // ombro da arma à frente
    bend(h, 'Spine1', 0, 1, 0, sg * 0.12 * aw);
    bend(h, 'Neck', 0, 1, 0, -sg * 0.16 * aw);            // cabeça volta para o alvo
    bend(h, 'Head', 1, 0, 0, -0.05 * aw);
    bend(h, lead + 'Arm', 0, 0, 1, sg * 0.10 * aw);
    bend(h, lead + 'Arm', 1, 0, 0, (tiro ? -1.00 : -0.62) * aw);
    bend(h, lead + 'ForeArm', 1, 0, 0, (tiro ? 0.27 : -0.34) * aw);
    bend(h, off + 'Arm', 0, 0, 1, sg * 0.18 * aw);        // cotovelo de trás aberto
    bend(h, off + 'Arm', 1, 0, 0, (tiro ? -0.85 : -0.55) * aw);
    bend(h, off + 'ForeArm', 1, 0, 0, (tiro ? -1.45 : -0.95) * aw);   // mão de apoio à frente do peito

    // Inclinação vertical dinâmica (mira em 3D: tronco e braços acompanham a mira para cima ou baixo)
    const ap = s.aimPitch || 0;
    if (Math.abs(ap) > 0.001) {
      bend(h, 'Spine', 1, 0, 0, -ap * 0.35 * aw);
      bend(h, 'Spine1', 1, 0, 0, -ap * 0.25 * aw);
      bend(h, 'Neck', 1, 0, 0, -ap * 0.18 * aw);
      bend(h, 'Head', 1, 0, 0, -ap * 0.12 * aw);
      bend(h, lead + 'Arm', 1, 0, 0, -ap * 0.45 * aw);
      bend(h, off + 'Arm', 1, 0, 0, -ap * 0.35 * aw);
    }
  }

  if (s.metamorph) {
    // Postura bestial (urso / fera)
    bend(h, 'Spine', 1, 0, 0, 0.38);
    bend(h, 'Spine1', 1, 0, 0, 0.22);
    bend(h, 'Neck', 1, 0, 0, -0.32);
    bend(h, 'LeftArm', 1, 0, 0, -0.85); bend(h, 'RightArm', 1, 0, 0, -0.85);
    bend(h, 'LeftForeArm', 1, 0, 0, -0.8); bend(h, 'RightForeArm', 1, 0, 0, -0.8);
    bend(h, 'LeftArm', 0, 0, 1, 0.3); bend(h, 'RightArm', 0, 0, 1, -0.3);
  }

  if (down) {
    if (deathAct || h.actions?.down) {
      postura(h, dt, 0);
      return;
    }
    h.body.rotation.x = -Math.PI / 2 + 0.08; h.body.position.set(0, 0.16, -0.2);
    bend(h, 'LeftArm', 0, 0, 1, 0.6); bend(h, 'RightArm', 0, 0, 1, -0.5);
    return;
  }
  if (s.mounted) {
    for (const [k, side] of [['LeftUpLeg', 1], ['RightUpLeg', -1]]) { bend(h, k, 0, 0, 1, side * 0.42); bend(h, k, 1, 0, 0, -1.35); }
    bend(h, 'LeftLeg', 1, 0, 0, 1.45); bend(h, 'RightLeg', 1, 0, 0, 1.45);
    bend(h, 'LeftArm', 1, 0, 0, -0.55); bend(h, 'RightArm', 1, 0, 0, -0.55);
    bend(h, 'LeftForeArm', 1, 0, 0, -0.7); bend(h, 'RightForeArm', 1, 0, 0, -0.7);
    h.body.position.y = Math.sin(h.phase * 2) * 0.03 * Math.min(1, mps);
    return;
  }
  if (s.dodge >= 0) {
    if (dodgeAct || h.actions?.roll) {
      postura(h, dt, w * 0.5);
      return;
    }
    // rolamento em torno do quadril, encolhido (fallback)
    const th = s.dodge * Math.PI * 2, hy = h.hipsY, cy = hy - 0.45 * Math.sin(s.dodge * Math.PI);
    h.body.rotation.x = th; h.body.position.set(0, cy - hy * Math.cos(th), -hy * Math.sin(th));
    bend(h, 'Spine', 1, 0, 0, 0.5);
    bend(h, 'LeftUpLeg', 1, 0, 0, -1.7); bend(h, 'RightUpLeg', 1, 0, 0, -1.7);
    bend(h, 'LeftLeg', 1, 0, 0, 2.0); bend(h, 'RightLeg', 1, 0, 0, 2.0);
    return;
  }
  if (s.channel) {
    bend(h, 'Spine', 1, 0, 0, 0.3);
    bend(h, 'RightArm', 1, 0, 0, -1.25 + Math.sin(h.phase * 1.8) * 0.7);
    bend(h, 'RightForeArm', 1, 0, 0, -0.4);
    bend(h, 'LeftArm', 1, 0, 0, -0.7);
  }
  if (s.attack >= 0 && !bowShot) {
    if (activeCombatAct) {
      // Clipes mocap Mixamo executam o ataque realisticamente.
      // Aplicamos impulso de inércia no tronco (follow-through) e mira vertical em 3D.
      const t = s.attack;
      const impactT = Math.sin(clamp((t - 0.2) / 0.5, 0, 1) * Math.PI);
      bend(h, 'Spine', 1, 0, 0, 0.09 * impactT);
      const ap = s.aimPitch || 0;
      if (Math.abs(ap) > 0.001) {
        bend(h, 'Spine', 1, 0, 0, -ap * 0.28);
        bend(h, 'Spine1', 1, 0, 0, -ap * 0.20);
        bend(h, 'Head', 1, 0, 0, -ap * 0.15);
      }
    } else {
      // Fallback procedural se não houver o clipe mocap
      const t = s.attack;
      if (s.kind === 'bow' || s.kind === 'charge') {
        const pull = s.kind === 'charge' ? Math.min(1, t * 1.2) : Math.min(1, t * 2);
        bend(h, 'Spine', 0, 1, 0, -0.35);
        bend(h, 'LeftArm', 1, 0, 0, -1.5); bend(h, 'LeftArm', 0, 1, 0, 0.35);
        bend(h, 'RightArm', 1, 0, 0, -1.45); bend(h, 'RightArm', 0, 1, 0, 0.2 - pull * 0.5);
        bend(h, 'RightForeArm', 0, 1, 0, -0.4 - pull * 1.7);
      } else if (s.kind === 'crossbow') {
        bend(h, 'Spine', 0, 1, 0, -0.2);
        bend(h, 'RightArm', 1, 0, 0, -1.45); bend(h, 'LeftArm', 1, 0, 0, -1.35);
        bend(h, 'LeftForeArm', 1, 0, 0, -0.65);
      } else if (s.kind === 'spin') {
        h.body.rotation.y = t * Math.PI * 2;
        bend(h, 'RightArm', 0, 0, 1, -1.25); bend(h, 'RightArm', 1, 0, 0, -0.5);
        bend(h, 'LeftArm', 0, 0, 1, 1.2);
      } else if (s.kind === 'thrust') {
        bend(h, 'Spine', 1, 0, 0, 0.3);
        bend(h, 'RightArm', 1, 0, 0, -1.5); bend(h, 'RightForeArm', 1, 0, 0, 0.15);
        bend(h, 'LeftUpLeg', 1, 0, 0, -0.7); bend(h, 'RightUpLeg', 1, 0, 0, 0.55);
        bend(h, 'LeftLeg', 1, 0, 0, 0.6);
      } else if (s.kind === 'rapid') {
        const punch = Math.sin(t * Math.PI * 4);
        bend(h, 'RightArm', 1, 0, 0, -1.35 - punch * 0.45);
        bend(h, 'LeftArm', 1, 0, 0, -1.35 + punch * 0.45);
        bend(h, 'Spine', 0, 1, 0, punch * 0.22);
      } else if (s.kind === 'heavy') {
        const up = t < 0.4 ? t / 0.4 : 1 - (t - 0.4) / 0.6;
        bend(h, 'RightArm', 1, 0, 0, -2.8 * up + 0.3 * (1 - up));
        bend(h, 'LeftArm', 1, 0, 0, -2.4 * up + 0.2 * (1 - up));
        bend(h, 'Spine', 1, 0, 0, -0.15 * up + 0.35 * (1 - up));
      } else if (s.kind === 'cast') {
        const wave = Math.sin(t * Math.PI);
        bend(h, 'Spine', 1, 0, 0, -0.15 * wave);
        bend(h, 'RightArm', 1, 0, 0, -1.6 - wave * 0.35);
        bend(h, 'LeftArm', 1, 0, 0, -1.2 - wave * 0.25);
        bend(h, 'RightForeArm', 0, 1, 0, 0.45 * wave);
      } else if (s.kind === 'slash') {
        h.body.rotation.y = (t - 0.5) * 1.6;
        bend(h, 'RightArm', 0, 0, 1, -1.1); bend(h, 'RightArm', 1, 0, 0, -0.7 + t * 0.9);
        bend(h, 'LeftArm', 0, 0, 1, 0.85);
      } else {
        // golpe descendente padrão
        const up = t < 0.35 ? t / 0.35 : 1 - (t - 0.35) / 0.65;
        const a = t < 0.35 ? -2.7 * up : -2.7 + ((t - 0.35) / 0.65) * 2.4;
        bend(h, 'Spine', 0, 1, 0, t < 0.35 ? 0.35 * up : 0.35 - (t - 0.35) * 1.2);
        bend(h, 'Spine', 1, 0, 0, t < 0.35 ? -0.08 * up : 0.25 * (1 - up));
        bend(h, 'RightArm', 1, 0, 0, a); bend(h, 'RightArm', 0, 0, 1, -0.25);
        bend(h, 'RightForeArm', 1, 0, 0, -0.5 * up);
        bend(h, 'LeftArm', 1, 0, 0, -0.45);
      }
    }
  }
  postura(h, dt, w);
}

// Inclina o corpo: para dentro da curva (quanto mais rápido, mais) e conforme a encosta sob os pés.
// Estabilização de cabeça: a cabeça e olhar compensam a inclinação para manter a linha do horizonte.
function postura(h, dt, w) {
  let d = h.root.rotation.y - h.prevYaw;
  while (d > Math.PI) d -= Math.PI * 2;
  while (d < -Math.PI) d += Math.PI * 2;
  h.prevYaw = h.root.rotation.y;
  const turnRate = clamp(d / Math.max(dt, 1e-3) * 0.055, -0.22, 0.22);
  h.lean = approach(h.lean, turnRate * w, dt * 5);
  const p = h.root.position, sy = Math.sin(h.root.rotation.y), cy = Math.cos(h.root.rotation.y), r = 0.55;
  // declive à frente e para o lado, no eixo do personagem
  const fx = groundHeight(p.x + sy * r, p.z + cy * r) - groundHeight(p.x - sy * r, p.z - cy * r);
  const rx = groundHeight(p.x + cy * r, p.z - sy * r) - groundHeight(p.x - cy * r, p.z + sy * r);
  h.tiltX = approach(h.tiltX, clamp(Math.atan2(fx, 2 * r) * 0.45, -0.18, 0.18), dt * 4.5);
  h.tiltZ = approach(h.tiltZ, clamp(Math.atan2(rx, 2 * r) * 0.45, -0.18, 0.18), dt * 4.5);
  h.body.rotation.x += h.tiltX;
  h.body.rotation.z += h.lean - h.tiltZ;

  // Estabilização vestíbulo-ocular: pescoço e cabeça compensam a inclinação do corpo
  bend(h, 'Neck', 1, 0, 0, -h.tiltX * 0.4);
  bend(h, 'Head', 1, 0, 0, -h.tiltX * 0.25);
  bend(h, 'Neck', 0, 0, 1, -(h.lean - h.tiltZ) * 0.45);
  bend(h, 'Head', 0, 0, 1, -(h.lean - h.tiltZ) * 0.3);
}

// Postura e alinhamento de terreno e curvas para quadrúpedes
function postureQuadruped(r, dt, w) {
  let d = r.root.rotation.y - r.prevYaw;
  while (d > Math.PI) d -= Math.PI * 2;
  while (d < -Math.PI) d += Math.PI * 2;
  r.prevYaw = r.root.rotation.y;
  const turnRate = clamp(d / Math.max(dt, 1e-3) * 0.08, -0.25, 0.25);
  r.lean = approach(r.lean, turnRate * w, dt * 6);
  const p = r.root.position, sy = Math.sin(r.root.rotation.y), cy = Math.cos(r.root.rotation.y), rDist = 1.1 * (r.scale || 1);
  const fx = groundHeight(p.x + sy * rDist, p.z + cy * rDist) - groundHeight(p.x - sy * rDist, p.z - cy * rDist);
  const rx = groundHeight(p.x + cy * 0.6, p.z - sy * 0.6) - groundHeight(p.x - cy * 0.6, p.z + sy * 0.6);
  r.tiltX = approach(r.tiltX, clamp(Math.atan2(fx, 2 * rDist) * 0.6, -0.28, 0.28), dt * 5);
  r.tiltZ = approach(r.tiltZ, clamp(Math.atan2(rx, 1.2) * 0.5, -0.22, 0.22), dt * 5);
  r.body.rotation.x += r.tiltX;
  r.body.rotation.z += r.lean - r.tiltZ;
}

// ---------- criatura (Garras): leoa ----------
function quadruped(M, { scale = 1, tint = null, emissive = null } = {}) {
  const root = track(new THREE.Group());
  const body = new THREE.Group(); root.add(body);
  const pivot = instantiate(M); body.add(pivot); oncePerFrame(pivot);
  body.scale.setScalar(scale);
  if (tint || emissive) paint(pivot, (m) => variant(m, 'q' + tint + emissive, (b) => {
    const c = b.clone(); if (tint) c.color.set(tint); if (emissive) { c.emissive.set(emissive); c.emissiveIntensity = 1; } return c;
  }));
  const bones = boneMap(pivot), base = basePose(bones);
  for (const [b, q] of base) q.copy(b.quaternion);
  const mixer = new THREE.AnimationMixer(pivot);
  const idle = mixer.clipAction(M.idle).play(), walk = mixer.clipAction(M.walkClip).play();
  walk.setEffectiveWeight(0); idle.time = Math.random() * M.idle.duration;
  const r = {
    root, body, pivot, M, mixer, idle, walk, bones, base, walkW: 0, acc: 0, tick: 0,
    phase: Math.random() * 6, scale, lean: 0, tiltX: 0, tiltZ: 0, prevYaw: root.rotation.y,
  };
  mixerStep(r, 0);
  return r;
}
function stepQuadruped(r, dt, mps, base, maxTs, frozen) {
  r.walkW = approach(r.walkW, !frozen && mps > 0.4 ? 1 : 0, dt * 6);
  r.walk.timeScale = clamp(mps / (base * r.scale), 0.6, maxTs);
  r.idle.timeScale = frozen ? 0 : 1;
  r.walk.setEffectiveWeight(r.walkW); r.idle.setEffectiveWeight(1 - r.walkW);
  mixerStep(r, dt);
}

export function makeBeast({ scale = 1, mane = false, pale = false } = {}) {
  if (MODELS.hound) {
    // corpo do cão do vazio: a líder ganha brilho violeta, a pálida um tom claro
    const tint = pale ? '#c9c3d6' : null, emissive = mane ? '#4a2470' : pale ? '#2a2632' : null;
    return hound(MODELS.hound, { scale: scale * (mane ? 1.12 : 1), tint, emissive });
  }
  const tint = mane ? '#b08a6c' : pale ? '#e6e0d6' : null;
  return quadruped(MODELS.lioness, { scale: scale * (mane ? 1.12 : 1), tint, emissive: pale ? '#2e2a26' : null });
}

// ---------- criatura (Garras): cão do vazio ----------
// Malha estática, sem esqueleto: o corpo inteiro flutua acima do chão e se move como bloco. Os mesmos
// campos de estado da leoa (mps, down, stun, windup, attack) dirigem a pose.
function hound(M, { scale = 1, tint = null, emissive = null } = {}) {
  const root = track(new THREE.Group());
  const body = new THREE.Group(); root.add(body);
  const pivot = instantiate(M); body.add(pivot);
  body.scale.setScalar(scale);
  if (tint || emissive) paint(pivot, (m) => variant(m, 'h' + tint + emissive, (b) => {
    const c = b.clone(); if (tint) c.color.set(tint); if (emissive) { c.emissive.set(emissive); c.emissiveIntensity = 0.6; } return c;
  }));
  return {
    root, body, pivot, M, isHound: true, walkW: 0, acc: 0, tick: 0, downT: 0,
    phase: Math.random() * 6, seed: Math.random() * 6, scale, lean: 0, tiltX: 0, tiltZ: 0, prevYaw: root.rotation.y,
  };
}
const HOVER = 0.3;   // altura do corpo acima do chão, em metros (escala 1)
function animateHound(r, s, dt) {
  r.phase += dt;
  const k = r.scale, mps = s.mps != null ? s.mps : (s.speed || 0) * 8;
  r.walkW = approach(r.walkW, !s.down && mps > 0.4 ? 1 : 0, dt * 6);
  r.body.rotation.set(0, 0, 0); r.body.position.set(0, 0, 0);
  if (s.down) {
    // desaba de lado e assenta no chão
    r.downT = Math.min(1, r.downT + dt * 1.8);
    const d = r.downT * r.downT * (3 - 2 * r.downT);
    r.body.rotation.z = d * Math.PI * 0.45;
    r.body.position.y = HOVER * k * (1 - d) + 0.18 * k * d;
    return;
  }
  r.downT = 0;
  // paira e respira; andando, abaixa a cabeça e desliza
  r.body.position.y = HOVER * k + Math.sin(r.phase * 2.1 + r.seed) * 0.07 * k + Math.sin(r.phase * 6.2) * 0.02 * k * r.walkW;
  r.body.rotation.x = 0.16 * r.walkW * Math.min(1, mps / 6);
  r.body.rotation.z = Math.sin(r.phase * 1.3 + r.seed) * 0.05;
  const pulse = 1 + Math.sin(r.phase * 2.6 + r.seed) * 0.025;
  r.body.scale.set(k * pulse, k * (2 - pulse), k * pulse);
  if (s.windup >= 0) {   // recua e se encolhe antes do bote
    r.body.position.z -= 0.35 * s.windup * k;
    r.body.position.y -= 0.14 * s.windup * k;
    r.body.rotation.x += 0.24 * s.windup;
  }
  if (s.attack >= 0) {   // bote: avança e ergue a frente
    const t = Math.sin(s.attack * Math.PI);
    r.body.position.z += 1.0 * t * k;
    r.body.position.y += 0.16 * t * k;
    r.body.rotation.x -= 0.32 * t;
  }
  if (s.stun) r.body.rotation.z += Math.sin(r.phase * 14) * 0.25;
  postureQuadruped(r, dt, r.walkW);
}
// s: {speed, mps, down, stun, windup(-1|0..1), attack(-1|0..1), dt}
export function animateBeast(b, s) {
  const dt = lodStep(b, s.dt || 0.016);
  if (!dt) return;
  if (b.isHound) { animateHound(b, s, dt); return; }
  b.phase += dt;
  stepQuadruped(b, dt, s.mps != null ? s.mps : (s.speed || 0) * 8, 2.5, 2.4, s.down);
  b.body.rotation.set(0, 0, 0); b.body.position.set(0, 0, 0);
  if (s.down) { b.body.rotation.z = Math.PI / 2; b.body.position.y = 0.32 * b.scale; return; }
  if (s.windup >= 0) {   // agacha e recua antes do bote
    b.body.position.set(0, -0.1 * s.windup * b.scale, -0.3 * s.windup);
    b.body.rotation.x = 0.1 * s.windup;
    bend(b, 'Head', 1, 0, 0, 0.3 * s.windup);
  }
  if (s.attack >= 0) {   // bote: avança, ergue a frente e abre a boca
    const t = Math.sin(s.attack * Math.PI);
    b.body.position.z = 0.9 * t; b.body.rotation.x = -0.22 * t;
    bend(b, 'Head', 1, 0, 0, -0.35 * t); bend(b, 'Jaw', 1, 0, 0, 0.45 * t);
  }
  if (s.stun) bend(b, 'Head', 0, 0, 1, Math.sin(b.phase * 14) * 0.25);
  postureQuadruped(b, dt, b.walkW);
}

// ---------- montaria: cavalo ----------
export function makeMount() {
  const M = MODELS.horse;
  const m = quadruped(M);
  m.seatY = M.seatY; m.seatZ = M.seatZ;
  return m;
}
export function animateMount(m, s) {
  const dt = lodStep(m, s.dt || 0.016);
  if (!dt) return;
  stepQuadruped(m, dt, (s.speed || 0) * 12.5, 1.9, 3.4, false);
  m.body.rotation.set(0, 0, 0); m.body.position.set(0, 0, 0);
  postureQuadruped(m, dt, m.walkW);
}
