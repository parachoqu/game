// Assets extras na cena: trocas só visuais de volumes do Blender e os acréscimos de `extra-spots.js`.
//
// Troca visual: o nó do `region-props.glb` fica invisível e o modelo novo entra na mesma caixa medida
// no manifesto. Colisão, tabuleiro das pontes, rampas e parapeitos continuam vindo do manifesto
// (`world-colliders.js`) — nada aqui muda por onde o jogador anda. Escala, giro e pegada vêm de
// `extra-seat.js` (a mesma conta usada pelas clareiras da vegetação).
//
// Assentamento: cada modelo traz no manifesto o plano de contato da base e a pegada. A altura sai do
// chão amostrado sob a pegada (não do vértice mais baixo nem de nove pontos num raio arbitrário), com
// um embutimento pequeno; onde o chão cai abaixo desse datum, a base desce até ele (fundação), e nada
// flutua. Pontes e doca mantêm o datum funcional (tabuleiro, piso do cais).
import * as THREE from 'three';
import { EXTRA_BINS, EXTRA_META } from '../engine/extra-assets.js';
import { parseSceneGLB } from './runtime-loader.js';
import { patchOcclusion } from './world-materials.js';
import { EXTRA_SPOTS } from './extra-spots.js';
import { regionHeightAt } from './heightfield.js';
import { REPLACEMENTS, replacementFit, additionFit, seatOf, EMBED, bridgeFrame } from './extra-seat.js';
import { buildRoadway } from './bridge-roadway.js';

export { REPLACEMENTS };
// modelos que `game/world.js` pede já carregados (barracas do Alto, torre da Passagem)
const GAME_MODELS = ['banca', 'torre_escombros'];
const REPLACED = new Map(REPLACEMENTS.map((r) => [r.volume, r]));
// nó do region-props → volume substituído ("NorthHouse_01_Roof" → "NorthHouse_01")
export function replacedVolume(nodeName) {
  const v = nodeName.replace(/_(Facade|Rear|Roof)$/, '');
  return REPLACED.has(v) ? v : null;
}

export const EXTRA_STATUS = { replaced: [], added: [], game: [], hidden: 0, ms: 0, parse: 0, drape: 0, bridge: 0, seats: [] };

// ---------------------------------------------------------------- modelos
const templates = new Map();
// Peças que não projetam sombra: interiores, móveis, chão decalcado e miudezas.
const NO_SHADOW = /Interior|Furniture|WeaponsTools|Ground|details|barrel/i;

async function template(id) {
  if (templates.has(id)) return templates.get(id);
  const meta = EXTRA_META[id];
  const gltf = await parseSceneGLB(EXTRA_BINS[id]);
  const scene = gltf.scene;
  // recortes de textura para o tabuleiro das pontes: presos a uma amostra mínima, que sai da cena
  const roadway = {};
  const samples = scene.getObjectByName('_amostras');
  if (samples) {
    samples.traverse((o) => { if (o.isMesh) for (const m of [o.material].flat()) roadway[m.name] = m; });
    samples.parent.remove(samples);
  }
  scene.traverse((o) => {
    if (!o.isMesh) return;
    o.receiveShadow = true;
    for (const m of [o.material].flat()) { m.envMapIntensity = meta?.extra?.stone ? 0.2 : 0.35; patchOcclusion(m); }
  });
  for (const m of Object.values(roadway)) { m.envMapIntensity = 0.2; patchOcclusion(m); }
  scene.userData.roadway = roadway;
  templates.set(id, scene);
  return scene;
}

// Carrega em paralelo (lotes de 4, na ordem da lista) — o posicionamento continua sequencial.
async function preload(ids) {
  const t0 = performance.now();
  const todo = [...new Set(ids)].filter((id) => EXTRA_BINS[id] && !templates.has(id));
  for (let i = 0; i < todo.length; i += 4) {
    await Promise.all(todo.slice(i, i + 4).map((id) => template(id).catch((e) => console.warn(`extra ${id}: ${e.message}`))));
  }
  EXTRA_STATUS.parse += Math.round(performance.now() - t0);
}
const place = (tpl) => tpl.clone(true);

// Sombra só do que tem volume: a casca do prédio projeta; interiores, peças pequenas e o chão
// decalcado não.
const _s = new THREE.Vector3(), _q = new THREE.Quaternion(), _t = new THREE.Vector3();
function shadows(obj) {
  obj.updateMatrixWorld(true);
  obj.traverse((o) => {
    if (!o.isMesh) return;
    if (!o.geometry.boundingSphere) o.geometry.computeBoundingSphere();
    o.matrixWorld.decompose(_t, _q, _s);
    const r = o.geometry.boundingSphere.radius * Math.max(_s.x, _s.y, _s.z);
    o.castShadow = r >= 0.9 && ![o.material].flat().some((m) => NO_SHADOW.test(m.name));
  });
}

// telhado das bancas: material próprio por cópia, na cor do tecido
function tintRoof(obj, color) {
  obj.traverse((o) => {
    if (!o.isMesh || o.material.name !== 'telhado') return;
    o.material = o.material.clone();
    o.material.color.set(color);
  });
}

// ---------------------------------------------------------------- assentamento
// Fundação: os vértices da base (até `band` acima do plano de contato, em unidades do modelo) descem
// até o chão onde ele fica abaixo deles. A geometria é copiada só quando algo muda.
const _w = new THREE.Vector3(), _m = new THREE.Vector3(), _inv = new THREE.Matrix4(), _toModel = new THREE.Matrix4(), _rel = new THREE.Matrix4();
function drapeBase(obj, band) {
  const t0 = performance.now();
  obj.updateMatrixWorld(true);
  _toModel.copy(obj.matrixWorld).invert();
  let moved = 0;
  obj.traverse((o) => {
    if (!o.isMesh) return;
    const src = o.geometry.attributes.position;
    let arr = null;
    _inv.copy(o.matrixWorld).invert();
    // pré-filtro: com a malha só escalada/deslocada em relação ao modelo (a quantização do GLB), a
    // altura no modelo sai de uma conta por vértice
    _rel.multiplyMatrices(_toModel, o.matrixWorld);
    const e = _rel.elements, axis = Math.abs(e[1]) + Math.abs(e[9]) < 1e-6 && e[5] > 0;
    for (let i = 0; i < src.count; i++) {
      if (axis && src.getY(i) * e[5] + e[13] > band) continue;
      _w.fromBufferAttribute(src, i).applyMatrix4(o.matrixWorld);
      _m.copy(_w).applyMatrix4(_toModel);
      if (_m.y > band) continue;
      const h = regionHeightAt(_w.x, _w.z) - EMBED;
      if (h >= _w.y) continue;
      if (!arr) {
        arr = new Float32Array(src.count * 3);
        for (let k = 0; k < src.count; k++) { arr[k * 3] = src.getX(k); arr[k * 3 + 1] = src.getY(k); arr[k * 3 + 2] = src.getZ(k); }
      }
      _w.y = h;
      _w.applyMatrix4(_inv);
      arr[i * 3] = _w.x; arr[i * 3 + 1] = _w.y; arr[i * 3 + 2] = _w.z;
      moved++;
    }
    if (arr) {
      // geometria nova só com a posição própria; normais, UV e índices continuam compartilhados
      const src0 = o.geometry, g = new THREE.BufferGeometry();
      for (const [k, a] of Object.entries(src0.attributes)) g.setAttribute(k, k === 'position' ? new THREE.BufferAttribute(arr, 3) : a);
      g.setIndex(src0.index);
      for (const gr of src0.groups) g.addGroup(gr.start, gr.count, gr.materialIndex);
      g.computeBoundingSphere();
      o.geometry = g;
    }
  });
  EXTRA_STATUS.drape += performance.now() - t0;
  return moved;
}
const drapeBand = (meta) => (meta.extra.contactY || 0) + (meta.extra.support === 'point' ? 0.03 : 0.004) * meta.size[1];

// Põe o modelo no chão pela pegada (flat/point) e registra o que foi feito.
function seat(obj, f, name) {
  obj.rotation.y = f.yaw;
  obj.scale.set(...f.s);
  const st = seatOf(f);
  obj.position.set(f.x, st.y, f.z);
  const moved = st.drape ? drapeBase(obj, drapeBand(f.meta)) : 0;
  EXTRA_STATUS.seats.push({ name, datum: +st.datum.toFixed(2), lo: +st.lo.toFixed(2), hi: +st.hi.toFixed(2), drape: moved });
  return st;
}

const OLD_RAILS = /gaurd_rail|Slab_Stone/;
// Pontes: o vão de pedra sem deformar, com a pista antiga logo abaixo do piso de colisão, e o
// tabuleiro plano por cima (`bridge-roadway.js`).
function placeBridge(obj, f, tpl) {
  const t0 = performance.now();
  const deck = f.p.deck_final ?? f.p.deck ?? f.p.base;
  obj.rotation.y = f.yaw;
  obj.scale.set(...f.s);
  obj.position.set(f.x, deck - 0.12 - f.meta.extra.deckY * f.s[1], f.z);
  // parapeitos e tampas antigos (em corcova) saem: as paredes do tabuleiro ocupam o lugar deles
  obj.traverse((o) => { if (o.isMesh && OLD_RAILS.test(o.material.name)) o.visible = false; });
  obj.updateMatrixWorld(true);
  // topo da pista antiga ao longo do eixo (faixa central), para as paredes novas descerem até ela
  const fr = bridgeFrame(f.r.volume);
  if (!fr) return null;
  const bins = new Map(), BIN = 0.5;
  obj.traverse((o) => {
    if (!o.isMesh || !o.visible) return;
    const p = o.geometry.attributes.position;
    for (let i = 0; i < p.count; i++) {
      _w.fromBufferAttribute(p, i).applyMatrix4(o.matrixWorld);
      const t = fr.alongZ ? _w.z - fr.b.z : _w.x - fr.b.x, c = fr.alongZ ? _w.x - fr.b.x : _w.z - fr.b.z;
      if (Math.abs(c) > fr.half * 0.8) continue;
      const k = Math.round(t / BIN);
      if (!bins.has(k) || bins.get(k) < _w.y) bins.set(k, _w.y);
    }
  });
  const oldTop = (t) => { const k = Math.round(t / BIN); return bins.get(k) ?? bins.get(k - 1) ?? bins.get(k + 1) ?? -Infinity; };
  const roadway = buildRoadway(f.r.volume, tpl.userData.roadway || {}, oldTop);
  EXTRA_STATUS.bridge += performance.now() - t0;
  return roadway;
}

// ---------------------------------------------------------------- cena
let group = null;
function holder(scene) {
  if (!group) { group = new THREE.Group(); group.name = 'extra-props'; scene.add(group); }
  return group;
}

// Trocas visuais. Chamado por `loadRegionProps` depois de esconder os nós substituídos.
export async function loadExtraReplacements(scene) {
  const t0 = performance.now();
  const group = holder(scene);
  await preload([...REPLACEMENTS.map((r) => r.asset), ...GAME_MODELS]);
  for (const r of REPLACEMENTS) {
    const f = replacementFit(r);
    const tpl = templates.get(r.asset);
    if (!f || !tpl) continue;
    const obj = place(tpl);
    obj.name = `extra:${r.volume}`;
    if (r.fit === 'ponte') {
      const roadway = placeBridge(obj, f, tpl);
      if (roadway) group.add(roadway);
    } else if (r.fit === 'doca') {
      obj.rotation.y = f.yaw; obj.scale.set(...f.s);
      obj.position.set(f.x, r.deck - f.meta.extra.deckY * f.s[1], f.z);
    } else {
      seat(obj, f, r.volume);
    }
    if (r.tint) tintRoof(obj, r.tint);
    shadows(obj);
    obj.userData.cull = cullRadius(f);
    group.add(obj);
    EXTRA_STATUS.replaced.push(r.volume);
  }
  group.updateMatrixWorld(true);
  EXTRA_STATUS.ms += Math.round(performance.now() - t0);
}

// Modelo para um objeto que o jogo monta (barraca do Alto, torre da Passagem): cópia na escala da
// pegada `w` × `d`, frente para +Z, assentada pelo chão sob a pegada no ponto (x, z) com giro `yaw`
// (os mesmos que `game/world.js` passa para `place`, que põe o grupo na altura do chão no centro).
// `null` se o modelo não carregou — o jogo fica com a peça procedural de antes.
export function extraModel(id, { w, d, fit = 'caixa', tint = null, x = 0, z = 0, yaw = 0 }) {
  const tpl = templates.get(id), meta = EXTRA_META[id];
  if (!tpl || !meta) return null;
  const obj = place(tpl);
  const [sx, , sz] = meta.size;
  const s = fit === 'redonda' ? Math.min(w, d) / Math.max(sx, sz) : Math.min(w / sx, d / sz);
  const g = new THREE.Group();
  g.name = `extra:${id}`;
  g.add(obj);
  // assenta com o grupo já no lugar e passa a altura para o referencial do grupo
  const base = regionHeightAt(x, z);
  g.position.set(x, base, z); g.rotation.y = yaw;
  obj.scale.setScalar(s);
  const st = seatOf({ meta, x, z, yaw, s: [s, s, s] });
  obj.position.y = st.y - base;
  g.updateMatrixWorld(true);
  if (st.drape) drapeBase(obj, drapeBand(meta));
  EXTRA_STATUS.seats.push({ name: `jogo:${id}`, datum: +st.datum.toFixed(2), lo: +st.lo.toFixed(2), hi: +st.hi.toFixed(2) });
  if (tint) tintRoof(obj, tint);
  shadows(g);
  EXTRA_STATUS.game.push(id);
  return g;
}

// Acréscimos nos pontos de `extra-spots.js` (escolhidos com as áreas funcionais, antes da vegetação).
export async function loadExtraAdditions(scene) {
  const t0 = performance.now();
  const group = holder(scene);
  await preload(EXTRA_SPOTS.map((s) => s.asset));
  for (const s of EXTRA_SPOTS) {
    const tpl = templates.get(s.asset), f = additionFit(s);
    if (!tpl || !f) continue;
    const obj = place(tpl);
    obj.name = `extra:${s.id}`;
    seat(obj, f, s.id);
    shadows(obj);
    if (s.asset === 'arvore_marco') {
      // dois níveis: o modelo inteiro perto, a cópia reduzida (sem sombra) longe
      const lod = new THREE.LOD();
      const l0 = obj.getObjectByName('L0'), l1 = obj.getObjectByName('L1');
      if (l0 && l1) {
        l1.traverse((o) => { if (o.isMesh) o.castShadow = false; });
        lod.addLevel(l0, 0); lod.addLevel(l1, 70);
        obj.clear();
        obj.add(lod);
      }
    }
    obj.userData.cull = cullRadius(f);
    group.add(obj);
    EXTRA_STATUS.added.push(s.id);
  }
  group.updateMatrixWorld(true);
  EXTRA_STATUS.ms += Math.round(performance.now() - t0);
}

// ---------------------------------------------------------------- detalhe por distância
// Raio que o modelo ocupa na cena: os pequenos saem de cena mais cedo que os grandes.
function cullRadius(f) {
  const [sx, sy, sz] = f.meta.size;
  return 0.5 * Math.hypot(sx * f.s[0], sy * f.s[1], sz * f.s[2]);
}
// Esconde os extras além do alcance: prédios e torres até `far`; peças com menos de 4 m de raio,
// até 45% disso. O tabuleiro das pontes fica sempre.
export function updateExtraDetail(cam, far = 900) {
  if (!group) return;
  for (const o of group.children) {
    const r = o.userData.cull;
    if (!r) continue;
    const lim = (r < 4 ? far * 0.45 : far) + r;
    o.visible = (o.position.x - cam.x) ** 2 + (o.position.z - cam.z) ** 2 < lim * lim;
  }
}
