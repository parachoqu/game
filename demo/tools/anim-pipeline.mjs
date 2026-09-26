// Peças do processador de animações (`process-animations.mjs`), separadas para os testes poderem
// importá-las sem disparar conversões. Tudo aqui trabalha sobre um Document do glTF-Transform.
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, readFileSync, rmSync, statSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
export const FBX2GLTF = join(HERE, 'node_modules/fbx2gltf/bin', process.platform === 'win32' ? 'Windows_NT' : process.platform === 'darwin' ? 'Darwin' : 'Linux', process.platform === 'win32' ? 'FBX2glTF.exe' : 'FBX2glTF');

export const sha256 = (buf) => createHash('sha256').update(buf).digest('hex');
export const sha256File = (file) => sha256(readFileSync(file));

// FBX → GLB num diretório temporário. `framerate` ('bake30'…) fixa a amostragem: sem ela o FBX2glTF
// instalado assa em ~24 fps. Um GLB antigo com o mesmo nome é apagado antes, para nunca ser
// confundido com o resultado; conversão que falha ou não gera arquivo lança erro.
export function fbx(file, tmpDir, name, { framerate = null } = {}) {
  const out = join(tmpDir, name);
  if (existsSync(out + '.glb')) rmSync(out + '.glb');
  const args = ['--binary', ...(framerate ? ['--anim-framerate', framerate] : []), '--input', file, '--output', out];
  execFileSync(FBX2GLTF, args, { stdio: 'ignore' });
  if (!existsSync(out + '.glb') || statSync(out + '.glb').size === 0) throw new Error(`FBX2glTF não gerou ${name}.glb`);
  return out + '.glb';
}

export const dur = (a) => Math.max(0, ...a.listSamplers().map((s) => s.getInput().getMax([])[0]));

export function disposeAnim(a) {
  for (const c of a.listChannels()) c.dispose();
  for (const s of a.listSamplers()) s.dispose();
  a.dispose();
}
export function dropEmptyAnimations(doc) {
  for (const a of doc.getRoot().listAnimations()) if (dur(a) <= 0) disposeAnim(a);
}

export function removeMeshes(doc) {
  for (const n of doc.getRoot().listNodes()) {
    n.setMesh(null);
    n.setSkin(null);
  }
}

// Derivado só de animação: sem malha, pele, material nem textura; ficam os nós (o esqueleto e a
// altura de repouso do quadril, que o retarget usa) e o clipe.
export function stripToSkeleton(doc) {
  removeMeshes(doc);
  const root = doc.getRoot();
  for (const m of root.listMeshes()) m.dispose();
  for (const s of root.listSkins()) s.dispose();
  for (const m of root.listMaterials()) m.dispose();
  for (const t of root.listTextures()) t.dispose();
}

export function dropStaticChannels(doc, { keepTranslation = /./, dropScale = true } = {}) {
  let n = 0;
  for (const a of doc.getRoot().listAnimations()) for (const c of a.listChannels()) {
    const node = c.getTargetNode(), path = c.getTargetPath(), s = c.getSampler();
    if (!node) continue;
    if (path === 'scale' && dropScale) { c.dispose(); n++; continue; }
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

export function sample(s, t) {
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

export function crop(doc, anim, a, b, fps = 30) {
  const n = Math.round((b - a) * fps) + 1;
  for (const s of anim.listSamplers()) {
    const k = s.getOutput().getElementSize(), times = new Float32Array(n), vals = new Float32Array(n * k);
    for (let i = 0; i < n; i++) { const t = a + (b - a) * i / (n - 1); times[i] = t - a; vals.set(sample(s, t), i * k); }
    s.setInput(doc.createAccessor().setType('SCALAR').setArray(times).setBuffer(s.getInput().getBuffer()));
    s.setOutput(doc.createAccessor().setType(k === 4 ? 'VEC4' : 'VEC3').setArray(vals).setBuffer(s.getOutput().getBuffer()));
    s.setInterpolation('LINEAR');
  }
}

// Biblioteca antiga: tira só a deriva linear do quadril (o clipe termina onde começou).
export function inPlace(anim, rootRe) {
  for (const c of anim.listChannels()) {
    if (c.getTargetPath() !== 'translation' || !rootRe.test(c.getTargetNode().getName())) continue;
    const s = c.getSampler(), t = s.getInput().getArray(), v = s.getOutput().getArray().slice(), T = t[t.length - 1] || 1;
    const last = t.length - 1, dx = v[last * 3] - v[0], dz = v[last * 3 + 2] - v[2];
    for (let i = 0; i < t.length; i++) { v[i * 3] -= dx * t[i] / T; v[i * 3 + 2] -= dz * t[i] / T; }
    s.getOutput().setArray(v);
  }
}

// Janela em que o quadril se desloca na fonte, medida antes de travar X/Z: instantes (s) em que o
// deslocamento horizontal passa de 10 % e de 99 % do máximo. O runtime casa essa janela com o
// deslocamento que o jogo faz (a esquiva, por exemplo). Sem deslocamento relevante, null.
export function motionWindow(anim, rootRe = /Hips$/) {
  const c = anim.listChannels().find((ch) => ch.getTargetPath() === 'translation' && rootRe.test(ch.getTargetNode().getName()));
  if (!c) return null;
  const rest = c.getTargetNode().getTranslation()[1] || 1;
  const t = c.getSampler().getInput().getArray(), v = c.getSampler().getOutput().getArray();
  const d = [];
  for (let i = 0; i < t.length; i++) d.push(Math.hypot(v[i * 3] - v[0], v[i * 3 + 2] - v[2]));
  const D = Math.max(...d);
  if (D < 0.2 * rest) return null;
  const at = (f) => { for (let i = 0; i < t.length; i++) if (d[i] >= f * D) return Math.round(t[i] * 1000) / 1000; return t[t.length - 1]; };
  return [at(0.1), at(0.99)];
}

// Estritamente no lugar: X e Z do quadril ficam na posição de repouso do esqueleto em TODAS as
// amostras. O deslocamento é do jogo; do clipe sobra só a altura (agachar, cair, levantar).
export function lockHipsXZ(anim, rootRe = /Hips$/) {
  let n = 0;
  for (const c of anim.listChannels()) {
    if (c.getTargetPath() !== 'translation' || !rootRe.test(c.getTargetNode().getName())) continue;
    const rest = c.getTargetNode().getTranslation();
    const s = c.getSampler(), v = s.getOutput().getArray().slice();
    for (let i = 0; i < v.length; i += 3) { v[i] = rest[0]; v[i + 2] = rest[2]; }
    s.getOutput().setArray(v);
    n++;
  }
  return n;
}

export function capFps(doc, fps = 30) {
  for (const a of doc.getRoot().listAnimations()) {
    const d = dur(a), keys = Math.max(...a.listSamplers().map((s) => s.getInput().getCount()));
    if (d > 0 && keys / d > fps * 1.2) crop(doc, a, 0, d, fps);
  }
}

export function dropOrphans(doc) {
  for (const a of doc.getRoot().listAccessors()) if (a.listParents().every((p) => p === doc.getRoot())) a.dispose();
}

// Pulo: só o trecho no ar interessa (a subida e a queda vêm da física do jogo). A decolagem e o pouso
// são os instantes em que o quadril passa 4 % acima da altura em pé; depois disso a altura do quadril
// é fixada na de pé, para o corpo não subir duas vezes (clipe + física). O que sobra é a pose: pernas
// recolhidas, braços e tronco.
export function airborne(doc, anim) {
  const ch = anim.listChannels().find((c) => c.getTargetPath() === 'translation' && /Hips$/.test(c.getTargetNode().getName()));
  if (!ch) return null;
  const node = ch.getTargetNode(), rest = node.getTranslation()[1];
  const s = ch.getSampler(), t = s.getInput().getArray(), v = s.getOutput().getArray();
  let a = -1, b = -1;
  for (let i = 0; i < t.length; i++) if (v[i * 3 + 1] > rest * 1.04) { if (a < 0) a = t[i]; b = t[i]; }
  if (a < 0 || b <= a) return null;
  const T = dur(anim);
  crop(doc, anim, Math.max(0, a - 0.05), Math.min(T, b + 0.05));
  const out = s.getOutput().getArray().slice();
  for (let i = 0; i < out.length / 3; i++) out[i * 3 + 1] = rest;
  s.getOutput().setArray(out);
  return { takeoff: a, landing: b };
}

// Impressão digital dos dados do clipe (tempos e valores de todos os canais, em ordem de nome):
// dois derivados com a mesma impressão são o mesmo clipe.
export function clipFingerprint(anim) {
  const h = createHash('sha256');
  const chans = anim.listChannels().map((c) => [`${c.getTargetNode()?.getName()}.${c.getTargetPath()}`, c.getSampler()])
    .sort((a, b) => (a[0] < b[0] ? -1 : 1));
  for (const [name, s] of chans) {
    h.update(name);
    h.update(Buffer.from(new Float32Array(s.getInput().getArray()).buffer));
    h.update(Buffer.from(new Float32Array(s.getOutput().getArray()).buffer));
  }
  return h.digest('hex');
}

// Contrato de um derivado do lote: devolve a lista de problemas (vazia = válido).
//   exatamente um clipe, com o nome da chave, duração e canais; sem malha, pele nem material; nó
//   Hips presente; X/Z da translação do quadril constantes em todas as amostras.
export function validateClipDoc(doc, key) {
  const root = doc.getRoot(), errors = [];
  const anims = root.listAnimations();
  if (anims.length !== 1) errors.push(`${anims.length} clipes (esperado 1)`);
  const a = anims[0];
  if (a) {
    if (a.getName() !== key) errors.push(`clipe "${a.getName()}" (esperado "${key}")`);
    if (!(dur(a) > 0)) errors.push('clipe com duração zero');
    if (!a.listChannels().length) errors.push('clipe sem canais');
  }
  if (root.listMeshes().length) errors.push(`${root.listMeshes().length} malha(s)`);
  if (root.listSkins().length) errors.push(`${root.listSkins().length} pele(s)`);
  if (root.listMaterials().length) errors.push(`${root.listMaterials().length} material(is)`);
  const hips = root.listNodes().find((n) => /Hips$/.test(n.getName()));
  if (!hips) errors.push('sem nó Hips');
  if (a && hips) {
    for (const c of a.listChannels()) {
      if (c.getTargetNode() !== hips || c.getTargetPath() !== 'translation') continue;
      const v = c.getSampler().getOutput().getArray();
      let drift = 0;
      for (let i = 0; i < v.length; i += 3) drift = Math.max(drift, Math.abs(v[i] - v[0]), Math.abs(v[i + 2] - v[2]));
      if (drift > 1e-5) errors.push(`quadril se desloca em X/Z (${drift.toExponential(2)})`);
    }
  }
  return errors;
}

// Altura de repouso do quadril no esqueleto de origem (a mesma que o runtime lê do GLB).
export function hipsRestY(doc) {
  const hips = doc.getRoot().listNodes().find((n) => /Hips$/.test(n.getName()));
  return hips ? hips.getTranslation()[1] : null;
}
