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

export async function loadModels(progress) {
  await MeshoptDecoder.ready;
  progress && progress('personagens');
  const [
    kachujin, eve, paladina, walk, run, idle, guard, slash, thrust, punch, smash, bow, cast, spellArea, roll, down, roar, craft,
    lioness, horse
  ] = await Promise.all([
    parse(kachujinBin), parse(eveBin), parse(paladinaBin), parse(walkBin), parse(runBin), parse(idleBin), parse(guardBin),
    parse(slashBin), parse(thrustBin), parse(punchBin), parse(smashBin), parse(bowBin), parse(castBin),
    parse(spellAreaBin), parse(rollBin), parse(downBin), parse(roarBin), parse(craftBin),
    parse(lionessBin), parse(horseBin)
  ]);
  const getClip = (g, name) => (g && g.animations ? (g.animations.find((a) => a.name === name) || g.animations[0]) : null);
  const rawClips = {
    walk: getClip(walk, 'walk'),
    run: getClip(run, 'run'),
    idle: getClip(idle, 'idle'),
    guard: getClip(guard, 'guard'),
    slash: getClip(slash, 'slash'),
    thrust: getClip(thrust, 'thrust'),
    punch: getClip(punch, 'punch'),
    smash: getClip(smash, 'smash'),
    bow: getClip(bow, 'bow'),
    cast: getClip(cast, 'cast'),
    spell_area: getClip(spellArea, 'spell_area'),
    roll: getClip(roll, 'roll'),
    down: getClip(down, 'down'),
    roar: getClip(roar, 'roar'),
    craft: getClip(craft, 'craft'),
  };
  // clipes opcionais (pulo, agachar): a altura de referência é a do quadril em repouso do esqueleto
  // de origem, que o GLB da animação preserva
  const extra = {};
  for (const [key, bin] of Object.entries(EXTRA_CLIPS)) {
    const g = await parse(bin);
    const clip = getClip(g, key);
    if (!clip) continue;
    let hips = null;
    g.scene.traverse((o) => { if (!hips && /Hips$/.test(o.name)) hips = o; });
    extra[key] = { clip, refY: hips ? hips.position.y : null };
  }
  for (const [id, g] of [['kachujin', kachujin], ['eve', eve], ['paladina', paladina]]) {
    const M = mold(g, { height: 1.85, rough: 0.68 });
    M.walk = retarget(rawClips.walk, M.pivot);
    M.anims = {};
    for (const [k, clip] of Object.entries(rawClips)) {
      if (clip) M.anims[k] = retarget(clip, M.pivot);
    }
    for (const [k, { clip, refY }] of Object.entries(extra)) M.anims[k] = retarget(clip, M.pivot, refY);
    MODELS[id] = M;
  }
  progress && progress('criaturas');
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
