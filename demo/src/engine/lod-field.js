// Vegetação instanciada com nível de detalhe por instância.
//
// O `instanced()` de `props.js` troca o nível do bloco inteiro: com blocos de 128 m, o nível "perto"
// acabava desenhando a árvore mais cara a 250 m da câmera. Aqui cada instância escolhe o próprio
// nível pela distância (com histerese de 3 m), e o bloco serve só ao recorte de frustum:
//
//   bloco de 128 m → um InstancedMesh por nível e por peça, todos com a mesma capacidade;
//   as peças de um nível dividem a mesma matriz de instâncias (sobe para a GPU uma vez);
//   blocos inteiros numa só faixa pulam a conta por instância.
//
// Sombras: as peças visíveis não projetam. Cada bloco tem um conjunto de "sombreadores" com a malha
// mais simples (L2), preenchido só com as instâncias até `shadowFar`; eles desenham na passada de
// sombra e zeram a contagem na passada de cor (`onBeforeRender`), então não custam nada na imagem.
import * as THREE from 'three';

export const LOD_FIELDS = [];
const HYST = 3;

// faixas por família: limite superior de cada nível, o último é o corte
export const LOD_BANDS = {
  tree: [28, 65, 150, 700],
  shrub: [22, 50, 360],
  shadowFar: 55,
};
let dirty = true, lastX = Infinity, lastZ = Infinity;

export function setLODBands({ tree, shrub, shadowFar } = {}) {
  if (tree) LOD_BANDS.tree = tree;
  if (shrub) LOD_BANDS.shrub = shrub;
  if (shadowFar != null) LOD_BANDS.shadowFar = shadowFar;
  dirty = true;
}
// Densidade dos itens secundários (arbustos, rochas, troncos): prefixo da lista ordenada por hash
// espacial, então rarear é uniforme e voltar a 1 restaura exatamente a mesma população.
export function setLODDensity(factor) {
  for (const f of LOD_FIELDS) if (f.secondary) f.density = Math.max(0, Math.min(1, factor));
  dirty = true;
}

function spatialRank(it) {
  let h = (Math.round(it.x * 100) * 73856093) ^ (Math.round(it.z * 100) * 19349663);
  h = Math.imul(h ^ (h >>> 16), 0x85ebca6b);
  h = Math.imul(h ^ (h >>> 13), 0xc2b2ae35);
  return (h ^ (h >>> 16)) >>> 0;
}

const isFoliage = (m) => (m.alphaTest || 0) > 0;
const _e = new THREE.Euler(0, 0, 0, 'YXZ'), _q = new THREE.Quaternion();
const _p = new THREE.Vector3(), _s = new THREE.Vector3(), _m = new THREE.Matrix4();

function attr(n, size) {
  const a = new THREE.InstancedBufferAttribute(new Float32Array(n * size), size);
  a.setUsage(THREE.DynamicDrawUsage);
  return a;
}

// Uma "fila" de malhas que dividem matriz e cor: um nível de detalhe, ou os sombreadores.
function makeSlot(parts, n, sphere, group, { shadow = false } = {}) {
  const mat = attr(n, 16), fol = attr(n, 3), trunk = attr(n, 3);
  const meshes = parts.map(([geo, material, , depth, tinted]) => {
    const im = new THREE.InstancedMesh(geo, material, n);
    im.instanceMatrix = mat;
    if (tinted) im.instanceColor = isFoliage(material) ? fol : trunk;
    im.count = 0; im.visible = false;
    im.frustumCulled = true;
    im.boundingSphere = sphere;
    im.receiveShadow = !shadow;
    im.castShadow = shadow;
    im.matrixAutoUpdate = false;
    if (shadow) {
      if (depth) im.customDepthMaterial = depth;
      // só desenha na passada de sombra
      im.onBeforeRender = function () { this.userData.keep = this.count; this.count = 0; };
      im.onAfterRender = function () { this.count = this.userData.keep; };
    }
    group.add(im);
    return im;
  });
  return { mat, fol, trunk, meshes, count: -1 };
}

// items: { x, y, z, s, sy, rot, tilt, fol: [r,g,b], trunk }  (fol/trunk: cor da copa e tom do tronco)
// levels: lista de peças por nível, do mais perto ao mais longe; shadowParts: peças dos sombreadores.
export function createLODField(name, items, { levels, band = 'tree', chunk = 128, shadowParts = null, secondary = false, height = 1 } = {}) {
  const group = new THREE.Group();
  group.name = name;
  const buckets = new Map();
  for (const it of items) {
    const k = `${Math.floor(it.x / chunk)},${Math.floor(it.z / chunk)}`;
    if (!buckets.has(k)) buckets.set(k, []);
    buckets.get(k).push(it);
  }
  const chunks = [];
  for (const list of buckets.values()) {
    list.sort((a, b) => spatialRank(a) - spatialRank(b));
    const n = list.length;
    const px = new Float32Array(n), pz = new Float32Array(n);
    const mats = new Float32Array(n * 16), fol = new Float32Array(n * 3), trunk = new Float32Array(n * 3);
    let x0 = Infinity, x1 = -Infinity, y0 = Infinity, y1 = -Infinity, z0 = Infinity, z1 = -Infinity, reach = 0;
    list.forEach((it, i) => {
      px[i] = it.x; pz[i] = it.z;
      _e.set(it.tilt || 0, it.rot || 0, 0);
      _q.setFromEuler(_e);
      _p.set(it.x, it.y, it.z);
      _s.set(it.s, it.sy || it.s, it.s);
      _m.compose(_p, _q, _s).toArray(mats, i * 16);
      const f = it.fol || [1, 1, 1];
      fol[i * 3] = f[0]; fol[i * 3 + 1] = f[1]; fol[i * 3 + 2] = f[2];
      const t = it.trunk ?? 1;
      trunk[i * 3] = t; trunk[i * 3 + 1] = t; trunk[i * 3 + 2] = t;
      const top = it.y + height * (it.sy || it.s);
      x0 = Math.min(x0, it.x); x1 = Math.max(x1, it.x);
      z0 = Math.min(z0, it.z); z1 = Math.max(z1, it.z);
      y0 = Math.min(y0, it.y); y1 = Math.max(y1, top);
      reach = Math.max(reach, height * 0.45 * it.s);
    });
    const cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;
    const sphere = new THREE.Sphere(
      new THREE.Vector3(cx, (y0 + y1) / 2, cz),
      Math.hypot(x1 - x0 + 2 * reach, y1 - y0, z1 - z0 + 2 * reach) / 2,
    );
    const slots = levels.map((parts) => makeSlot(parts, n, sphere, group));
    const shadow = shadowParts ? makeSlot(shadowParts, n, sphere, group, { shadow: true }) : null;
    chunks.push({
      n, px, pz, mats, fol, trunk, cx, cz,
      radius: Math.hypot(x1 - x0, z1 - z0) / 2 + reach,
      slots, shadow, level: new Uint8Array(n).fill(255), mode: -2, limit: -1,
    });
  }
  const field = { name, band, group, chunks, secondary, density: 1, total: items.length };
  LOD_FIELDS.push(field);
  dirty = true;
  return field;
}

// ---------------------------------------------------------------- escrita
function write(slot, c, idx, k) {
  const m = slot.mat.array, f = slot.fol.array, t = slot.trunk.array;
  for (let a = 0; a < k; a++) {
    const i = idx[a];
    for (let e = 0; e < 16; e++) m[a * 16 + e] = c.mats[i * 16 + e];
    f[a * 3] = c.fol[i * 3]; f[a * 3 + 1] = c.fol[i * 3 + 1]; f[a * 3 + 2] = c.fol[i * 3 + 2];
    t[a * 3] = c.trunk[i * 3]; t[a * 3 + 1] = t[a * 3]; t[a * 3 + 2] = t[a * 3];
  }
  commit(slot, k);
}
function writePrefix(slot, c, k) {
  slot.mat.array.set(c.mats.subarray(0, k * 16));
  slot.fol.array.set(c.fol.subarray(0, k * 3));
  slot.trunk.array.set(c.trunk.subarray(0, k * 3));
  commit(slot, k);
}
function commit(slot, k) {
  for (const a of [slot.mat, slot.fol, slot.trunk]) {
    a.clearUpdateRanges();
    a.addUpdateRange(0, Math.max(1, k) * a.itemSize);
    a.needsUpdate = true;
  }
  setCount(slot, k);
}
function setCount(slot, k) {
  slot.count = k;
  for (const im of slot.meshes) { im.count = k; im.visible = k > 0; }
}

const levelOf = (bands, d) => {
  for (let i = 0; i < bands.length; i++) if (d < bands[i]) return i;
  return 255;
};

// ---------------------------------------------------------------- atualização
const scratch = [];
function updateChunk(c, bands, limit, cam, shadowFar) {
  const cut = bands[bands.length - 1];
  const dc = Math.hypot(c.cx - cam.x, c.cz - cam.z);
  const dmin = Math.max(0, dc - c.radius), dmax = dc + c.radius;

  // sombreadores: só perto
  if (c.shadow) {
    if (dmin > shadowFar || shadowFar <= 0) { if (c.shadow.count !== 0) setCount(c.shadow, 0); }
    else {
      const idx = scratch; idx.length = 0;
      for (let i = 0; i < limit; i++) if (Math.hypot(c.px[i] - cam.x, c.pz[i] - cam.z) < shadowFar) idx.push(i);
      write(c.shadow, c, idx, idx.length);
    }
  }

  if (dmin > cut + HYST) {
    if (c.mode !== -1) { for (const s of c.slots) if (s.count !== 0) setCount(s, 0); c.level.fill(255); c.mode = -1; }
    return;
  }
  const lmin = levelOf(bands, dmin), lmax = levelOf(bands, dmax);
  if (lmin === lmax && lmin !== 255) {
    // bloco inteiro numa faixa só
    if (c.mode !== lmin || c.limit !== limit) {
      c.slots.forEach((s, l) => { if (l === lmin) writePrefix(s, c, limit); else if (s.count !== 0) setCount(s, 0); });
      c.level.fill(255); c.level.fill(lmin, 0, limit);
      c.mode = lmin; c.limit = limit;
    }
    return;
  }
  // bloco atravessado por uma fronteira: cada instância escolhe o nível
  let changed = c.mode !== -3 || c.limit !== limit;
  for (let i = 0; i < c.n; i++) {
    let l = 255;
    if (i < limit) {
      const d = Math.hypot(c.px[i] - cam.x, c.pz[i] - cam.z);
      l = levelOf(bands, d);
      const p = c.level[i];
      if (p !== l && p !== 255) {
        const edge = l === 255 ? cut : bands[Math.min(p, l)];
        if (Math.abs(d - edge) < HYST && p < bands.length && bands[p] > 0) l = p;
      }
    }
    if (l !== c.level[i]) { c.level[i] = l; changed = true; }
  }
  c.mode = -3; c.limit = limit;
  if (!changed) return;
  c.slots.forEach((s, l) => {
    const idx = scratch; idx.length = 0;
    for (let i = 0; i < c.n; i++) if (c.level[i] === l) idx.push(i);
    if (idx.length || s.count !== 0) write(s, c, idx, idx.length);
  });
}

export function updateLODFields(cam, force = false) {
  if (!force && !dirty && Math.hypot(cam.x - lastX, cam.z - lastZ) < 1.5) return;
  lastX = cam.x; lastZ = cam.z;
  const wasDirty = dirty;
  dirty = false;
  for (const f of LOD_FIELDS) {
    const bands = LOD_BANDS[f.band];
    const shadowFar = f.band === 'tree' ? LOD_BANDS.shadowFar : LOD_BANDS.shadowFar * 0.6;
    for (const c of f.chunks) {
      if (wasDirty) c.mode = -2;
      const limit = f.secondary ? Math.floor(c.n * f.density) : c.n;
      updateChunk(c, bands, limit, cam, shadowFar);
    }
  }
}

// Soma das instâncias ativas (desenhadas em algum nível) de um campo: usado pelos testes e relatórios.
export function activeCount(field) {
  let n = 0;
  for (const c of field.chunks) for (const s of c.slots) n += Math.max(0, s.count);
  return n;
}

// Todas as malhas dos campos (níveis e sombreadores): a tela de carregamento as deixa visíveis por um
// instante para compilar os sombreadores, senão cada nível compila na primeira vez que aparece.
export function lodMeshes() {
  const out = [];
  for (const f of LOD_FIELDS) for (const c of f.chunks) {
    for (const s of c.slots) out.push(...s.meshes);
    if (c.shadow) out.push(...c.shadow.meshes);
  }
  return out;
}
