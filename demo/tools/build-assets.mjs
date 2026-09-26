// Pipeline de assets 3D da demo: converte os modelos da pasta "modelos 3d animados" em GLBs pequenos
// (malha simplificada, texturas WebP, animações sem canais parados, compressão meshopt) em demo/assets/.
// Uso: cd demo/tools && npm install && node build-assets.mjs
import { execFileSync } from 'node:child_process';
import { mkdirSync, statSync, existsSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { NodeIO } from '@gltf-transform/core';
import { ALL_EXTENSIONS, EXTTextureWebP } from '@gltf-transform/extensions';
import { weld, simplifyPrimitive, resample, prune, dedup, meshopt } from '@gltf-transform/functions';
import { MeshoptEncoder, MeshoptDecoder, MeshoptSimplifier } from 'meshoptimizer';
import sharp from 'sharp';

const HERE = dirname(fileURLToPath(import.meta.url));
const SRC = join(HERE, '../../modelos 3d animados');
const OUT = join(HERE, '../assets');
const TMP = join(tmpdir(), 'projeto-game-assets');
const FBX2GLTF = join(HERE, 'node_modules/fbx2gltf/bin', process.platform === 'win32' ? 'Windows_NT' : process.platform === 'darwin' ? 'Darwin' : 'Linux', process.platform === 'win32' ? 'FBX2glTF.exe' : 'FBX2glTF');
mkdirSync(OUT, { recursive: true }); mkdirSync(TMP, { recursive: true });

await MeshoptEncoder.ready; await MeshoptDecoder.ready; await MeshoptSimplifier.ready;
const io = new NodeIO().registerExtensions(ALL_EXTENSIONS).registerDependencies({ 'meshopt.encoder': MeshoptEncoder, 'meshopt.decoder': MeshoptDecoder });

function fbx(file, name) {
  const out = join(TMP, name);
  execFileSync(FBX2GLTF, ['--binary', '--input', file, '--output', out], { stdio: 'ignore' });
  return out + '.glb';
}

// ---------- utilidades ----------
const dur = (a) => Math.max(0, ...a.listSamplers().map((s) => s.getInput().getMax([])[0]));
// descartar a animação sem os amostradores deixaria os dados deles no arquivo
function disposeAnim(a) { for (const c of a.listChannels()) c.dispose(); for (const s of a.listSamplers()) s.dispose(); a.dispose(); }
function dropEmptyAnimations(doc) { for (const a of doc.getRoot().listAnimations()) if (dur(a) <= 0) disposeAnim(a); }

// Canais que não mexem em nada (constantes e iguais à pose de repouso) só ocupam espaço
function dropStaticChannels(doc, { keepTranslation = /./, dropScale = true } = {}) {
  let n = 0;
  for (const a of doc.getRoot().listAnimations()) for (const c of a.listChannels()) {
    const node = c.getTargetNode(), path = c.getTargetPath(), s = c.getSampler();
    if (!node) continue;
    if (path === 'scale' && dropScale) { c.dispose(); n++; continue; }
    // translações de ossos (exceto a raiz escolhida) forçariam as proporções do esqueleto de origem
    if (path === 'translation' && !keepTranslation.test(node.getName())) { c.dispose(); n++; continue; }
    const v = s.getOutput().getArray(), k = s.getOutput().getElementSize();
    const rest = path === 'rotation' ? node.getRotation() : path === 'translation' ? node.getTranslation() : path === 'scale' ? node.getScale() : null;
    if (!rest) continue;
    let flat = true;
    for (let i = k; i < v.length && flat; i++) if (Math.abs(v[i] - v[i % k]) > 1e-4) flat = false;
    if (!flat) continue;
    let same = true;
    for (let j = 0; j < k; j++) if (Math.abs(v[j] - rest[j]) > 1e-4 && !(path === 'rotation' && Math.abs(v[j] + rest[j]) < 1e-4)) same = false;
    if (same) { c.dispose(); n++; }
  }
  for (const a of doc.getRoot().listAnimations()) for (const s of a.listSamplers()) if (!a.listChannels().some((c) => c.getSampler() === s)) s.dispose();
  return n;
}

// amostra um canal no tempo t (linear; quaternions com nlerp)
function sample(s, t) {
  const ti = s.getInput().getArray(), v = s.getOutput().getArray(), k = s.getOutput().getElementSize();
  let i = 0; while (i < ti.length - 1 && ti[i + 1] < t) i++;
  const j = Math.min(i + 1, ti.length - 1), f = ti[j] > ti[i] ? Math.min(1, Math.max(0, (t - ti[i]) / (ti[j] - ti[i]))) : 0;
  const out = new Array(k);
  let dot = 0; if (k === 4) for (let c = 0; c < 4; c++) dot += v[i * 4 + c] * v[j * 4 + c];
  const sgn = k === 4 && dot < 0 ? -1 : 1;
  for (let c = 0; c < k; c++) out[c] = v[i * k + c] * (1 - f) + v[j * k + c] * f * sgn;
  if (k === 4) { const l = Math.hypot(...out); for (let c = 0; c < 4; c++) out[c] /= l; }
  return out;
}

// recorta [a, b] da animação e reamostra a 30 qps, começando em 0
function crop(doc, anim, a, b, fps = 30) {
  const n = Math.round((b - a) * fps) + 1;
  for (const s of anim.listSamplers()) {
    const k = s.getOutput().getElementSize(), times = new Float32Array(n), vals = new Float32Array(n * k);
    for (let i = 0; i < n; i++) { const t = a + (b - a) * i / (n - 1); times[i] = t - a; vals.set(sample(s, t), i * k); }
    s.setInput(doc.createAccessor().setType('SCALAR').setArray(times).setBuffer(s.getInput().getBuffer()));
    s.setOutput(doc.createAccessor().setType(k === 4 ? 'VEC4' : 'VEC3').setArray(vals).setBuffer(s.getOutput().getBuffer()));
    s.setInterpolation('LINEAR');
  }
}

// tira o deslocamento de raiz (caminhada no lugar): remove a tendência linear em X/Z da translação da raiz
function inPlace(anim, rootRe) {
  for (const c of anim.listChannels()) {
    if (c.getTargetPath() !== 'translation' || !rootRe.test(c.getTargetNode().getName())) continue;
    const s = c.getSampler(), t = s.getInput().getArray(), v = s.getOutput().getArray().slice(), T = t[t.length - 1] || 1;
    const last = t.length - 1, dx = v[last * 3] - v[0], dz = v[last * 3 + 2] - v[2];
    for (let i = 0; i < t.length; i++) { v[i * 3] -= dx * t[i] / T; v[i * 3 + 2] -= dz * t[i] / T; }
    s.getOutput().setArray(v);
  }
}

// procura o melhor laço entre [a0,a1] e [b0,b1]: poses mais parecidas nas pontas
function findLoop(anim, [a0, a1], [b0, b1], step = 1 / 30) {
  const rots = anim.listChannels().filter((c) => c.getTargetPath() === 'rotation').map((c) => c.getSampler());
  let best = null;
  for (let a = a0; a <= a1; a += step) for (let b = b0; b <= b1; b += step) {
    let d = 0;
    for (const s of rots) { const p = sample(s, a), q = sample(s, b); d += 1 - Math.abs(p[0] * q[0] + p[1] * q[1] + p[2] * q[2] + p[3] * q[3]); }
    if (!best || d < best.d) best = { a, b, d };
  }
  return best;
}

// Ossos: cada osso custa uma atualização de matriz por quadro e por personagem. Os pesos dos ossos
// de `mergeRe` passam para o osso pai indicado por `into`; depois saem os ossos sem peso (e sem
// descendentes com peso), exceto os de `keepRe`. Os índices de JOINTS_0 e as matrizes de bind são refeitos.
function pruneJoints(doc, { mergeRe = null, into = null, keepRe = /$^/ } = {}) {
  const root = doc.getRoot();
  for (const skin of root.listSkins()) {
    const joints = skin.listJoints(), idx = new Map(joints.map((j, i) => [j, i]));
    const prims = [];
    for (const n of root.listNodes()) if (n.getSkin() === skin && n.getMesh()) prims.push(...n.getMesh().listPrimitives());
    // destino de cada osso (ele mesmo ou o osso em que é mesclado)
    const target = joints.map((j, i) => {
      if (!mergeRe || !mergeRe.test(j.getName())) return i;
      const t = into(j, joints); return t && idx.has(t) ? idx.get(t) : i;
    });
    const weight = new Float64Array(joints.length);
    for (const p of prims) {
      const J = p.getAttribute('JOINTS_0'), W = p.getAttribute('WEIGHTS_0'), a = [0, 0, 0, 0], w = [0, 0, 0, 0];
      for (let v = 0; v < J.getCount(); v++) {
        J.getElement(v, a); W.getElement(v, w);
        for (let c = 0; c < 4; c++) { a[c] = target[a[c]]; weight[a[c]] += w[c]; }
        J.setElement(v, a);
      }
    }
    // mantém ossos com peso, seus ancestrais e os pedidos
    const keep = new Set();
    joints.forEach((j, i) => {
      if (weight[i] > 1e-6 || keepRe.test(j.getName())) for (let n = j; n; n = n.getParentNode()) keep.add(n);
    });
    const kept = joints.filter((j) => keep.has(j));
    if (kept.length === joints.length) continue;
    const remap = new Map(kept.map((j, i) => [idx.get(j), i]));
    for (const p of prims) {
      const J = p.getAttribute('JOINTS_0'), a = [0, 0, 0, 0];
      for (let v = 0; v < J.getCount(); v++) { J.getElement(v, a); for (let c = 0; c < 4; c++) a[c] = remap.get(a[c]) ?? 0; J.setElement(v, a); }
    }
    const ibm = skin.getInverseBindMatrices(), src = ibm.getArray(), out = new Float32Array(kept.length * 16);
    kept.forEach((j, i) => out.set(src.subarray(idx.get(j) * 16, idx.get(j) * 16 + 16), i * 16));
    skin.setInverseBindMatrices(doc.createAccessor().setType('MAT4').setArray(out).setBuffer(ibm.getBuffer()));
    for (const j of joints) skin.removeJoint(j);
    for (const j of kept) skin.addJoint(j);
    // remove os nós dos ossos descartados (sempre subárvores inteiras) e os canais que os animavam
    for (const j of joints) if (!keep.has(j)) j.dispose();
    for (const a of root.listAnimations()) for (const c of a.listChannels()) if (!c.getTargetNode()) c.dispose();
    for (const a of root.listAnimations()) for (const sp of a.listSamplers()) if (!a.listChannels().some((c) => c.getSampler() === sp)) sp.dispose();
    console.log(`  ossos: ${joints.length} → ${kept.length}`);
  }
}
// dedos do Mixamo → mão (mantém o Middle1, usado para posicionar a empunhadura)
const FINGERS = { mergeRe: /Hand(Thumb|Index|Middle|Ring|Pinky)\d/, into: (j, joints) => joints.find((o) => o.getName() === j.getName().replace(/(Thumb|Index|Middle|Ring|Pinky)\d.*$/, '')), keepRe: /HandMiddle1$/ };

function removeMeshes(doc) {
  for (const n of doc.getRoot().listNodes()) { n.setMesh(null); n.setSkin(null); }
}

function dropAttributes(doc, names) {
  for (const m of doc.getRoot().listMeshes()) for (const p of m.listPrimitives()) for (const a of names) if (p.getAttribute(a)) p.setAttribute(a, null);
}

// Specular-glossiness (export do Sketchfab) → metal/rugosidade simples, reaproveitando a textura difusa
function specGlossToMetalRough(doc, roughness = 0.75) {
  for (const m of doc.getRoot().listMaterials()) {
    const ext = m.getExtension('KHR_materials_pbrSpecularGlossiness');
    if (!ext) continue;
    const tex = ext.getDiffuseTexture();
    if (tex) m.setBaseColorTexture(tex);
    m.setBaseColorFactor(ext.getDiffuseFactor()).setMetallicFactor(0).setRoughnessFactor(roughness);
    m.setExtension('KHR_materials_pbrSpecularGlossiness', null);
  }
  for (const e of doc.getRoot().listExtensionsUsed()) if (e.extensionName === 'KHR_materials_pbrSpecularGlossiness') e.dispose();
}

// texturas em WebP, com tamanho máximo por textura (sizeOf recebe nome e slot)
async function webp(doc, sizeOf) {
  doc.createExtension(EXTTextureWebP).setRequired(true);
  const slots = new Map();
  for (const m of doc.getRoot().listMaterials()) {
    if (m.getBaseColorTexture()) slots.set(m.getBaseColorTexture(), 'base');
    if (m.getNormalTexture()) slots.set(m.getNormalTexture(), 'normal');
  }
  for (const t of doc.getRoot().listTextures()) {
    const slot = slots.get(t) || 'other', max = sizeOf(t.getName() || t.getURI() || '', slot);
    const img = await sharp(Buffer.from(t.getImage())).resize(max, max, { fit: 'inside', withoutEnlargement: true })
      .webp({ quality: slot === 'normal' ? 88 : 80, effort: 6 }).toBuffer();
    t.setImage(new Uint8Array(img)).setMimeType('image/webp').setURI((t.getName() || 'tex') + '.webp');
  }
}

function simplifyAll(doc, ratioOf, error = 0.004) {
  for (const m of doc.getRoot().listMeshes()) for (const p of m.listPrimitives()) {
    const r = ratioOf(p.getMaterial()?.getName() || '', p);
    if (r < 1) simplifyPrimitive(p, { simplifier: MeshoptSimplifier, ratio: r, error, lockBorder: false });
  }
}

// quadros-chave demais (o FBX exporta a ~190 qps): reamostra tudo a no máximo `fps`
function capFps(doc, fps = 30) {
  for (const a of doc.getRoot().listAnimations()) {
    const d = dur(a), keys = Math.max(...a.listSamplers().map((s) => s.getInput().getCount()));
    if (d > 0 && keys / d > fps * 1.2) crop(doc, a, 0, d, fps);
  }
}

// o export do Sketchfab cria um esqueleto idêntico por malha: um só basta (menos ossos por quadro)
function shareSkins(doc) {
  const skins = doc.getRoot().listSkins();
  const same = (a, b) => {
    const ja = a.listJoints(), jb = b.listJoints();
    if (ja.length !== jb.length || ja.some((j, i) => j !== jb[i])) return false;
    const ia = a.getInverseBindMatrices()?.getArray(), ib = b.getInverseBindMatrices()?.getArray();
    return ia && ib && ia.length === ib.length && ia.every((v, i) => Math.abs(v - ib[i]) < 1e-5);
  };
  for (const n of doc.getRoot().listNodes()) {
    const sk = n.getSkin(); if (!sk) continue;
    const first = skins.find((o) => same(o, sk));
    if (first && first !== sk) n.setSkin(first);
  }
}

// acessores que ninguém usa (sobras de recortes/reamostragens)
function dropOrphans(doc) {
  for (const a of doc.getRoot().listAccessors()) if (a.listParents().every((p) => p === doc.getRoot())) a.dispose();
}

// volume 'scene': todas as malhas quantizadas no mesmo espaço; com várias malhas no mesmo esqueleto,
// o volume por malha criaria um esqueleto (e matrizes de bind) por malha
async function finish(doc, file, label, { volume = 'mesh' } = {}) {
  capFps(doc);
  await doc.transform(resample({ tolerance: 2e-4 }), dedup(), prune({ keepLeaves: true }));
  dropOrphans(doc);
  await doc.transform(meshopt({ encoder: MeshoptEncoder, level: 'medium', quantizationVolume: volume }));
  shareSkins(doc);
  await doc.transform(prune({ keepLeaves: true }));
  dropOrphans(doc);
  const out = join(OUT, file);
  await io.write(out, doc);
  let tris = 0; for (const m of doc.getRoot().listMeshes()) for (const p of m.listPrimitives()) tris += (p.getIndices() ? p.getIndices().getCount() : p.getAttribute('POSITION').getCount()) / 3;
  console.log(`${label.padEnd(10)} ${file.padEnd(22)} ${(statSync(out).size / 1024).toFixed(0).padStart(6)} KB · ${tris | 0} tri · ${doc.getRoot().listAnimations().map((a) => a.getName() + ' ' + dur(a).toFixed(2) + 's').join(', ')}`);
}

// ---------- personagens (Mixamo) ----------
async function character(src, file, ratio) {
  const doc = await io.read(fbx(join(SRC, src), file.replace('.glb', '')));
  dropEmptyAnimations(doc);
  // peça totalmente transparente (sobra do export FBX) não aparece e só custa desenho
  for (const m of doc.getRoot().listMeshes()) for (const p of m.listPrimitives()) {
    const mat = p.getMaterial();
    if (mat && mat.getAlphaMode() === 'BLEND' && mat.getBaseColorFactor()[3] < 0.05) p.dispose();
  }
  for (const mat of doc.getRoot().listMaterials()) mat.setBaseColorFactor([1, 1, 1, 1]).setMetallicFactor(0).setRoughnessFactor(0.72);
  pruneJoints(doc, FINGERS);
  await doc.transform(weld());
  simplifyAll(doc, () => ratio);
  await webp(doc, () => 1024);
  await finish(doc, file, 'personagem');
}

// ---------- animações Mixamo (só os ossos) ----------
async function mixamoClip(src, file, name, loop) {
  const doc = await io.read(fbx(join(SRC, src), file.replace('.glb', '')));
  dropEmptyAnimations(doc);
  removeMeshes(doc);
  const anim = doc.getRoot().listAnimations()[0];
  anim.setName(name);
  dropStaticChannels(doc, { keepTranslation: /Hips$/ });
  if (loop) { const l = findLoop(anim, loop[0], loop[1]); crop(doc, anim, l.a, l.b); console.log(`  laço ${name}: ${l.a.toFixed(2)}–${l.b.toFixed(2)} s (dif ${l.d.toFixed(3)})`); }
  inPlace(anim, /Hips$/);
  await finish(doc, file, 'animação');
}

// ---------- leoa (Garras) ----------
async function lioness() {
  if (!existsSync(join(TMP, 'lioness'))) execFileSync('unzip', ['-o', '-q', join(SRC, 'lioness-realistic-3d-model-demo-free/source/source.zip'), '-d', join(TMP, 'lioness')]);
  const doc = await io.read(fbx(join(TMP, 'lioness/source/LIONESS_DEMO.fbx'), 'lioness'));
  for (const a of doc.getRoot().listAnimations()) {
    const n = a.getName();
    if (/Idle$/.test(n)) disposeAnim(a);                // 17 s de ocioso: a respiração em laço já basta
    else a.setName(/Walk/.test(n) ? 'walk' : 'idle');
  }
  dropStaticChannels(doc, { keepTranslation: /^(Root|Pelvis)$/ });
  dropAttributes(doc, ['COLOR_0', 'TEXCOORD_1']);
  pruneJoints(doc, { keepRe: /^(Head|Jaw)$/ });
  for (const mat of doc.getRoot().listMaterials()) mat.setMetallicFactor(0).setRoughnessFactor(0.85);
  await doc.transform(weld());
  simplifyAll(doc, () => 0.42);
  await webp(doc, () => 1024);
  await finish(doc, 'beast_lioness.glb', 'criatura');
}

// ---------- cavalo (montaria) ----------
async function horse() {
  const doc = await io.read(join(SRC, 'horse_-_realistic_3d_model_demo_free/scene.gltf'));
  specGlossToMetalRough(doc);
  for (const a of doc.getRoot().listAnimations()) a.setName(/Walk/.test(a.getName()) ? 'walk' : 'idle');
  const idle = doc.getRoot().listAnimations().find((a) => a.getName() === 'idle');
  dropStaticChannels(doc, { keepTranslation: /^(root|pelvis)/ });
  if (idle && dur(idle) > 6) { const l = findLoop(idle, [0, 0.5], [3.5, 6], 1 / 15); crop(doc, idle, l.a, l.b, 15); console.log(`  laço ocioso do cavalo: ${l.a.toFixed(2)}–${l.b.toFixed(2)} s (dif ${l.d.toFixed(3)})`); }
  dropAttributes(doc, ['TEXCOORD_1']);
  pruneJoints(doc);
  const hair = doc.getRoot().listMaterials().find((m) => /Hair/.test(m.getName()));
  if (hair) hair.setAlphaMode('MASK').setAlphaCutoff(0.4).setDoubleSided(true);
  await doc.transform(weld());
  simplifyAll(doc, (name) => (/Body/.test(name) ? 0.3 : /Hair/.test(name) ? 0.35 : /Saddle2/.test(name) ? 0.2 : /Saddle1/.test(name) ? 0.3 : 0.12), 0.006);
  await webp(doc, (name) => (/Body/.test(name) ? 1024 : /Eye/.test(name) ? 128 : 512));
  await finish(doc, 'mount_horse.glb', 'montaria', { volume: 'scene' });
}

rmSync(OUT, { recursive: true, force: true }); mkdirSync(OUT, { recursive: true });
await character('Kachujin G Rosales.fbx', 'char_kachujin.glb', 0.6);
await character('Eve By J.Gonzales.fbx', 'char_eve.glb', 0.3);
await mixamoClip('Unarmed Walk Forward.fbx', 'anim_walk.glb', 'walk');
await lioness();
await horse();
