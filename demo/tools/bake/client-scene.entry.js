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
import { weaponMesh } from '../../src/engine/characters.js';
import { portalMesh, mat } from '../../src/engine/props.js';
import { LOD_FIELDS, LOD_BANDS } from '../../src/engine/lod-field.js';
import { loadWorldScene } from '../../src/world/index.js';
import { WORLD_SCENE, updateWorldDetail, setTurbulentFx } from '../../src/world/scene-world.js';
import { buildWorld, NODES } from '../../src/game/world.js';
import { initTurbulent, TS } from '../../src/game/turbulent.js';
import { CAMP } from '../../src/game/camp.js';
import { createLootBag } from '../../src/game/zones.js';
import { TURB_GATE_X } from '../../src/world/coordinates.js';

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
      if (!['position', 'normal', 'uv', 'color', 'splatA', 'splatB', 'splatC', 'aDepth'].includes(name)) continue;
      const raw = a.isInterleavedBufferAttribute ? null : a.array;
      if (raw instanceof Uint8Array && a.normalized) {
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
        out.push({ geometry, material: await this.material(mats[g.material]), start: g.start, count: g.count, matrix });
      }
    } else {
      out.push({ geometry, material: await this.material(mats[0]), matrix });
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
          count: o.count, matrices: ex.bin.push(matrices),
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

  out.geometries = ex.geometries;
  out.materials = ex.materials;
  out.textures = ex.textures;
  out.sources = ex.sources;
  out.binSize = ex.bin.size;
  return { json: out, chunks: ex.bin.chunks };
}
