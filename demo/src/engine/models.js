// Modelos realistas (GLB embutidos pelo build, gerados por tools/build-assets.mjs a partir da pasta
// "modelos 3d animados"): personagens do Mixamo, caminhada do Mixamo, leoa e cavalo com animações.
// Cada modelo vira um "molde" normalizado (altura, pés no chão, frente para +Z) que é clonado por personagem.
import * as THREE from 'three';
import { GLTFLoader } from '../../vendor/three/addons/loaders/GLTFLoader.js';
import { MeshoptDecoder } from '../../vendor/three/addons/libs/meshopt_decoder.module.js';
import { clone as cloneSkinned } from '../../vendor/three/addons/utils/SkeletonUtils.js';
import { patchMaterial } from './materials.js';
import kachujinBin from '../../assets/char_kachujin.glb';
import eveBin from '../../assets/char_eve.glb';
import paladinaBin from '../../assets/char_paladina.glb';
import walkBin from '../../assets/anim_walk.glb';
import runBin from '../../assets/anim_run.glb';
import idleBin from '../../assets/anim_idle.glb';
import guardBin from '../../assets/anim_guard.glb';
import slashBin from '../../assets/anim_slash.glb';
import thrustBin from '../../assets/anim_thrust.glb';
import punchBin from '../../assets/anim_punch.glb';
import smashBin from '../../assets/anim_smash.glb';
import bowBin from '../../assets/anim_bow.glb';
import castBin from '../../assets/anim_cast.glb';
import spellAreaBin from '../../assets/anim_spell_area.glb';
import rollBin from '../../assets/anim_roll.glb';
import downBin from '../../assets/anim_down.glb';
import roarBin from '../../assets/anim_roar.glb';
import craftBin from '../../assets/anim_craft.glb';
import EXTRA_CLIPS from './extra-clips.js';
import COMBAT_CLIPS from './combat-clips.js';
import { EXTRA_BINS } from './extra-assets.js';
import lionessBin from '../../assets/beast_lioness.glb';
import horseBin from '../../assets/mount_horse.glb';

export const MODELS = {};

// Texturas embutidas decodificadas direto dos bytes. O GLTFLoader criaria uma URL blob: por imagem,
// e páginas com política de segurança restritiva (como a do Artifact) bloqueiam essas URLs: a textura
// não carrega e o personagem fica branco. createImageBitmap(Blob) não passa por URL nenhuma.
function directImages(parser) {
  parser.loadImageSource = function (sourceIndex) {
    if (this.sourceCache[sourceIndex] !== undefined) return this.sourceCache[sourceIndex].then((t) => t.clone());
    const def = this.json.images[sourceIndex];
    const p = this.getDependency('bufferView', def.bufferView)
      .then((view) => createImageBitmap(new Blob([view], { type: def.mimeType }), { imageOrientation: 'none', premultiplyAlpha: 'none', colorSpaceConversion: 'none' }))
      .then((bmp) => { const t = new THREE.Texture(bmp); t.needsUpdate = true; t.userData.mimeType = def.mimeType; return t; });
    this.sourceCache[sourceIndex] = p;
    return p;
  };
  return { name: 'direct_images' };
}

async function parse(bin) {
  const loader = new GLTFLoader().setMeshoptDecoder(MeshoptDecoder).register(directImages);
  const buf = bin.buffer.slice(bin.byteOffset, bin.byteOffset + bin.byteLength);
  return loader.parseAsync(buf, '');
}

// nome do osso sem o prefixo do Mixamo ("mixamorigLeftArm" → "LeftArm")
export const boneKey = (name) => name.replace(/^mixamorig\d*:?/, '');

// Molde: pivot (escala, frente, pés no chão) → cena do glTF. Materiais PBR com o recorte pontilhado.
function mold(gltf, { height, yaw = 0, rough = 0.7, env = 0.6 }) {
  const scene = gltf.scene;
  const pivot = new THREE.Group();
  pivot.add(scene);
  pivot.rotation.y = yaw;
  scene.updateMatrixWorld(true);
  const box = new THREE.Box3().setFromObject(scene, true);
  const s = height / (box.max.y - box.min.y);
  pivot.scale.setScalar(s);
  pivot.position.y = -box.min.y * s;
  const meshes = [];
  scene.traverse((o) => {
    if (!o.isMesh) return;
    o.frustumCulled = true;
    o.castShadow = true; o.receiveShadow = true;
    const m = o.material;
    m.roughness = rough; m.metalness = 0; m.envMapIntensity = env;
    if (m.map) m.map.anisotropy = 4;
    if (!m.userData.patched) { patchMaterial(m, ['occ']); m.userData.patched = true; }
    meshes.push(o);
  });
  // esfera de recorte generosa (a pose animada sai da pose de repouso), calculada uma vez por molde
  const sphere = box.getBoundingSphere(new THREE.Sphere());
  pivot.updateMatrixWorld(true);
  for (const o of meshes) {
    const inv = new THREE.Matrix4().copy(o.matrixWorld).invert();
    const sp = sphere.clone().applyMatrix4(pivot.matrixWorld).applyMatrix4(inv);
    sp.radius *= 1.35;
    o.userData.sphere = sp;
  }
  return { pivot, scale: s, clips: gltf.animations, height };
}

// Clona o molde (esqueleto próprio, geometria e texturas compartilhadas)
export function instantiate(M) {
  const pivot = cloneSkinned(M.pivot);
  pivot.traverse((o) => {
    // clone() copia userData via JSON: a esfera chega como {center:{x,y,z}, radius}
    const sp = o.userData.sphere;
    if (o.isSkinnedMesh && sp) o.boundingSphere = new THREE.Sphere(new THREE.Vector3().copy(sp.center), sp.radius);
  });
  return pivot;
}

// ---------- registro de clipes humanos ----------
// Uma entrada por clipe, com o mesmo formato para a biblioteca, os opcionais (`extra-clips.js`) e o
// lote de combate (`combat-clips.js`, gerado de `tools/combat-manifest.mjs`):
//   { key, bin, refY, contactMode, locomotionMode, motion, group }
// `refY` sai sempre de `hipsRef()` — a altura de repouso do quadril no esqueleto do GLB da animação.
//   contactMode     'feet' | 'body' | 'none': como `characters.js` assenta o clipe no chão
//   locomotionMode  'cycle' | 'pose' | 'oneshot'
// A biblioteca antiga (anterior aos opcionais) foi ajustada à mão com a altura do quadril no primeiro
// quadro de cada clipe; ela guarda essa referência (`refFrom: 'firstFrame'`) para as poses que já
// estão no jogo não mudarem de altura. Opcionais e combate usam o repouso do esqueleto de origem.
const LIBRARY = [
  ['walk', walkBin, 'none', 'cycle'], ['run', runBin, 'none', 'cycle'], ['idle', idleBin, 'none', 'pose'],
  ['guard', guardBin, 'feet', 'pose'], ['slash', slashBin, 'none', 'oneshot'], ['thrust', thrustBin, 'none', 'oneshot'],
  ['punch', punchBin, 'none', 'oneshot'], ['smash', smashBin, 'none', 'oneshot'], ['bow', bowBin, 'none', 'oneshot'],
  ['cast', castBin, 'none', 'oneshot'], ['spell_area', spellAreaBin, 'none', 'oneshot'], ['roll', rollBin, 'none', 'oneshot'],
  ['down', downBin, 'none', 'oneshot'], ['roar', roarBin, 'none', 'oneshot'], ['craft', craftBin, 'none', 'pose'],
].map(([key, bin, contactMode, locomotionMode]) => ({ key, bin, contactMode, locomotionMode, motion: null, group: 'library', refFrom: 'firstFrame' }));
const EXTRA_MODES = {
  jump: ['feet', 'oneshot'], jumpBoost: ['feet', 'oneshot'], crouchIdle: ['feet', 'pose'], crouchWalk: ['feet', 'cycle'], bowAim: ['none', 'oneshot'],
};
const EXTRA = Object.entries(EXTRA_CLIPS).map(([key, bin]) => {
  const [contactMode, locomotionMode] = EXTRA_MODES[key] || ['none', 'oneshot'];
  return { key, bin, contactMode, locomotionMode, motion: null, group: 'extra', refFrom: 'rest' };
});
const COMBAT = COMBAT_CLIPS.map((e) => ({ ...e, group: 'combat', refFrom: 'rest' }));
export const CLIP_REGISTRY = [...LIBRARY, ...EXTRA, ...COMBAT];

// Quadril do esqueleto de origem: altura de repouso e altura no primeiro quadro do clipe.
export function hipsRef(gltf, clip) {
  let hips = null;
  gltf.scene.traverse((o) => { if (!hips && /Hips$/.test(o.name)) hips = o; });
  const rest = hips ? hips.position.y : null;
  const track = hips && clip ? clip.tracks.find((t) => t.name === hips.name + '.position') : null;
  return { rest, first: track ? track.values[1] : rest };
}

// Clipe da caminhada adaptado a um esqueleto: a translação do quadril vem do boneco de origem do
// Mixamo e é reescalada pela altura do quadril do alvo (senão as pernas flutuam ou afundam).
// `refY`: altura do quadril em pé no boneco de origem. Sem ela vale o primeiro quadro do clipe — o
// que só serve para clipes que começam em pé (agachar começa com o quadril baixo).
function retarget(clip, pivot, refY = null) {
  let hips = null;
  pivot.traverse((o) => { if (!hips && o.isBone && boneKey(o.name) === 'Hips') hips = o; });
  const c = clip.clone();
  const names = new Set(); pivot.traverse((o) => names.add(o.name));
  c.tracks = c.tracks.filter((t) => names.has(t.name.split('.')[0]));
  for (const t of c.tracks) {
    if (!t.name.endsWith('.position') || !hips) continue;
    const k = hips.position.y / (refY || t.values[1]);
    for (let i = 0; i < t.values.length; i++) t.values[i] *= k;
  }
  return c;
}

// frente do quadrúpede: da pelve para a cabeça, no plano XZ
function forwardYaw(gltf, headRe, tailRe) {
  const scene = gltf.scene; scene.updateMatrixWorld(true);
  let head = null, tail = null;
  scene.traverse((o) => { if (!head && headRe.test(o.name)) head = o; if (!tail && tailRe.test(o.name)) tail = o; });
  if (!head || !tail) return 0;
  const a = new THREE.Vector3().setFromMatrixPosition(tail.matrixWorld), b = new THREE.Vector3().setFromMatrixPosition(head.matrixWorld);
  return -Math.atan2(b.x - a.x, b.z - a.z);   // gira o modelo para a cabeça apontar para +Z
}

// Resolve o registro: lê cada GLB, acha o clipe pela chave e grava `refY`. Uma entrada sem clipe
// (registro parcial) simplesmente não entra: o animador usa o substituto daquela ação.
export async function resolveClips(registry = CLIP_REGISTRY) {
  const getClip = (g, name) => (g && g.animations ? (g.animations.find((a) => a.name === name) || g.animations[0]) : null);
  const parsed = await Promise.all(registry.map((e) => parse(e.bin)));
  const out = [];
  registry.forEach((e, i) => {
    const clip = getClip(parsed[i], e.key);
    if (!clip) return;
    const ref = hipsRef(parsed[i], clip);
    out.push({ ...e, bin: undefined, clip, refY: e.refFrom === 'firstFrame' ? ref.first : ref.rest, restY: ref.rest });
  });
  return out;
}

export async function loadModels(progress) {
  await MeshoptDecoder.ready;
  progress && progress('personagens');
  const [kachujin, eve, paladina, lioness, horse, clips] = await Promise.all([
    parse(kachujinBin), parse(eveBin), parse(paladinaBin), parse(lionessBin), parse(horseBin), resolveClips(),
  ]);
  // metadados por chave (sem o clipe cru), lidos pelo animador
  MODELS.clipMeta = Object.fromEntries(clips.map(({ clip, ...meta }) => [meta.key, meta]));
  const walk = clips.find((c) => c.key === 'walk');
  for (const [id, g] of [['kachujin', kachujin], ['eve', eve], ['paladina', paladina]]) {
    const M = mold(g, { height: 1.85, rough: 0.68 });
    M.walk = retarget(walk.clip, M.pivot, walk.refY);
    M.anims = {};
    for (const c of clips) M.anims[c.key] = retarget(c.clip, M.pivot, c.refY);
    MODELS[id] = M;
  }
  progress && progress('criaturas');
  // corpo das garras (void_hound): molde estático, sem esqueleto — flutua por animação procedural.
  // Sem ele, as garras continuam com a leoa.
  if (EXTRA_BINS.cao_vazio) MODELS.hound = mold(await parse(EXTRA_BINS.cao_vazio), { height: 1.3, rough: 0.8, env: 0.5 });
  // armas do pacote PurePoly: uma cena por modelo, com uma parte por zona (lâmina, guarda, cabo…)
  MODELS.weapons = {};
  for (const id of Object.keys(EXTRA_BINS).filter((k) => k.startsWith('arma_'))) MODELS.weapons[id] = (await parse(EXTRA_BINS[id])).scene;
  const lyaw = forwardYaw(lioness, /^Head$/i, /^(Pelvis|Tail)/i);
  const L = mold(lioness, { height: 1.25, yaw: lyaw, rough: 0.85, env: 0.5 });
  L.pivot.updateMatrixWorld(true);
  const lc = new THREE.Box3().setFromObject(L.pivot, true).getCenter(new THREE.Vector3());
  L.pivot.position.x -= lc.x; L.pivot.position.z -= lc.z;
  L.idle = lioness.animations.find((a) => a.name === 'idle'); L.walkClip = lioness.animations.find((a) => a.name === 'walk');
  MODELS.lioness = L;
  const hyaw = forwardYaw(horse, /^head_/i, /^pelvis/i);
  const H = mold(horse, { height: 2.05, yaw: hyaw, rough: 0.7, env: 0.55 });
  H.idle = horse.animations.find((a) => a.name === 'idle'); H.walkClip = horse.animations.find((a) => a.name === 'walk');
  // o cavalo é centrado na sela (a posição da montaria é a do cavaleiro); o topo dela é o assento
  let saddle = null; H.pivot.traverse((o) => { if (!saddle && o.isMesh && /Saddle1/.test(o.material.name)) saddle = o; });
  H.pivot.updateMatrixWorld(true);
  const sb = saddle ? new THREE.Box3().setFromObject(saddle, true) : new THREE.Box3().setFromObject(H.pivot, true);
  const c = sb.getCenter(new THREE.Vector3());
  H.pivot.position.x -= c.x; H.pivot.position.z -= c.z;
  H.seatY = sb.max.y - 0.1; H.seatZ = 0;
  MODELS.horse = H;
  return MODELS;
}
