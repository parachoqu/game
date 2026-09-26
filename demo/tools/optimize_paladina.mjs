import { statSync, rmSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { NodeIO } from '@gltf-transform/core';
import { ALL_EXTENSIONS, EXTTextureWebP } from '@gltf-transform/extensions';
import { weld, simplifyPrimitive, resample, prune, dedup, meshopt } from '@gltf-transform/functions';
import { MeshoptEncoder, MeshoptDecoder, MeshoptSimplifier } from 'meshoptimizer';
import sharp from 'sharp';

const HERE = dirname(fileURLToPath(import.meta.url));
const RAW = join(HERE, '../assets/char_paladina_raw.glb');
const OUT = join(HERE, '../assets/char_paladina.glb');

await MeshoptEncoder.ready;
await MeshoptDecoder.ready;
await MeshoptSimplifier.ready;

const io = new NodeIO()
  .registerExtensions(ALL_EXTENSIONS)
  .registerDependencies({
    'meshopt.encoder': MeshoptEncoder,
    'meshopt.decoder': MeshoptDecoder
  });

console.log('Loading raw GLB...');
const doc = await io.read(RAW);
const root = doc.getRoot();

// 1. Remove Cornea primitives so the eyes (iris/sclera) are crystal clear
for (const m of root.listMeshes()) {
  for (const p of m.listPrimitives()) {
    const matName = p.getMaterial()?.getName() || '';
    if (/Cornea/i.test(matName)) {
      console.log(`Disposing Cornea primitive in mesh ${m.getName()}`);
      p.dispose();
    }
  }
}

// 2. Configure Alpha modes for Hair and Eyelash
for (const mat of root.listMaterials()) {
  const name = mat.getName();
  mat.setEmissiveFactor([0, 0, 0]);
  mat.setEmissiveTexture(null);
  for (const extName of ['KHR_materials_anisotropy', 'KHR_materials_sheen', 'KHR_materials_specular', 'KHR_materials_ior']) {
    mat.setExtension(extName, null);
  }
  if (/Hair/i.test(name)) {
    console.log(`Setting MASK alpha on ${name}`);
    mat.setAlphaMode('MASK').setAlphaCutoff(0.35).setDoubleSided(true);
  } else if (/Eyelash/i.test(name)) {
    console.log(`Setting MASK alpha on ${name}`);
    mat.setAlphaMode('MASK').setAlphaCutoff(0.4).setDoubleSided(true);
  } else if (/Bra/i.test(name) || /Underwear/i.test(name)) {
    mat.setAlphaMode('OPAQUE');
    mat.setDoubleSided(true);
  } else if (/Skin/i.test(name)) {
    mat.setAlphaMode('OPAQUE');
    mat.setRoughnessFactor(0.72).setMetallicFactor(0.0);
  }
}

for (const ext of root.listExtensionsUsed()) {
  if (['KHR_materials_anisotropy', 'KHR_materials_sheen', 'KHR_materials_specular', 'KHR_materials_ior'].includes(ext.extensionName)) {
    ext.dispose();
  }
}

// 3. Prune finger joints (merge into Hand, keep HandMiddle1 for weapon grip)
const FINGERS = {
  mergeRe: /Hand(Thumb|Index|Middle|Ring|Pinky)\d/,
  into: (j, joints) => joints.find((o) => o.getName() === j.getName().replace(/(Thumb|Index|Middle|Ring|Pinky)\d.*$/, '')),
  keepRe: /HandMiddle1$/
};

function pruneJoints(doc, { mergeRe = null, into = null, keepRe = /$^/ } = {}) {
  for (const skin of root.listSkins()) {
    const joints = skin.listJoints(), idx = new Map(joints.map((j, i) => [j, i]));
    const prims = [];
    for (const n of root.listNodes()) if (n.getSkin() === skin && n.getMesh()) prims.push(...n.getMesh().listPrimitives());
    const target = joints.map((j, i) => {
      if (!mergeRe || !mergeRe.test(j.getName())) return i;
      const t = into(j, joints); return t && idx.has(t) ? idx.get(t) : i;
    });
    const weight = new Float64Array(joints.length);
    for (const p of prims) {
      const J = p.getAttribute('JOINTS_0'), W = p.getAttribute('WEIGHTS_0'), a = [0, 0, 0, 0], w = [0, 0, 0, 0];
      if (!J || !W) continue;
      for (let v = 0; v < J.getCount(); v++) {
        J.getElement(v, a); W.getElement(v, w);
        for (let c = 0; c < 4; c++) { a[c] = target[a[c]]; weight[a[c]] += w[c]; }
        J.setElement(v, a);
      }
    }
    const keep = new Set();
    joints.forEach((j, i) => {
      if (weight[i] > 1e-6 || keepRe.test(j.getName())) for (let n = j; n; n = n.getParentNode()) keep.add(n);
    });
    const kept = joints.filter((j) => keep.has(j));
    if (kept.length === joints.length) continue;
    const remap = new Map(kept.map((j, i) => [idx.get(j), i]));
    for (const p of prims) {
      const J = p.getAttribute('JOINTS_0'), a = [0, 0, 0, 0];
      if (!J) continue;
      for (let v = 0; v < J.getCount(); v++) { J.getElement(v, a); for (let c = 0; c < 4; c++) a[c] = remap.get(a[c]) ?? 0; J.setElement(v, a); }
    }
    const ibm = skin.getInverseBindMatrices(), src = ibm.getArray(), out = new Float32Array(kept.length * 16);
    kept.forEach((j, i) => out.set(src.subarray(idx.get(j) * 16, idx.get(j) * 16 + 16), i * 16));
    skin.setInverseBindMatrices(doc.createAccessor().setType('MAT4').setArray(out).setBuffer(ibm.getBuffer()));
    for (const j of joints) skin.removeJoint(j);
    for (const j of kept) skin.addJoint(j);
    for (const j of joints) if (!keep.has(j)) j.dispose();
    console.log(`Bones pruned: ${joints.length} → ${kept.length}`);
  }
}

pruneJoints(doc, FINGERS);

// 4. Simplify geometry
console.log('Simplifying geometry...');
await doc.transform(weld());
for (const m of root.listMeshes()) {
  for (const p of m.listPrimitives()) {
    const matName = p.getMaterial()?.getName() || '';
    // Keep face and eyes sharp; simplify body/hair/cloth
    const ratio = /Eye|Teeth|Tongue/.test(matName) ? 0.8 : /Hair/.test(matName) ? 0.45 : 0.5;
    simplifyPrimitive(p, { simplifier: MeshoptSimplifier, ratio, error: 0.003, lockBorder: false });
  }
}

// 5. Compress textures to WebP 1024
console.log('Compressing textures to WebP...');
doc.createExtension(EXTTextureWebP).setRequired(true);
const slots = new Map();
for (const m of root.listMaterials()) {
  if (m.getBaseColorTexture()) slots.set(m.getBaseColorTexture(), 'base');
  if (m.getNormalTexture()) slots.set(m.getNormalTexture(), 'normal');
}
for (const t of root.listTextures()) {
  const slot = slots.get(t) || 'other';
  const max = /Eye|Nails/.test(t.getName() || '') ? 512 : 1024;
  try {
    const rawImg = Buffer.from(t.getImage());
    const img = await sharp(rawImg)
      .resize(max, max, { fit: 'inside', withoutEnlargement: true })
      .webp({ quality: slot === 'normal' ? 88 : 82, effort: 5 })
      .toBuffer();
    t.setImage(new Uint8Array(img)).setMimeType('image/webp').setURI((t.getName() || 'tex') + '.webp');
  } catch (err) {
    console.warn(`Could not compress texture ${t.getName()}:`, err.message);
  }
}

// 6. Meshopt & prune
console.log('Finalizing with meshopt...');
for (const a of root.listAccessors()) if (a.listParents().every((p) => p === root)) a.dispose();
await doc.transform(resample({ tolerance: 2e-4 }), dedup(), prune({ keepLeaves: true }));
await doc.transform(meshopt({ encoder: MeshoptEncoder, level: 'medium', quantizationVolume: 'mesh' }));
await doc.transform(prune({ keepLeaves: true }));
for (const a of root.listAccessors()) if (a.listParents().every((p) => p === root)) a.dispose();

// Write out
await io.write(OUT, doc);
const sizeKb = (statSync(OUT).size / 1024).toFixed(0);
let tris = 0;
for (const m of root.listMeshes()) for (const p of m.listPrimitives()) tris += (p.getIndices() ? p.getIndices().getCount() : p.getAttribute('POSITION').getCount()) / 3;
console.log(`SUCCESS! Saved ${OUT} (${sizeKb} KB, ${tris | 0} triangles, ${root.listSkins()[0].listJoints().length} bones)`);
