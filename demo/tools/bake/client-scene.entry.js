// Entrada empacotada por `bake-client-scene.mjs` e executada no Chromium (WebGL de verdade). Roda a
// mesma montagem de mundo de main.js (texturas → modelos → mundo → camada de jogo → Turbulenta) e
// exporta a cena pronta para o cliente em C++: geometrias, materiais (com a família de recursos de
// `patchMaterial`), texturas, malhas estáticas, instâncias, campos de LOD e os modelos que mudam em
// jogo (pontos de coleta, santuários, portal, sacos de carga, armas).
//
// Convenção das texturas: as linhas saem na ordem de envio do WebGL (a primeira linha é v = 0). Dados
// crus vão como RGBA8; imagens embutidas vão com os bytes originais e `flipY`, para o C++ inverter
// depois de decodificar quando o three inverteria no envio.
import * as THREE from 'three';
import { G } from '../../src/state.js';
import { WEAPONS } from '../../src/config.js';
import { generateTextures } from '../../src/engine/textures.js';
import { loadModels } from '../../src/engine/models.js';
import { weaponMesh, makeHumanoid, animateHumanoid, makeBeast, animateBeast, makeMount, animateMount } from '../../src/engine/characters.js';
import { groundHeight } from '../../src/engine/terrain.js';
import { MODELS, instantiate } from '../../src/engine/models.js';
import { portalMesh, mat } from '../../src/engine/props.js';
import { LOD_FIELDS, LOD_BANDS } from '../../src/engine/lod-field.js';
import { loadWorldScene } from '../../src/world/index.js';
import { WORLD_SCENE, updateWorldDetail, setTurbulentFx } from '../../src/world/scene-world.js';
import { buildWorld, NODES } from '../../src/game/world.js';
import { initTurbulent, TS } from '../../src/game/turbulent.js';
import { CAMP } from '../../src/game/camp.js';
import { createLootBag } from '../../src/game/zones.js';
import { TURB_GATE_X } from '../../src/world/coordinates.js';
import { generateCell, CELL as COVER_CELL, COVER } from '../../src/world/ground-cover.js';
import { levelParts } from '../../src/engine/props.js';

// ---------------------------------------------------------------- aleatoriedade fixa (bake reprodutível)
function mulberry(seed) {
  let a = seed >>> 0;
  return () => {
    a = (a + 0x6d2b79f5) >>> 0;
    let t = a;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

// ---------------------------------------------------------------- binário
class Bin {
  constructor() { this.chunks = []; this.size = 0; }
  // devolve o deslocamento (alinhado a 16 bytes)
  push(typed) {
    const pad = (16 - (this.size % 16)) % 16;
    if (pad) { this.chunks.push(new Uint8Array(pad)); this.size += pad; }
    const off = this.size;
    const bytes = new Uint8Array(typed.buffer, typed.byteOffset, typed.byteLength);
    this.chunks.push(bytes.slice());
    this.size += bytes.byteLength;
    return off;
  }
}

function toFloat32(attr) {
  const n = attr.count, k = attr.itemSize, out = new Float32Array(n * k);
  const get = [attr.getX, attr.getY, attr.getZ, attr.getW];
  for (let i = 0; i < n; i++) for (let c = 0; c < k; c++) out[i * k + c] = get[c].call(attr, i);
  return out;
}

const round = (v) => Math.round(v * 1e6) / 1e6;
const col = (c) => [round(c.r), round(c.g), round(c.b)];

// ---------------------------------------------------------------- exportador
class Exporter {
  constructor(renderer) {
    this.renderer = renderer;
    this.bin = new Bin();
    this.geometries = []; this.geoIds = new Map();
    this.materials = []; this.matIds = new Map();
    this.textures = []; this.texIds = new Map();
    this.sources = []; this.srcIds = new Map();
  }

  geometry(g) {
    if (this.geoIds.has(g.uuid)) return this.geoIds.get(g.uuid);
    const attributes = {};
    for (const [name, a] of Object.entries(g.attributes)) {
      if (!['position', 'normal', 'uv', 'color', 'splatA', 'splatB', 'splatC', 'aDepth', 'skinIndex', 'skinWeight'].includes(name)) continue;
      const raw = a.isInterleavedBufferAttribute ? null : a.array;
      if (name === 'skinIndex') {
        attributes[name] = { type: 'u16', size: a.itemSize, offset: this.bin.push(Uint16Array.from(toFloat32(a))) };
      } else if (raw instanceof Uint8Array && a.normalized) {
        attributes[name] = { type: 'u8n', size: a.itemSize, offset: this.bin.push(raw) };
      } else {
        attributes[name] = { type: 'f32', size: a.itemSize, offset: this.bin.push(raw instanceof Float32Array && !a.normalized ? raw : toFloat32(a)) };
      }
    }
    const count = g.attributes.position.count;
    let index = null;
    if (g.index) {
      const idx = g.index.array instanceof Uint32Array ? g.index.array : Uint32Array.from(g.index.array);
      index = { offset: this.bin.push(idx), count: idx.length };
    }
    if (!g.boundingBox) g.computeBoundingBox();
    const b = g.boundingBox;
    const groups = g.groups.length ? g.groups.map((x) => ({ start: x.start, count: x.count, material: x.materialIndex })) : null;
    const id = this.geometries.length;
    this.geometries.push({ count, attributes, index, groups, bounds: [b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z].map(round) });
    this.geoIds.set(g.uuid, id);
    return id;
  }

  async source(tex) {
    const src = tex.source;
    if (this.srcIds.has(src.uuid)) return this.srcIds.get(src.uuid);
    const img = src.data;
    let rec;
    if (tex.isDataArrayTexture) {
      const layers = [];
      const per = img.width * img.height * 4;
      for (let l = 0; l < img.depth; l++) layers.push(this.bin.push(img.data.subarray(l * per, (l + 1) * per)));
      rec = { kind: 'raw', w: img.width, h: img.height, layers };
    } else if (tex.isDataTexture) {
      rec = { kind: 'raw', w: img.width, h: img.height, layers: [this.bin.push(img.data)] };
    } else if (tex.isRenderTargetTexture && tex.renderTarget) {
      const rt = tex.renderTarget;
      const buf = new Uint8Array(rt.width * rt.height * 4);
      this.renderer.readRenderTargetPixels(rt, 0, 0, rt.width, rt.height, buf);
      rec = { kind: 'raw', w: rt.width, h: rt.height, layers: [this.bin.push(buf)] };  // readPixels já está na ordem do GL
    } else if (src.__bytes) {
      rec = { kind: 'encoded', mime: src.__mime || 'image/webp', w: img.width, h: img.height, bytes: this.bin.push(src.__bytes), length: src.__bytes.byteLength };
    } else if (img && typeof img.src === 'string' && img.src.startsWith('data:')) {
      const comma = img.src.indexOf(',');
      const mime = img.src.slice(5, img.src.indexOf(';'));
      const bytes = Uint8Array.from(atob(img.src.slice(comma + 1)), (c) => c.charCodeAt(0));
      rec = { kind: 'encoded', mime, w: img.width, h: img.height, bytes: this.bin.push(bytes), length: bytes.byteLength };
    } else if (img && img.width) {
      // canvas (texturas procedurais) ou imagem sem bytes conhecidos: os pixels como o WebGL os recebe
      const c = document.createElement('canvas');
      c.width = img.width; c.height = img.height;
      const ctx = c.getContext('2d', { willReadFrequently: true });
      if (tex.flipY) { ctx.translate(0, c.height); ctx.scale(1, -1); }
      ctx.drawImage(img, 0, 0);
      rec = { kind: 'raw', w: c.width, h: c.height, layers: [this.bin.push(ctx.getImageData(0, 0, c.width, c.height).data)] };
    } else {
      throw new Error(`textura sem imagem: ${tex.name || tex.uuid}`);
    }
    const id = this.sources.length;
    this.sources.push(rec);
    this.srcIds.set(src.uuid, id);
    return id;
  }

  async texture(tex) {
    if (!tex) return -1;
    if (this.texIds.has(tex.uuid)) return this.texIds.get(tex.uuid);
    const source = await this.source(tex);
    tex.updateMatrix();
    const mips = (tex.mipmaps || []).filter((m) => m && m.data).slice(1).map((m) => ({ w: m.width, h: m.height, offset: this.bin.push(m.data) }));
    const rec = {
      source,
      // dados crus e alvos de render já estão na ordem de envio; imagens codificadas levam o flipY
      flipY: this.sources[source].kind === 'encoded' ? !!tex.flipY : false,
      srgb: tex.colorSpace === THREE.SRGBColorSpace,
      array: !!tex.isDataArrayTexture,
      wrapS: tex.wrapS === THREE.RepeatWrapping ? 'repeat' : tex.wrapS === THREE.MirroredRepeatWrapping ? 'mirror' : 'clamp',
      wrapT: tex.wrapT === THREE.RepeatWrapping ? 'repeat' : tex.wrapT === THREE.MirroredRepeatWrapping ? 'mirror' : 'clamp',
      mipmaps: tex.generateMipmaps || mips.length > 0,
      mipLevels: mips,
      nearest: tex.magFilter === THREE.NearestFilter,
      anisotropy: tex.anisotropy || 1,
      uvMatrix: tex.matrix.elements.map(round),
    };
    const id = this.textures.length;
    this.textures.push(rec);
    this.texIds.set(tex.uuid, id);
    return id;
  }

  async uniformValue(v) {
    if (v && v.value !== undefined) v = v.value;
    if (v == null) return null;
    if (typeof v === 'number' || typeof v === 'boolean') return v;
    if (Array.isArray(v)) return v.map((x) => (typeof x === 'number' ? round(x) : x));
    if (v.isColor) return col(v);
    if (v.isVector2) return [round(v.x), round(v.y)];
    if (v.isVector3) return [round(v.x), round(v.y), round(v.z)];
    if (v.isTexture) return { texture: await this.texture(v) };
    return null;
  }

  async material(m) {
    if (this.matIds.has(m.uuid)) return this.matIds.get(m.uuid);
    let feats = m.userData.__feats ? [...m.userData.__feats] : [];
    const key = m.customProgramCacheKey && m.customProgramCacheKey !== THREE.Material.prototype.customProgramCacheKey ? m.customProgramCacheKey() : '';
    if (key === 'world-anomaly') feats.push('anomaly');
    const uniforms = {};
    for (const [k, v] of Object.entries(m.userData.__uniforms || {})) {
      const u = await this.uniformValue(v);
      if (u !== null) uniforms[k] = u;
    }
    const rec = {
      name: m.name || '',
      type: m.type,                                   // MeshStandardMaterial, MeshLambertMaterial, MeshBasicMaterial…
      feats,
      uniforms,
      color: m.color ? col(m.color) : [1, 1, 1],
      emissive: m.emissive ? col(m.emissive) : [0, 0, 0],
      emissiveIntensity: round(m.emissiveIntensity ?? 1),
      roughness: round(m.roughness ?? 1),
      metalness: round(m.metalness ?? 0),
      opacity: round(m.opacity ?? 1),
      transparent: !!m.transparent,
      alphaTest: round(m.alphaTest || 0),
      side: m.side,                                   // 0 frente, 1 trás, 2 os dois
      vertexColors: !!m.vertexColors,
      flatShading: !!m.flatShading,
      depthWrite: m.depthWrite !== false,
      depthTest: m.depthTest !== false,
      additive: m.blending === THREE.AdditiveBlending,
      polygonOffset: m.polygonOffset ? [m.polygonOffsetFactor, m.polygonOffsetUnits] : null,
      envMapIntensity: round(m.envMapIntensity ?? 1),
      normalScale: m.normalScale ? [round(m.normalScale.x), round(m.normalScale.y)] : [1, 1],
      map: await this.texture(m.map),
      normalMap: await this.texture(m.normalMap),
      roughnessMap: await this.texture(m.roughnessMap),
      metalnessMap: await this.texture(m.metalnessMap),
      emissiveMap: await this.texture(m.emissiveMap),
      aoMap: await this.texture(m.aoMap),
      alphaMap: await this.texture(m.alphaMap),
    };
    const id = this.materials.length;
    this.materials.push(rec);
    this.matIds.set(m.uuid, id);
    return id;
  }

  // Uma malha comum como lista de desenhos (uma entrada por grupo de material).
  async draws(mesh, matrix) {
    const geometry = this.geometry(mesh.geometry);
    const mats = Array.isArray(mesh.material) ? mesh.material : [mesh.material];
    const out = [];
    const groups = this.geometries[geometry].groups;
    if (groups && Array.isArray(mesh.material)) {
      for (const g of groups) {
        if (!mats[g.material]) continue;
        out.push({ geometry, material: await this.material(mats[g.material]), start: g.start, count: g.count, matrix, cast: !!mesh.castShadow });
      }
    } else {
      out.push({ geometry, material: await this.material(mats[0]), matrix, cast: !!mesh.castShadow });
    }
    return out;
  }
}

const mapOf = (x) => (x > TURB_GATE_X ? 1 : 0);
const mat16 = (m) => m.elements.map(round);

// Subárvore como modelo: peças com matriz relativa à raiz (nomeadas quando a demo usa o nome).
async function template(ex, root, { skipHidden = true } = {}) {
  root.updateMatrixWorld(true);
  const inv = root.matrixWorld.clone().invert();
  const parts = [];
  const visit = async (o) => {
    if (skipHidden && !o.visible && o !== root) return;
    if (o.isMesh && !o.isInstancedMesh && !o.isSkinnedMesh) {
      const local = inv.clone().multiply(o.matrixWorld);
      for (const d of await ex.draws(o, mat16(local))) parts.push({ ...d, name: o.name || '' });
    }
    for (const c of o.children) await visit(c);
  };
  await visit(root);
  return parts;
}

function hasSkin(o) {
  let s = false;
  o.traverse((c) => { if (c.isSkinnedMesh) s = true; });
  return s;
}

// Esqueleto de um molde: nós da subárvore do pivot (ordem de traverse) com a pose de repouso (a que o
// mixer do three guarda como estado original), malhas (com pele: bindMatrix, ossos e inversas) e os
// clipes já adaptados ao modelo (retarget + contato), cada trilha apontando para o índice do nó.
async function exportRig(ex, pivot, clips, extra = {}) {
  const nodes = [], idx = new Map(), byName = new Map();
  pivot.traverse((o) => { idx.set(o, nodes.length); if (!byName.has(o.name)) byName.set(o.name, nodes.length); nodes.push(o); });
  const rest = new Float32Array(nodes.length * 10);
  nodes.forEach((o, i) => { o.position.toArray(rest, i * 10); o.quaternion.toArray(rest, i * 10 + 3); o.scale.toArray(rest, i * 10 + 7); });
  const meshes = [];
  for (const o of nodes) {
    if (!o.isMesh) continue;
    const mats = Array.isArray(o.material) ? o.material : [o.material];
    const rec = { node: idx.get(o), geometry: ex.geometry(o.geometry), material: await ex.material(mats[0]), skinned: !!o.isSkinnedMesh };
    if (o.isSkinnedMesh) {
      rec.bindMatrix = ex.bin.push(new Float32Array(o.bindMatrix.elements));
      rec.bones = o.skeleton.bones.map((b) => idx.get(b));
      const inv = new Float32Array(o.skeleton.boneInverses.length * 16);
      o.skeleton.boneInverses.forEach((m, i) => inv.set(m.elements, i * 16));
      rec.boneInverses = ex.bin.push(inv);
    }
    meshes.push(rec);
  }
  const outClips = {};
  for (const [key, clip] of Object.entries(clips)) {
    if (!clip) continue;
    outClips[key] = {
      duration: clip.duration,
      tracks: clip.tracks.map((t) => {
        const dot = t.name.lastIndexOf('.');
        return {
          node: byName.has(t.name.slice(0, dot)) ? byName.get(t.name.slice(0, dot)) : -1, path: t.name.slice(dot + 1),
          count: t.times.length, size: t.getValueSize(),
          interp: t.getInterpolation() === THREE.InterpolateDiscrete ? 'step' : 'linear',
          times: ex.bin.push(Float32Array.from(t.times)), values: ex.bin.push(Float32Array.from(t.values)),
        };
      }),
    };
  }
  return {
    nodes: nodes.map((o) => ({ name: o.name, parent: o.parent && idx.has(o.parent) && o !== pivot ? idx.get(o.parent) : -1, bone: !!o.isBone })),
    rest: ex.bin.push(rest), meshes, clips: outClips, ...extra,
  };
}

async function exportRigs(ex) {
  const rigs = {};
  const q = (v) => v.toArray().map(round);
  for (const id of ['kachujin', 'eve', 'paladina']) {
    const M = MODELS[id];
    if (!M) continue;
    // makeHumanoid prepara o molde (pose ociosa, contato, ciclos) e calibra as empunhaduras
    const h = makeHumanoid({ model: id });
    const fresh = instantiate(M);
    const names = new Map();
    fresh.traverse((o) => { if (!names.has(o.name)) names.set(o.name, o); });
    const grip = (g) => ({ bone: g.parent.name, t: q(g.position), r: q(g.quaternion), s: q(g.scale) });
    rigs[id] = await exportRig(ex, fresh, M.anims, {
      kind: 'humanoid', clipOrder: Object.keys(M.anims).filter((k) => M.anims[k]),
      hipsY: M.hipsY, hipsBase: M.hipsBase, height: M.height,
      idleRef: Object.fromEntries(Object.entries(M.idleRef || {}).map(([k, v]) => [k, q(v)])),
      cycle: M.cycle || {}, contact: M.contact || {},
      grips: { R: grip(h.gripR), L: grip(h.gripL) },
    });
    // a pele do molde já serve a qualquer clone: o nome dos ossos é o mesmo
    void names;
  }
  const quad = async (M, kind, extra = {}) => exportRig(ex, instantiate(M), { idle: M.idle, walk: M.walkClip }, { kind, ...extra });
  if (MODELS.lioness) rigs.lioness = await quad(MODELS.lioness, 'quadruped');
  if (MODELS.horse) rigs.horse = await quad(MODELS.horse, 'quadruped', { seatY: MODELS.horse.seatY, seatZ: MODELS.horse.seatZ });
  if (MODELS.hound) rigs.hound = await exportRig(ex, instantiate(MODELS.hound), {}, { kind: 'hound' });
  return rigs;
}

// ---------------------------------------------------------------- paridade da animação
// Roteiros de entrada (o `s` que player.js, enemies.js, npcs.js e mount.js entregam) rodados nos
// próprios animadores da demo, com Math.random fixo em 0,5 e sem câmera (todo quadro anima). Grava a
// pose de cada nó no fim de cada roteiro e, num dos humanoides, posições de vértices com pele, para o
// C++ (tests/client/AnimTests.cpp) refazer o mesmo caminho e comparar.
const DT = 1 / 30;
const ramp = (n, f) => Array.from({ length: n }, (_, i) => f(i, n));
function humanScenarios() {
  const walk = (mps, n, extra = {}) => ramp(n, () => ({ mps, fwd: 1, strafe: 0, ...extra }));
  return [
    { name: 'idle_guard', family: 'espada', frames: ramp(45, () => ({ mps: 0 })) },
    { name: 'idle_fists', family: null, frames: ramp(80, () => ({ mps: 0 })) },
    { name: 'walk_run', family: 'espada', frames: [...walk(1.8, 30), ...walk(5.6, 30)], yaw: (i) => 0.6 + i * 0.01 },
    { name: 'strafe_back', family: 'arco', frames: [...ramp(20, () => ({ mps: 2.2, fwd: 0.2, strafe: 0.9 })), ...ramp(20, () => ({ mps: 2.0, fwd: -0.9, strafe: -0.3 }))] },
    { name: 'slash', family: 'espada', frames: ramp(20, (i, n) => ({ mps: 0, attack: i / (n - 1), kind: 'slash', aimPitch: 0.2 })), checkpoints: [6, 12] },
    { name: 'swing_walk', family: 'martelo', frames: [...walk(2, 10), ...ramp(16, (i, n) => ({ mps: 2, fwd: 1, attack: i / (n - 1), kind: 'swing' }))] },
    { name: 'heavy_cast', family: 'tomo_fogo', frames: [...ramp(12, (i, n) => ({ mps: 0, attack: i / (n - 1), kind: 'heavy' })), ...ramp(12, (i, n) => ({ mps: 0, attack: i / (n - 1), kind: 'cast' }))], checkpoints: [5, 17] },
    { name: 'bow_shot', family: 'arco', frames: ramp(18, (i, n) => ({ mps: 0, attack: i / (n - 1), kind: 'bow' })), checkpoints: [5, 10] },
    { name: 'dodge_left', family: 'arco', frames: [...ramp(13, (i) => ({ mps: 0, dodge: Math.min(1, (i * DT) / 0.42), dodgeKey: 'dodgeLeft' })), ...ramp(8, () => ({ mps: 0 }))], checkpoints: [7, 15] },
    { name: 'roll', family: 'espada', frames: [...walk(3, 6), ...ramp(13, (i) => ({ mps: 0, dodge: Math.min(1, (i * DT) / 0.42), dodgeKey: 'roll' }))] },
    { name: 'hit_walk', family: 'espada', frames: [...walk(2, 12), ...ramp(14, (i) => ({ mps: 2, fwd: 1, hit: { side: 'left', t: i * DT } }))] },
    { name: 'hit_idle', family: 'espada', frames: ramp(14, (i) => ({ mps: 0, hit: { side: 'back', t: i * DT } })) },
    { name: 'death_forward', family: 'espada', frames: ramp(40, (i) => ({ mps: 0, death: { fall: 'forward', t: i * DT } })) },
    { name: 'down', family: 'espada', frames: ramp(30, () => ({ mps: 0, down: true })) },
    { name: 'crouch_jump', family: 'espada', frames: [...ramp(20, () => ({ mps: 0, crouch: 1 })), ...ramp(10, () => ({ mps: 1.2, fwd: 1, crouch: 1 })), ...ramp(16, (i, n) => ({ mps: 3, fwd: 1, air: { phase: i / (n - 1), boosted: i > 8 } }))] },
    { name: 'mounted', family: 'lanca', frames: ramp(20, () => ({ mps: 0, mounted: true })) },
    { name: 'channel_meta', family: null, frames: [...ramp(15, () => ({ mps: 0, channel: true })), ...ramp(15, () => ({ mps: 0, metamorph: true }))] },
    { name: 'craft', family: 'martelo', frames: ramp(20, () => ({ mps: 0, craft: true })) },
    ...aimScenarios(),
    ...actionScenarios(),
  ];
}
// A mira: o aim que player.js entrega é P.aimT, que updateCamera leva a 1 com min(1, dt·9) por quadro
// enquanto se mira (1 − 0,7ᵏ a 30 quadros por segundo) e de volta a 0 ao soltar.
const aimUp = (k) => 1 - Math.pow(1 - Math.min(1, DT * 9), k + 1);
const aimDown = (k) => Math.pow(1 - Math.min(1, DT * 9), k + 1);
function aimScenarios() {
  const hold = (n, extra = {}) => ramp(n, () => ({ mps: 0, aim: 1, ...extra }));
  const up = (n, extra = {}) => ramp(n, (i) => ({ mps: 0, aim: aimUp(i), ...extra }));
  const shot = (kind, n, extra = {}) => ramp(n, (i, m) => ({ mps: 0, aim: 1, attack: 0.9 * i / (m - 1), kind, ...extra }));
  return [
    // arco: erguer e puxar (full), segurar (hold), soltar e voltar a puxar, e baixar
    { name: 'bow_aim_hold', family: 'arco', frames: [...up(12), ...hold(30), ...ramp(10, (i) => ({ mps: 0, aim: aimDown(i) }))], checkpoints: [5, 11, 25, 41, 46] },
    { name: 'bow_aim_fire', family: 'arco', frames: [...up(9), ...hold(12, { aimPitch: 0.15 }), ...shot('bow', 12, { aimPitch: 0.15 }), ...hold(10, { aimPitch: 0.15 })], checkpoints: [20, 24, 28, 36, 42] },
    { name: 'bow_aim_charge', family: 'arco', frames: [...up(9, { aimPitch: 0.3 }), ...hold(6, { aimPitch: 0.3 }), ...shot('charge', 14, { aimPitch: 0.3 }), ...hold(6, { aimPitch: -0.25 })], checkpoints: [14, 20, 28, 34] },
    { name: 'bow_aim_walk', family: 'arco', frames: [...up(8, { mps: 1.4, fwd: 1 }), ...ramp(20, () => ({ mps: 1.4, fwd: 1, aim: 1 })), ...ramp(15, () => ({ mps: 1.6, fwd: 0.3, strafe: 0.8, aim: 1 }))], checkpoints: [7, 20, 27, 42] },
    { name: 'bow_aim_hit', family: 'arco', frames: [...up(6), ...hold(8), ...ramp(8, (i) => ({ mps: 0, aim: 1, hit: { side: 'front', t: i * DT } }))], checkpoints: [13, 17, 21] },
    { name: 'bow_aim_air', family: 'arco', frames: [...hold(6), ...ramp(12, (i, n) => ({ mps: 2, fwd: 1, aim: 1, air: { phase: i / (n - 1), boosted: false } }))], checkpoints: [5, 11, 17] },
    { name: 'bow_aim_mounted', family: 'arco', frames: hold(16, { mounted: true }), checkpoints: [8, 15] },
    // mira das outras armas (ombros de lado, arma na linha do olhar, cabeça para o alvo)
    { name: 'aim_sword', family: 'espada', frames: [...up(10), ...hold(10, { aimPitch: -0.3 })], checkpoints: [5, 19] },
    { name: 'aim_crossbow', family: 'besta', frames: [...up(10), ...hold(8), ...shot('crossbow', 10)], checkpoints: [9, 17, 22, 27] },
    { name: 'aim_tome', family: 'tomo_fogo', frames: [...up(10), ...hold(8, { aimPitch: 0.2 }), ...shot('cast', 10, { aimPitch: 0.2 })], checkpoints: [9, 17, 22, 27] },
    { name: 'aim_staff', family: 'cajado_gelo', frames: [...up(10, { mps: 1.2, fwd: 1 }), ...hold(10, { mps: 1.2, fwd: 1 })], checkpoints: [6, 19] },
  ];
}
// Um roteiro por tipo de golpe e por caso de esquiva, golpe, queda, agachar, pulo e canal, com poses
// no meio da ação (no fim de uma rampa até 1 o peso do clipe já zerou).
const ONE = ['kachujin'];
function actionScenarios() {
  const act = (kind, family, n = 14) => ({ name: `kind_${kind}`, family, models: ONE, frames: ramp(n, (i, m) => ({ mps: 0, attack: 0.65 * i / (m - 1), kind })), checkpoints: [Math.floor(n / 2), n - 1] });
  const dodge = (key, family) => ({ name: `dodge_${key}`, family, models: ONE, frames: [...ramp(8, (i) => ({ mps: 0, dodge: Math.min(1, (i * DT) / 0.42), dodgeKey: key })), ...ramp(4, () => ({ mps: 0 }))], checkpoints: [7, 9, 11] });
  return [
    act('thrust', 'lanca'), act('rapid', 'manoplas'), act('spin', 'foice'), act('crossbow', 'besta'),
    act('charge', 'arco'), act('heavy', 'machado'), act('cast', 'cajado_gelo'), act('bow', 'arco'),
    act('slash', 'espada'), act('swing', 'maca'),
    dodge('dodgeForward', 'besta'), dodge('dodgeBackward', null), dodge('dodgeRight', 'arco'), dodge('roll', 'adaga'),
    { name: 'hit_front_idle', family: 'espada', models: ONE, frames: ramp(12, (i) => ({ mps: 0, hit: { side: 'front', t: i * DT } })), checkpoints: [5, 11] },
    { name: 'hit_right_walk', family: 'espada', models: ONE, frames: [...ramp(8, () => ({ mps: 2, fwd: 1 })), ...ramp(12, (i) => ({ mps: 2, fwd: 1, hit: { side: 'right', t: i * DT } }))], checkpoints: [13, 19] },
    { name: 'hit_during_attack', family: 'espada', models: ONE, frames: [...ramp(8, (i, n) => ({ mps: 0, attack: 0.5 * i / (n - 1), kind: 'slash' })), ...ramp(8, (i) => ({ mps: 0, attack: 0.5, kind: 'slash', hit: { side: 'back', t: i * DT } }))], checkpoints: [7, 11, 15] },
    { name: 'hit_expire', family: 'espada', models: ONE, frames: ramp(40, (i) => ({ mps: 0, hit: { side: 'left', t: i * DT * 2.5 } })), checkpoints: [10, 25, 39] },
    { name: 'death_backward_walk', family: 'machado', models: ONE, frames: [...ramp(10, () => ({ mps: 2, fwd: 1 })), ...ramp(30, (i) => ({ mps: 0, death: { fall: 'backward', t: i * DT } }))], checkpoints: [15, 25, 39] },
    { name: 'crouch_posture', family: 'espada', models: ONE, frames: [...ramp(5, () => ({ mps: 0 })), ...ramp(20, () => ({ mps: 0, crouch: 1 })), ...ramp(15, () => ({ mps: 0 })), ...ramp(12, () => ({ mps: 1, fwd: -1, crouch: 1 }))], checkpoints: [8, 16, 28, 34, 51] },
    { name: 'air_land', family: 'espada', models: ONE, frames: [...ramp(12, (i, n) => ({ mps: 2.5, fwd: 1, air: { phase: i / (n - 1), boosted: true } })), ...ramp(6, () => ({ mps: 2.5, fwd: 1 }))], checkpoints: [6, 11, 14, 17] },
    { name: 'channel_only', family: 'martelo', models: ONE, frames: ramp(20, () => ({ mps: 0, channel: true })), checkpoints: [10, 19] },
    { name: 'metamorph_walk', family: 'metamorfose', models: ONE, frames: ramp(20, () => ({ mps: 2, fwd: 1, metamorph: true })), checkpoints: [10, 19] },
    { name: 'fists_walk_back', family: 'punhos', models: ONE, frames: ramp(20, () => ({ mps: 1.8, fwd: -1 })), checkpoints: [10, 19] },
  ];
}
function quadScenarios() {
  const base = [
    ...ramp(20, () => ({ mps: 0 })), ...ramp(30, () => ({ mps: 5 })),
    ...ramp(10, (i, n) => ({ mps: 0, windup: i / (n - 1) })), ...ramp(10, (i, n) => ({ mps: 0, attack: i / (n - 1) })),
    ...ramp(8, () => ({ mps: 0, stun: true })), ...ramp(12, () => ({ mps: 0, down: true })),
  ];
  return [
    { name: 'base', frames: base, checkpoints: [55, 65, 75] },
    { name: 'down_up', frames: [...ramp(12, () => ({ mps: 0, down: true })), ...ramp(14, () => ({ mps: 0 }))], checkpoints: [11, 18, 25], beasts: true },
    { name: 'windup_moving', frames: ramp(12, (i, n) => ({ mps: 3, windup: i / (n - 1) })), checkpoints: [6, 11], beasts: true },
  ];
}
function poseOf(pivot, body, skip = []) {
  // os nós do molde, na ordem de traverse (sem os suportes das armas que makeHumanoid pendura nas mãos)
  const nodes = [];
  const visit = (o) => {
    if (skip.includes(o)) return;
    nodes.push([...o.quaternion.toArray(), ...o.position.toArray(), ...o.scale.toArray()].map((v) => +v.toFixed(7)));
    for (const c of o.children) visit(c);
  };
  visit(pivot);
  const b = [body.rotation.x, body.rotation.y, body.rotation.z, ...body.position.toArray(), ...body.scale.toArray()].map((v) => +v.toFixed(7));
  return { nodes, body: b };
}
function skinSamples(root, pivot, n = 24, skip = []) {
  root.updateMatrixWorld(true);
  // a primeira malha com pele do molde e o índice dela entre as malhas do molde (Rig::meshes)
  let mesh = null, meshIndex = -1, k = 0;
  const visit = (o) => {
    if (skip.includes(o)) return;
    if (o.isMesh) {
      if (!mesh && o.isSkinnedMesh) { mesh = o; meshIndex = k; }
      k++;
    }
    for (const c of o.children) visit(c);
  };
  visit(pivot);
  if (!mesh) return null;
  const count = mesh.geometry.attributes.position.count, v = new THREE.Vector3(), out = [];
  for (let i = 0; i < n; i++) {
    const idx = Math.floor((i + 0.5) / n * count);
    mesh.getVertexPosition(idx, v);
    v.applyMatrix4(mesh.matrixWorld);
    out.push([idx, +v.x.toFixed(6), +v.y.toFixed(6), +v.z.toFixed(6)]);
  }
  return { mesh: meshIndex, vertices: out };
}
function exportAnimParity() {
  const savedRandom = Math.random, savedCam = G.camera;
  Math.random = () => 0.5;
  G.camera = null;
  const X = 40, Z = 375, Y = groundHeight(X, Z);
  const out = { dt: DT, x: X, y: Y, z: Z, humanoids: [], quadrupeds: [] };
  try {
    for (const model of ['kachujin', 'eve', 'paladina']) {
      if (!MODELS[model]) continue;
      for (const sc of humanScenarios()) {
        if (sc.models && !sc.models.includes(model)) continue;
        const race = model === 'eve' ? 'elfo' : model === 'paladina' && sc.name === 'walk_run' ? 'anao' : 'humano';
        const h = makeHumanoid({ model, race });
        h.setWeapon(sc.family);
        h.root.position.set(X, Y, Z);
        const frames = [], checkpoints = [];
        sc.frames.forEach((f, i) => {
          const yaw = sc.yaw ? sc.yaw(i) : 0.6;
          h.root.rotation.y = yaw;
          const s = { dt: DT, attack: -1, dodge: -1, ...f };
          animateHumanoid(h, s);
          frames.push({ yaw, s: f });
          if (sc.checkpoints?.includes(i)) checkpoints.push({ frame: i, ...poseOf(h.pivot, h.body, [h.gripR, h.gripL]) });
        });
        const rec = { model, race, scenario: sc.name, family: sc.family, frames, pose: poseOf(h.pivot, h.body, [h.gripR, h.gripL]) };
        if (checkpoints.length) rec.checkpoints = checkpoints;
        if (sc.name === 'walk_run' || sc.name === 'slash') rec.skin = skinSamples(h.root, h.pivot, 24, [h.gripR, h.gripL]);
        out.humanoids.push(rec);
      }
    }
    const makeQuad = (rig) => {
      if (rig === 'lioness') {
        const saved = MODELS.hound;
        MODELS.hound = null;
        const r = makeBeast({ scale: 1.1 });
        MODELS.hound = saved;
        return r;
      }
      return rig === 'hound' ? makeBeast({ scale: 0.9 }) : makeMount();
    };
    const quads = [];
    if (MODELS.lioness) quads.push(['lioness', 'beast']);
    if (MODELS.hound) quads.push(['hound', 'beast']);
    if (MODELS.horse) quads.push(['horse', 'mount']);
    for (const sc of quadScenarios()) {
      for (const [rig, kind] of quads) {
        if (sc.beasts && kind !== 'beast') continue;
        const r = makeQuad(rig);
        r.root.position.set(X, Y, Z);
        const frames = [], checkpoints = [];
        sc.frames.forEach((f, i) => {
          const yaw = 0.6 + i * 0.02;
          r.root.rotation.y = yaw;
          if (kind === 'mount') animateMount(r, { dt: DT, speed: f.mps / 12.5 });
          else animateBeast(r, { dt: DT, windup: -1, attack: -1, ...f });
          frames.push({ yaw, s: f });
          if (sc.checkpoints?.includes(i)) checkpoints.push({ frame: i, ...poseOf(r.pivot, r.body) });
        });
        const rec = { rig, kind, scenario: sc.name, scale: r.scale, frames, pose: poseOf(r.pivot, r.body),
          skin: kind === 'mount' && sc.name === 'base' ? skinSamples(r.root, r.pivot) : null };
        if (checkpoints.length) rec.checkpoints = checkpoints;
        out.quadrupeds.push(rec);
      }
    }
  } finally {
    Math.random = savedRandom;
    G.camera = savedCam;
  }
  return out;
}

export async function bake(log = () => {}) {
  globalThis.__visRand = mulberry(99);
  Math.random = mulberry(424242);
  const canvas = document.createElement('canvas');
  canvas.width = 256; canvas.height = 256;
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: false });
  const scene = new THREE.Scene();
  G.scene = scene;
  await generateTextures(() => {});
  log('texturas');
  await loadModels(() => {});
  log('modelos');
  await loadWorldScene(scene, (t) => log(`mundo: ${t}`), { density: 1, debug: false, tier: 'alta', renderer });
  buildWorld();
  initTurbulent();
  log('camada de jogo');
  // tudo à vista: nenhum corte por distância, todos os fragmentos da Turbulenta
  updateWorldDetail({ x: 0, z: 0 }, { tileNear: 1e9, blockFar: 1e9, extraFar: 1e9 });
  setTurbulentFx(1);
  scene.updateMatrixWorld(true);

  const ex = new Exporter(renderer);
  const out = {
    schema: 1, lodBands: { tree: [...LOD_BANDS.tree], shrub: [...LOD_BANDS.shrub], shadowFar: LOD_BANDS.shadowFar },
    terrain: [], static: [], instanced: [], lodFields: [], dynamic: {}, templates: {},
  };

  // ------------------------------------------------ relevo (blocos de 64 m, perto e longe)
  // O C++ refaz posições, normais e índices a partir do mesmo heightfield (scene-world.js
  // `tileGeometry`); daqui saem só os pesos das camadas de solo e um resumo das posições e normais
  // para o teste de paridade conferir a malha refeita.
  const tileSets = [[WORLD_SCENE.tiles || [], 0], [WORLD_SCENE.turbTiles || [], 1]];
  const tileLod = (g) => {
    const pos = g.attributes.position.array, nor = g.attributes.normal.array;
    const n = Math.round(Math.sqrt(g.attributes.position.count));
    const sa = g.attributes.splatA.array, sb = g.attributes.splatB.array, sc = g.attributes.splatC.array;
    const splat = new Uint8Array(n * n * 12);
    for (let k = 0; k < n * n; k++) {
      splat.set(sa.subarray(k * 4, k * 4 + 4), k * 12);
      splat.set(sb.subarray(k * 4, k * 4 + 4), k * 12 + 4);
      splat.set(sc.subarray(k * 2, k * 2 + 2), k * 12 + 8);
    }
    // FNV-1a sobre os bytes de posição e normal (float32)
    let h = 0x811c9dc5;
    for (const arr of [pos, nor]) {
      const b = new Uint8Array(arr.buffer, arr.byteOffset, arr.byteLength);
      for (let i = 0; i < b.length; i++) { h ^= b[i]; h = Math.imul(h, 0x01000193) >>> 0; }
    }
    return { n, step: round((pos[(n - 1) * 3] - pos[0]) / (n - 1)), splat: ex.bin.push(splat), hash: h >>> 0 };
  };
  for (const [tiles, map] of tileSets) {
    for (const t of tiles) {
      const g = t.near.geometry, pos = g.attributes.position.array;
      const near = tileLod(g), far = tileLod(t.far.geometry);
      out.terrain.push({
        map, x0: round(pos[0]), z0: round(pos[2]), size: round(near.step * (near.n - 1)),
        x: round(t.x), z: round(t.z), radius: round(t.radius),
        material: await ex.material(t.near.material),
        near, far,
      });
    }
  }
  log(`relevo: ${out.terrain.length} blocos`);

  // ------------------------------------------------ campos de LOD (vegetação)
  const lodMeshSet = new Set();
  const shadowJobs = [];
  for (const f of LOD_FIELDS) {
    const field = { name: f.name, band: f.band, secondary: !!f.secondary, levels: [], chunks: [] };
    const first = f.chunks[0];
    if (!first) continue;
    for (const slot of first.slots) {
      const parts = [];
      for (const im of slot.meshes) {
        const material = await ex.material(im.material);
        const tint = im.instanceColor ? (im.instanceColor === slot.fol ? 'fol' : 'trunk') : null;
        parts.push({ geometry: ex.geometry(im.geometry), material, tint });
      }
      field.levels.push(parts);
    }
    // sombreadores (lod-field.js `shadowParts`): exportados no fim (os índices do resto não mudam)
    if (first.shadow) shadowJobs.push([field, first.shadow]);
    for (const c of f.chunks) {
      for (const s of [...c.slots, ...(c.shadow ? [c.shadow] : [])]) for (const im of s.meshes) lodMeshSet.add(im);
      // por instância: matriz (16), cor da copa (3), tom do tronco (1)
      const items = new Float32Array(c.n * 20);
      for (let i = 0; i < c.n; i++) {
        items.set(c.mats.subarray(i * 16, i * 16 + 16), i * 20);
        items.set(c.fol.subarray(i * 3, i * 3 + 3), i * 20 + 16);
        items[i * 20 + 19] = c.trunk[i * 3];
      }
      field.chunks.push({ n: c.n, cx: round(c.cx), cz: round(c.cz), radius: round(c.radius), items: ex.bin.push(items) });
    }
    field.map = mapOf(first.cx);
    out.lodFields.push(field);
  }
  log(`campos de LOD: ${out.lodFields.length}`);

  // ------------------------------------------------ objetos que mudam em jogo
  const dynamicRoots = new Set();
  out.dynamic.nodes = [];
  for (const n of NODES) {
    dynamicRoots.add(n.mesh);
    out.dynamic.nodes.push({ kind: n.kind, x: round(n.x), z: round(n.z), matrix: mat16(n.mesh.matrixWorld), parts: await template(ex, n.mesh) });
  }
  out.dynamic.shrines = [];
  for (const s of TS.shrines) {
    dynamicRoots.add(s.mesh);
    out.dynamic.shrines.push({ x: round(s.x), z: round(s.z), matrix: mat16(s.mesh.matrixWorld), parts: await template(ex, s.mesh) });
  }
  if (CAMP.extraPalisade) {
    dynamicRoots.add(CAMP.extraPalisade);
    out.dynamic.campPalisade = { matrix: mat16(CAMP.extraPalisade.matrixWorld), parts: await template(ex, CAMP.extraPalisade, { skipHidden: false }) };
  }

  // ------------------------------------------------ cena estática
  const visit = async (o, label) => {
    if (!o.visible || dynamicRoots.has(o) || lodMeshSet.has(o)) return;
    if (o === WORLD_SCENE.terrain || o === WORLD_SCENE.turbTerrain) return;
    if (o.isPoints || o.isLine || o.isSprite) return;
    if (o.parent === scene && hasSkin(o)) return;       // personagens (o cliente monta a partir dos GLB)
    if (o.isInstancedMesh) {
      if (o.count > 0) {
        const matrices = new Float32Array(o.instanceMatrix.array.buffer, o.instanceMatrix.array.byteOffset, o.count * 16).slice();
        // a matriz da malha entra em cada instância
        const m = new THREE.Matrix4(), w = o.matrixWorld;
        for (let i = 0; i < o.count; i++) { m.fromArray(matrices, i * 16); m.premultiply(w); m.toArray(matrices, i * 16); }
        const center = new THREE.Vector3().setFromMatrixPosition(o.matrixWorld);
        out.instanced.push({
          map: mapOf(o.count ? matrices[12] : center.x), group: label,
          geometry: ex.geometry(o.geometry), material: await ex.material(Array.isArray(o.material) ? o.material[0] : o.material),
          count: o.count, matrices: ex.bin.push(matrices), cast: !!o.castShadow,
          colors: o.instanceColor ? ex.bin.push(new Float32Array(o.instanceColor.array.subarray(0, o.count * 3))) : -1,
        });
      }
    } else if (o.isMesh && !o.isSkinnedMesh) {
      if (!o.geometry.boundingSphere) o.geometry.computeBoundingSphere();
      const c = o.geometry.boundingSphere.center.clone().applyMatrix4(o.matrixWorld);
      for (const d of await ex.draws(o, mat16(o.matrixWorld)))
        out.static.push({ ...d, map: mapOf(c.x), group: label, renderOrder: o.renderOrder || 0, name: o.name || '' });
    }
    for (const ch of o.children) await visit(ch, label);
  };
  for (const top of scene.children) await visit(top, top.name || top.type);
  log(`estáticos: ${out.static.length} malhas, ${out.instanced.length} instanciadas`);

  // ------------------------------------------------ modelos instanciados em jogo
  out.templates.portal = await template(ex, portalMesh());
  for (const own of [true, false]) {
    const bag = createLootBag(0, 0, [], 'bake', { own });
    out.templates[own ? 'lootBagOwn' : 'lootBag'] = await template(ex, bag.mesh);
    bag.remove();
  }
  out.templates.weapons = {};
  for (const fam of Object.keys(WEAPONS)) {
    if (fam === 'punhos') continue;
    out.templates.weapons[fam] = await template(ex, weaponMesh(fam, false));
    out.templates.weapons[`${fam}:sombra`] = await template(ex, weaponMesh(fam, true));
  }
  // projéteis: as mesmas geometrias e materiais de combat.js `shoot` (cor = emissivo × 0,85)
  out.templates.projectiles = {};
  const arrowGeo = new THREE.BoxGeometry(0.07, 0.07, 1.0);
  const boltGeo = new THREE.BoxGeometry(0.09, 0.09, 0.65);
  const spellGeo = new THREE.SphereGeometry(0.24, 8, 6);
  const shot = (geo, color) => new THREE.Mesh(geo, mat(color, { emissive: color, emissiveIntensity: 0.85 }));
  const variants = {
    'arrow:player': [arrowGeo, '#f3e2b0'], 'arrow:shadow': [arrowGeo, '#d9c4ff'], 'arrow:enemy': [arrowGeo, '#ff9a74'],
    bolt: [boltGeo, '#f0e6d2'], spell: [spellGeo, '#9fe2f2'],
    'spell:ice': [spellGeo, '#70d6ff'], 'spell:fire': [spellGeo, '#ff6b35'], 'spell:nature': [spellGeo, '#52b788'],
    'spell:wind': [spellGeo, '#90e0ef'], 'spell:curse': [spellGeo, '#9b5de5'], 'spell:holy': [spellGeo, '#ffd166'],
    'spell:vital': [spellGeo, '#a7c957'],
  };
  for (const [k, [geo, color]] of Object.entries(variants)) out.templates.projectiles[k] = await template(ex, shot(geo, color));
  log('modelos dinâmicos');

  // ------------------------------------------------ personagens (models.js + characters.js)
  out.rigs = await exportRigs(ex);
  out.clipMeta = MODELS.clipMeta;
  out.animParity = exportAnimParity();
  log(`personagens: ${Object.keys(out.rigs).join(', ')}`);

  // ------------------------------------------------ acréscimos da fase 7 (depois de tudo: os índices de
  // geometrias, materiais e texturas do resto ficam como estavam)
  // sombreadores dos campos de LOD (lod-field.js `shadowParts`): só desenham na passada de sombra
  for (const [field, slot] of shadowJobs) {
    field.shadow = [];
    for (const im of slot.meshes) {
      const tint = im.instanceColor ? (im.instanceColor === slot.fol ? 'fol' : 'trunk') : null;
      field.shadow.push({ geometry: ex.geometry(im.geometry), material: await ex.material(im.material), tint });
    }
  }
  // ------------------------------------------------ cobertura do chão (ground-cover.js), célula a célula
  // generateCell é determinístico por célula; o cliente só escolhe as células em volta da câmera e
  // corta cada lista pela densidade (a lista já vem em ordem aleatória). Por instância, 16 bytes:
  // x e z (u16 na célula), y (f32), escala e escala vertical (u16, ×1000), giro (u8) e cor (3 × u8, ×127,5).
  {
    const KINDS = ['grass', 'flower', 'mushroom', 'pebble', 'branch', 'meadow', 'meadowTall'];
    const VARIANTS = { pebble: 5, meadow: 8, meadowTall: 3 };
    const NC = 1024 / COVER_CELL;
    const cover = { cell: COVER_CELL, origin: -512, n: NC, meadowStep: COVER.meadowStep, kinds: [], cells: [] };
    for (const k of KINDS) {
      const variants = [];
      for (let v = 0; v < (VARIANTS[k] || 1); v++) {
        const lv = {};
        for (const [key, level] of [['near', 1], ['far', 2]]) {
          lv[key] = [];
          for (const [geo, material] of levelParts(k, v, level)) lv[key].push({ geometry: ex.geometry(geo), material: await ex.material(material) });
        }
        variants.push(lv);
      }
      cover.kinds.push({ name: k, variants });
    }
    const items = [];
    let total = 0;
    for (let j = 0; j < NC; j++) for (let i = 0; i < NC; i++) {
      const data = generateCell(i, j);
      const x0 = -512 + i * COVER_CELL, z0 = -512 + j * COVER_CELL;
      const kinds = [];
      let any = false;
      for (const k of KINDS) {
        const list = data[k];
        kinds.push([data.variant[k] || 0, total, list.length]);
        if (list.length) any = true;
        const buf = new ArrayBuffer(list.length * 16), dv = new DataView(buf);
        list.forEach((it, n) => {
          const o = n * 16;
          const q = (v, lo, hi) => Math.max(lo, Math.min(hi, Math.round(v)));
          dv.setUint16(o, q((it.x - x0) / COVER_CELL * 65535, 0, 65535), true);
          dv.setUint16(o + 2, q((it.z - z0) / COVER_CELL * 65535, 0, 65535), true);
          dv.setFloat32(o + 4, it.y, true);
          dv.setUint16(o + 8, q(it.s * 1000, 0, 65535), true);
          dv.setUint16(o + 10, q(it.sy * 1000, 0, 65535), true);
          const rot = ((it.rot % (Math.PI * 2)) + Math.PI * 2) % (Math.PI * 2);
          dv.setUint8(o + 12, q(rot / (Math.PI * 2) * 256, 0, 256) & 255);
          for (let c = 0; c < 3; c++) dv.setUint8(o + 13 + c, q(it.c[c] * 127.5, 0, 255));
        });
        items.push(new Uint8Array(buf));
        total += list.length;
      }
      if (any) cover.cells.push({ i, j, kinds });
    }
    const all = new Uint8Array(total * 16);
    let off = 0;
    for (const b of items) { all.set(b, off); off += b.length; }
    cover.count = total;
    cover.items = ex.bin.push(all);
    out.groundCover = cover;
    log(`cobertura do chão: ${cover.cells.length} células, ${total} instâncias`);
  }

  out.geometries = ex.geometries;
  out.materials = ex.materials;
  out.textures = ex.textures;
  out.sources = ex.sources;
  out.binSize = ex.bin.size;
  return { json: out, chunks: ex.bin.chunks };
}
