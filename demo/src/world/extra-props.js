// Assets extras na cena: trocas só visuais de volumes do Blender e os acréscimos de `extra-spots.js`.
//
// Troca visual: o nó do `region-props.glb` fica invisível e o modelo novo entra na mesma caixa medida
// no manifesto. Colisão, tabuleiro das pontes, rampas e parapeitos continuam vindo do manifesto
// (`world-colliders.js`) — nada aqui muda por onde o jogador anda.
//   'caixa'   casas, oficina, bancas e ruínas: escala uniforme para caber na pegada (sem passar de 1,6×
//             a altura antiga), frente virada para o lado em que a fachada original estava (posição do
//             nó _Facade em relação ao _Rear) ou para `yaw`, nos volumes de nó único
//   'redonda' torres: o diâmetro do modelo vira o diâmetro do colisor redondo
//   'ponte'   comprimento e largura casados com a caixa; o piso da pista na altura do tabuleiro
//   'doca'    escala da pegada; o piso de tábuas na altura do piso do cais antigo (`deck`)
// `tint` pinta o telhado das bancas com o tecido que o mercado já usava.
import * as THREE from 'three';
import { EXTRA_BINS, EXTRA_META } from '../engine/extra-assets.js';
import { parseSceneGLB } from './runtime-loader.js';
import { patchOcclusion } from './world-materials.js';
import { PLACEMENTS } from './runtime-manifest.js';
import { PLAN_Z_SIGN } from './coordinates.js';
import { EXTRA_SPOTS } from './extra-spots.js';
import { regionHeightAt } from './heightfield.js';

export const REPLACEMENTS = [
  { asset: 'ponte_pedra', volume: 'Bridge_Main', fit: 'ponte' },
  { asset: 'ferraria', volume: 'SouthWorkshop', fit: 'caixa' },
  { asset: 'casa_palha', volume: 'NorthHouse_01', fit: 'caixa' },
  { asset: 'casa_palha', volume: 'NorthHouse_02', fit: 'caixa' },
  { asset: 'casa_palha', volume: 'NorthHouse_03', fit: 'caixa' },
  { asset: 'torre_a', volume: 'SouthOutpost_Tower', fit: 'redonda' },
  { asset: 'torre_b', volume: 'NorthOutpost_Tower', fit: 'redonda' },
  // Vale: casas, armazém, estábulo, mercado e doca
  { asset: 'casa_s32_b', volume: 'SouthHouse_01', fit: 'caixa' },
  { asset: 'casa_s32_c', volume: 'SouthHouse_02', fit: 'caixa' },
  { asset: 'casa_pedra', volume: 'SouthHouse_03', fit: 'caixa' },
  { asset: 'casa_s32_a', volume: 'SouthHouse_04', fit: 'caixa' },
  { asset: 'armazem', volume: 'SouthWarehouse', fit: 'caixa' },
  { asset: 'casa_palha', volume: 'SouthStable', fit: 'caixa', yaw: -Math.PI / 2 },   // porta para oeste, onde fica o cuidador
  { asset: 'banca', volume: 'SouthMarket_01', fit: 'caixa', yaw: 0, tint: '#b24a3a' },   // PR.COL.tecido1
  { asset: 'banca', volume: 'SouthMarket_02', fit: 'caixa', yaw: 0, tint: '#3e7a9a' },   // PR.COL.tecido3
  { asset: 'doca', volume: 'SouthOutpost_Dock', fit: 'doca', yaw: 0, deck: 11 },   // topo das tábuas do cais antigo
  { asset: 'ponte_pedra', volume: 'Bridge_Minor', fit: 'ponte' },
  // Ermos: muros do kit Kenney e a torre com escombros
  { asset: 'muro_a', volume: 'Wastes_Ruin_01', fit: 'caixa', yaw: 0 },
  { asset: 'muro_b', volume: 'Wastes_Ruin_02', fit: 'caixa', yaw: 0 },
  { asset: 'torre_escombros', volume: 'Wastes_Ruin_03', fit: 'redonda' },
];
// modelos que `game/world.js` pede já carregados (barracas do Alto, torre da Passagem)
const GAME_MODELS = ['banca', 'torre_escombros'];
const REPLACED = new Map(REPLACEMENTS.map((r) => [r.volume, r]));
// nó do region-props → volume substituído ("NorthHouse_01_Roof" → "NorthHouse_01")
export function replacedVolume(nodeName) {
  const v = nodeName.replace(/_(Facade|Rear|Roof)$/, '');
  return REPLACED.has(v) ? v : null;
}

export const EXTRA_STATUS = { replaced: [], added: [], game: [], hidden: 0, ms: 0 };
const templates = new Map();

async function template(id) {
  if (templates.has(id)) return templates.get(id);
  const gltf = await parseSceneGLB(EXTRA_BINS[id]);
  gltf.scene.traverse((o) => {
    if (!o.isMesh) return;
    o.castShadow = true; o.receiveShadow = true;
    const mats = Array.isArray(o.material) ? o.material : [o.material];
    for (const m of mats) { m.envMapIntensity = 0.35; patchOcclusion(m); }
  });
  templates.set(id, gltf.scene);
  return gltf.scene;
}
const place = (tpl) => tpl.clone(true);

// telhado das bancas: material próprio por cópia, na cor do tecido
function tintRoof(obj, color) {
  obj.traverse((o) => {
    if (!o.isMesh || o.material.name !== 'telhado') return;
    o.material = o.material.clone();
    o.material.color.set(color);
  });
}

// modelo já normalizado pela ferramenta: base em y = 0, pegada centrada, frente para +Z
function fitReplacement(obj, meta, p, fit, front, r) {
  const x = p.plan[0], z = PLAN_Z_SIGN * p.plan[1], [hw, hd] = p.half;
  const [sx, sy, sz] = meta.size;
  if (fit === 'ponte') {
    // o eixo longo do modelo vai para o eixo longo da caixa; o piso fica no tabuleiro
    // (a escala age nos eixos do próprio modelo, antes da rotação)
    const longX = meta.extra.longAxis === 'x';
    const modelLong = longX ? sx : sz, modelWide = longX ? sz : sx;
    const targetLongZ = hd >= hw;
    obj.rotation.y = longX === targetLongZ ? Math.PI / 2 : 0;
    // meio metro a mais em cada ponta: a pedra encosta no barranco sem cobrir o começo das rampas
    const sLong = (2 * Math.max(hw, hd) + 1) / modelLong, sWide = (2 * Math.min(hw, hd)) / modelWide;
    const sy2 = (sLong + sWide) / 2;
    if (longX) obj.scale.set(sLong, sy2, sWide); else obj.scale.set(sWide, sy2, sLong);
    const deck = p.deck_final ?? p.deck ?? p.base;
    obj.position.set(x, deck - meta.extra.deckY * sy2, z);
    flattenDeck(obj, deck, targetLongZ ? 'z' : 'x', targetLongZ ? x : z);
    return;
  }
  obj.rotation.y = front;
  const turned = Math.abs(Math.sin(front)) > Math.SQRT1_2;
  const w = turned ? sz : sx, d = turned ? sx : sz;
  let s = fit === 'redonda' ? (2 * Math.min(hw, hd)) / Math.max(sx, sz) : Math.min((2 * hw) / w, (2 * hd) / d);
  if (fit === 'caixa') s = Math.min(s, (1.6 * (p.top - p.base)) / sy);
  obj.scale.setScalar(s);
  obj.position.set(x, fit === 'doca' ? r.deck - meta.extra.deckY * s : p.base + (p.lift || 0), z);
  if (r.tint) tintRoof(obj, r.tint);
}

// O tabuleiro de colisão das pontes é plano (e as rampas ficam fora dele); a ponte de pedra tem a
// pista abaulada, até 2,5 m mais baixa nas pontas do vão. Para o jogador não andar no ar, a pista e
// os parapeitos sobem até o tabuleiro; o que está mais de 2,5 m abaixo da pista (arco, pilares) fica
// onde está, e a faixa entre os dois se estica. A colisão não muda.
const _v = new THREE.Vector3(), _inv = new THREE.Matrix4();
function flattenDeck(obj, deck, along, cross0) {
  obj.updateMatrixWorld(true);
  const BAND = 2.5, STRIP = 1.2, BIN = 0.5;
  const meshes = [];
  obj.traverse((o) => { if (o.isMesh) meshes.push(o); });
  // perfil do topo da pista: maior altura na faixa central, a cada meio metro ao longo da ponte
  const prof = new Map();
  for (const o of meshes) {
    const p = o.geometry.attributes.position;
    for (let i = 0; i < p.count; i++) {
      _v.fromBufferAttribute(p, i).applyMatrix4(o.matrixWorld);
      const c = along === 'z' ? _v.x : _v.z, a = along === 'z' ? _v.z : _v.x;
      if (Math.abs(c - cross0) > STRIP) continue;
      const k = Math.round(a / BIN);
      if (!prof.has(k) || prof.get(k) < _v.y) prof.set(k, _v.y);
    }
  }
  const keys = [...prof.keys()].sort((p, q) => p - q);
  if (!keys.length) return;
  const top = (a) => {
    const k = Math.min(keys[keys.length - 1], Math.max(keys[0], Math.round(a / BIN)));
    for (let d = 0; d < keys.length; d++) { if (prof.has(k - d)) return prof.get(k - d); if (prof.has(k + d)) return prof.get(k + d); }
    return deck;
  };
  for (const o of meshes) {
    const g = o.geometry.clone(), p = g.attributes.position;
    const arr = new Float32Array(p.count * 3);
    _inv.copy(o.matrixWorld).invert();
    for (let i = 0; i < p.count; i++) {
      _v.fromBufferAttribute(p, i).applyMatrix4(o.matrixWorld);
      const road = top(along === 'z' ? _v.z : _v.x), lift = Math.max(0, deck - road);
      const w = Math.min(1, Math.max(0, (_v.y - (road - BAND)) / BAND));
      _v.y += lift * w;
      _v.applyMatrix4(_inv);
      arr[i * 3] = _v.x; arr[i * 3 + 1] = _v.y; arr[i * 3 + 2] = _v.z;
    }
    g.setAttribute('position', new THREE.BufferAttribute(arr, 3));
    g.computeBoundingSphere();
    o.geometry = g;
  }
}

// frente de uma construção: da caixa dos fundos para a caixa da fachada
function facadeYaw(nodes, volume) {
  const box = (n) => n && new THREE.Box3().setFromObject(n).getCenter(new THREE.Vector3());
  const f = box(nodes.get(`${volume}_Facade`)), r = box(nodes.get(`${volume}_Rear`));
  if (!f || !r || f.distanceTo(r) < 1e-3) return 0;
  const yaw = Math.atan2(f.x - r.x, f.z - r.z);
  return Math.round(yaw / (Math.PI / 2)) * (Math.PI / 2);   // alinhado à caixa
}

// assenta no ponto mais baixo do chão sob a pegada (nenhuma borda flutua)
function groundUnder(x, z, r) {
  let h = regionHeightAt(x, z);
  for (let a = 0; a < 8; a++) h = Math.min(h, regionHeightAt(x + Math.cos(a * Math.PI / 4) * r, z + Math.sin(a * Math.PI / 4) * r));
  return h;
}

let group = null;
function holder(scene) {
  if (!group) { group = new THREE.Group(); group.name = 'extra-props'; scene.add(group); }
  return group;
}

// Trocas visuais. Chamado por `loadRegionProps` depois de esconder os nós substituídos
// (`nodes`: nome → objeto, para achar a fachada de cada construção).
export async function loadExtraReplacements(scene, nodes) {
  const t0 = performance.now();
  const group = holder(scene);
  for (const r of REPLACEMENTS) {
    const p = PLACEMENTS.find((q) => q.name === r.volume);
    if (!p) continue;
    const obj = place(await template(r.asset));
    fitReplacement(obj, EXTRA_META[r.asset], p, r.fit, r.yaw ?? facadeYaw(nodes, r.volume), r);
    obj.name = `extra:${r.volume}`;
    group.add(obj);
    EXTRA_STATUS.replaced.push(r.volume);
  }
  for (const id of GAME_MODELS) await template(id);
  group.updateMatrixWorld(true);
  EXTRA_STATUS.ms += Math.round(performance.now() - t0);
}

// Modelo para um objeto que o jogo monta (barraca do Alto, torre da Passagem): cópia já na escala da
// pegada `w` × `d`, base no chão, frente para +Z. `null` se o modelo não carregou — o jogo fica com a
// peça procedural de antes.
export function extraModel(id, { w, d, fit = 'caixa', tint = null }) {
  const tpl = templates.get(id), meta = EXTRA_META[id];
  if (!tpl || !meta) return null;
  const obj = place(tpl);
  const [sx, , sz] = meta.size;
  obj.scale.setScalar(fit === 'redonda' ? Math.min(w, d) / Math.max(sx, sz) : Math.min(w / sx, d / sz));
  obj.position.y = -0.1;   // assenta a borda no chão em terreno pouco inclinado
  if (tint) tintRoof(obj, tint);
  const g = new THREE.Group();
  g.name = `extra:${id}`;
  g.add(obj);
  EXTRA_STATUS.game.push(id);
  return g;
}

// Acréscimos nos pontos de `extra-spots.js` (escolhidos com as áreas funcionais, antes da vegetação).
export async function loadExtraAdditions(scene) {
  const t0 = performance.now();
  const group = holder(scene);
  for (const s of EXTRA_SPOTS) {
    const tpl = await template(s.asset);
    const obj = place(tpl);
    if (s.asset === 'arvore_marco') {
      // dois níveis: o modelo inteiro perto, a cópia reduzida longe
      const lod = new THREE.LOD();
      const l0 = obj.getObjectByName('L0'), l1 = obj.getObjectByName('L1');
      if (l0 && l1) {
        l1.traverse((o) => { if (o.isMesh) o.castShadow = false; });
        lod.addLevel(l0, 0); lod.addLevel(l1, 70);
        obj.clear();
        obj.add(lod);
      }
    }
    obj.rotation.y = s.yaw;
    obj.position.set(s.x, groundUnder(s.x, s.z, s.col.r || 2) - 0.08, s.z);
    obj.name = `extra:${s.id}`;
    group.add(obj);
    EXTRA_STATUS.added.push(s.id);
  }
  group.updateMatrixWorld(true);
  EXTRA_STATUS.ms += Math.round(performance.now() - t0);
}
