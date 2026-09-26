// Processa animações Mixamo FBX em GLBs otimizados
import { execFileSync } from 'node:child_process';
import { mkdirSync, statSync, rmSync, existsSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { NodeIO } from '@gltf-transform/core';
import { ALL_EXTENSIONS } from '@gltf-transform/extensions';
import { resample, prune, dedup, meshopt } from '@gltf-transform/functions';
import { MeshoptEncoder, MeshoptDecoder } from 'meshoptimizer';

const HERE = dirname(fileURLToPath(import.meta.url));
const SRC_ANIM = join(HERE, '../../modelos 3d animados/animacoes');
const OUT_GLB_LIB = join(HERE, '../../modelos 3d animados/animacoes/glb');
const OUT_ASSETS = join(HERE, '../assets');
const TMP = join(tmpdir(), 'rpg-anims-tmp');

const FBX2GLTF = join(HERE, 'node_modules/fbx2gltf/bin', process.platform === 'win32' ? 'Windows_NT' : process.platform === 'darwin' ? 'Darwin' : 'Linux', process.platform === 'win32' ? 'FBX2glTF.exe' : 'FBX2glTF');

mkdirSync(OUT_GLB_LIB, { recursive: true });
mkdirSync(OUT_ASSETS, { recursive: true });
mkdirSync(TMP, { recursive: true });

await MeshoptEncoder.ready;
await MeshoptDecoder.ready;
const io = new NodeIO().registerExtensions(ALL_EXTENSIONS).registerDependencies({
  'meshopt.encoder': MeshoptEncoder,
  'meshopt.decoder': MeshoptDecoder
});

function fbx(file, name) {
  const out = join(TMP, name);
  if (existsSync(out + '.glb')) rmSync(out + '.glb');
  execFileSync(FBX2GLTF, ['--binary', '--input', file, '--output', out], { stdio: 'ignore' });
  return out + '.glb';
}

const dur = (a) => Math.max(0, ...a.listSamplers().map((s) => s.getInput().getMax([])[0]));

function disposeAnim(a) {
  for (const c of a.listChannels()) c.dispose();
  for (const s of a.listSamplers()) s.dispose();
  a.dispose();
}
function dropEmptyAnimations(doc) {
  for (const a of doc.getRoot().listAnimations()) if (dur(a) <= 0) disposeAnim(a);
}

function removeMeshes(doc) {
  for (const n of doc.getRoot().listNodes()) {
    n.setMesh(null);
    n.setSkin(null);
  }
}

function dropStaticChannels(doc, { keepTranslation = /./, dropScale = true } = {}) {
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

function inPlace(anim, rootRe) {
  for (const c of anim.listChannels()) {
    if (c.getTargetPath() !== 'translation' || !rootRe.test(c.getTargetNode().getName())) continue;
    const s = c.getSampler(), t = s.getInput().getArray(), v = s.getOutput().getArray().slice(), T = t[t.length - 1] || 1;
    const last = t.length - 1, dx = v[last * 3] - v[0], dz = v[last * 3 + 2] - v[2];
    for (let i = 0; i < t.length; i++) { v[i * 3] -= dx * t[i] / T; v[i * 3 + 2] -= dz * t[i] / T; }
    s.getOutput().setArray(v);
  }
}

function capFps(doc, fps = 30) {
  for (const a of doc.getRoot().listAnimations()) {
    const d = dur(a), keys = Math.max(...a.listSamplers().map((s) => s.getInput().getCount()));
    if (d > 0 && keys / d > fps * 1.2) crop(doc, a, 0, d, fps);
  }
}

function dropOrphans(doc) {
  for (const a of doc.getRoot().listAccessors()) if (a.listParents().every((p) => p === doc.getRoot())) a.dispose();
}

// Pulo: só o trecho no ar interessa (a subida e a queda vêm da física do jogo). A decolagem e o pouso
// são os instantes em que o quadril passa 4 % acima da altura em pé; depois disso a altura do quadril
// é fixada na de pé, para o corpo não subir duas vezes (clipe + física). O que sobra é a pose: pernas
// recolhidas, braços e tronco.
function airborne(doc, anim) {
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

async function convertClip(srcFile, destName, clipName, opts = {}) {
  const fullSrc = join(SRC_ANIM, srcFile);
  if (opts.optional && !existsSync(fullSrc)) { console.log(`- ${clipName.padEnd(12)} (sem ${srcFile}: pulado)`); return false; }
  const tmpGlb = fbx(fullSrc, destName);
  const doc = await io.read(tmpGlb);
  dropEmptyAnimations(doc);
  removeMeshes(doc);
  const anims = doc.getRoot().listAnimations();
  if (!anims.length) {
    console.error('Sem animações em:', srcFile);
    return;
  }
  const anim = anims[0];
  anim.setName(clipName);
  dropStaticChannels(doc, { keepTranslation: /Hips$/ });
  inPlace(anim, /Hips$/);
  if (opts.air) {
    const cut = airborne(doc, anim);
    if (cut) console.log(`  ${clipName}: trecho no ar de ${cut.takeoff.toFixed(2)} s a ${cut.landing.toFixed(2)} s`);
    else console.log(`  ${clipName}: quadril não sai do chão; clipe mantido inteiro`);
  }
  capFps(doc, 30);
  await doc.transform(resample({ tolerance: 2e-4 }), dedup(), prune({ keepLeaves: true }));
  dropOrphans(doc);
  await doc.transform(meshopt({ encoder: MeshoptEncoder, level: 'medium', quantizationVolume: 'mesh' }));
  dropOrphans(doc);

  const outLib = join(OUT_GLB_LIB, destName + '.glb');
  const outAsset = join(OUT_ASSETS, destName + '.glb');
  await io.write(outLib, doc);
  await io.write(outAsset, doc);
  console.log(`✓ ${clipName.padEnd(12)} -> ${destName}.glb (${(statSync(outAsset).size / 1024).toFixed(1)} KB, duração: ${dur(anim).toFixed(2)}s)`);
  return true;
}

const LIST = [
  ['Correr_Sprint.fbx', 'anim_run', 'run'],
  ['Ocioso_Idle.fbx', 'anim_idle', 'idle'],
  ['Guarda_Combate_Idle.fbx', 'anim_guard', 'guard'],
  ['Golpe_Corte_Slash.fbx', 'anim_slash', 'slash'],
  ['Estocada_Lanca_Thrust.fbx', 'anim_thrust', 'thrust'],
  ['Socos_Manoplas_PunchCombo.fbx', 'anim_punch', 'punch'],
  ['Impacto_Pesado_Martelo_Smash.fbx', 'anim_smash', 'smash'],
  ['Disparo_Arco_ShootingArrow.fbx', 'anim_bow', 'bow'],
  ['Magia_Conjuracao_Cast1H.fbx', 'anim_cast', 'cast'],
  ['Magia_Area_Spell2H.fbx', 'anim_spell_area', 'spell_area'],
  ['Esquiva_Rolamento_Roll.fbx', 'anim_roll', 'roll'],
  ['Derrubado_Queda_Death.fbx', 'anim_down', 'down'],
  ['Metamorfose_Rugido_Roar.fbx', 'anim_roar', 'roar'],
  ['NPC_Oficio_Trabalho_Craft.fbx', 'anim_craft', 'craft'],
];

// Clipes opcionais: pulo, salto impulsionado e agachar. Baixados do Mixamo em FBX Binary, Without
// Skin, 30 fps, com os nomes abaixo, na pasta "modelos 3d animados/animacoes". Os que existirem
// entram em `src/engine/extra-clips.js`; os que faltarem são pulados sem quebrar o build.
const EXTRA = [
  ['Pulo_Jump.fbx', 'anim_jump', 'jump', { optional: true, air: true }],
  ['Salto_Impulsionado_RunningJump.fbx', 'anim_jump_boost', 'jumpBoost', { optional: true, air: true }],
  ['Agachado_Parado_CrouchIdle.fbx', 'anim_crouch_idle', 'crouchIdle', { optional: true }],
  ['Agachado_Andando_CrouchWalk.fbx', 'anim_crouch_walk', 'crouchWalk', { optional: true }],
  // mira com o arco: puxar, segurar e soltar a flecha
  ['Mira_Arco_ShootingArrow.fbx', 'anim_bow_aim', 'bowAim', { optional: true }],
];

function writeExtraManifest() {
  const present = EXTRA.filter(([, dest]) => existsSync(join(OUT_ASSETS, dest + '.glb')));
  const lines = [
    '// Gerado por tools/process-animations.mjs — não editar à mão.',
    '// Clipes opcionais (pulo, salto impulsionado, agachado parado e andando, mira com o arco) que existem em assets/.',
    '// Enquanto os FBX não estão na pasta de animações, fica vazio e as mecânicas rodam sem essas poses.',
    ...present.map(([, dest, name]) => `import ${name} from '../../assets/${dest}.glb';`),
    `export default { ${present.map(([, , name]) => name).join(', ')} };`,
    '',
  ];
  writeFileSync(join(HERE, '../src/engine/extra-clips.js'), lines.join('\n'));
  console.log(`extra-clips.js: ${present.length ? present.map(([, , n]) => n).join(', ') : 'nenhum clipe opcional'}`);
}

// `--extra`: só os clipes opcionais (não refaz os demais)
const onlyExtra = process.argv.includes('--extra');
console.log('Iniciando processamento das animações 3D...');
for (const [src, dest, name, opts] of [...(onlyExtra ? [] : LIST), ...EXTRA]) {
  try {
    await convertClip(src, dest, name, opts);
  } catch (err) {
    console.error(`Erro ao converter ${src}:`, err.message);
  }
}
writeExtraManifest();
console.log('Processamento concluído com sucesso!');
