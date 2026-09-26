// Processa animações Mixamo FBX em GLBs otimizados.
//
//   node tools/process-animations.mjs            biblioteca principal + clipes opcionais (gera src/engine/extra-clips.js)
//   node tools/process-animations.mjs --extra    só os clipes opcionais (gera src/engine/extra-clips.js)
//   node tools/process-animations.mjs --combat   só o lote COMBAT_2026_09_26 (gera src/engine/combat-clips.js)
//   ... --dry-run                                converte e valida sem escrever em assets/, glb/ nem src/
//
// Os modos são exclusivos. Qualquer falha termina com código diferente de zero; no modo --combat nada
// é escrito enquanto as 20 conversões não passarem todas na validação, então um GLB antigo nunca fica
// no lugar de um que falhou.
import { mkdirSync, statSync, rmSync, existsSync, writeFileSync, copyFileSync, readFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { NodeIO } from '@gltf-transform/core';
import { ALL_EXTENSIONS } from '@gltf-transform/extensions';
import { resample, prune, dedup, meshopt } from '@gltf-transform/functions';
import { MeshoptEncoder, MeshoptDecoder } from 'meshoptimizer';
import {
  FBX2GLTF, fbx, dur, dropEmptyAnimations, removeMeshes, stripToSkeleton, dropStaticChannels,
  inPlace, lockHipsXZ, motionWindow, capFps, dropOrphans, airborne, validateClipDoc, clipFingerprint, sha256, sha256File, hipsRestY,
} from './anim-pipeline.mjs';
import { COMBAT_2026_09_26, COMBAT_BATCH, COMBAT_DIR } from './combat-manifest.mjs';

const HERE = dirname(fileURLToPath(import.meta.url));
// ANIM_SRC_DIR troca a pasta das fontes (usado pelos testes, sempre junto de --dry-run)
const SRC_ANIM = process.env.ANIM_SRC_DIR ? resolve(process.env.ANIM_SRC_DIR) : join(HERE, '../../modelos 3d animados/animacoes');
const TMP = join(tmpdir(), 'rpg-anims-tmp');

// ---------------------------------------------------------------- argumentos
const KNOWN = new Set(['--extra', '--combat', '--dry-run']);
const args = process.argv.slice(2);
const unknown = args.filter((a) => !KNOWN.has(a));
if (unknown.length) { console.error(`process-animations: opção desconhecida: ${unknown.join(' ')}`); process.exit(2); }
const onlyExtra = args.includes('--extra'), combat = args.includes('--combat'), dryRun = args.includes('--dry-run');
if (onlyExtra && combat) { console.error('process-animations: --extra e --combat são exclusivos'); process.exit(2); }
if (process.env.ANIM_SRC_DIR && !dryRun) { console.error('process-animations: ANIM_SRC_DIR só vale com --dry-run'); process.exit(2); }

const OUT_GLB_LIB = dryRun ? join(TMP, 'dry-glb') : join(HERE, '../../modelos 3d animados/animacoes/glb');
const OUT_ASSETS = dryRun ? join(TMP, 'dry-assets') : join(HERE, '../assets');
const OUT_SRC = dryRun ? join(TMP, 'dry-src') : join(HERE, '../src/engine');
if (!existsSync(FBX2GLTF)) { console.error(`process-animations: FBX2glTF não encontrado em ${FBX2GLTF} (rode: cd tools && npm install)`); process.exit(1); }
for (const d of [OUT_GLB_LIB, OUT_ASSETS, OUT_SRC, TMP]) mkdirSync(d, { recursive: true });

await MeshoptEncoder.ready;
await MeshoptDecoder.ready;
const io = new NodeIO().registerExtensions(ALL_EXTENSIONS).registerDependencies({
  'meshopt.encoder': MeshoptEncoder,
  'meshopt.decoder': MeshoptDecoder
});

async function optimize(doc) {
  capFps(doc, 30);
  await doc.transform(resample({ tolerance: 2e-4 }), dedup(), prune({ keepLeaves: true }));
  dropOrphans(doc);
  await doc.transform(meshopt({ encoder: MeshoptEncoder, level: 'medium', quantizationVolume: 'mesh' }));
  dropOrphans(doc);
}

// ---------------------------------------------------------------- biblioteca e opcionais
async function convertClip(srcFile, destName, clipName, opts = {}) {
  const fullSrc = join(SRC_ANIM, srcFile);
  if (!existsSync(fullSrc)) {
    if (opts.optional) { console.log(`- ${clipName.padEnd(12)} (sem ${srcFile}: pulado)`); return false; }
    throw new Error(`fonte ausente: ${srcFile}`);
  }
  const tmpGlb = fbx(fullSrc, TMP, destName);
  const doc = await io.read(tmpGlb);
  dropEmptyAnimations(doc);
  removeMeshes(doc);
  const anims = doc.getRoot().listAnimations();
  if (!anims.length) throw new Error(`sem animações em ${srcFile}`);
  const anim = anims[0];
  anim.setName(clipName);
  dropStaticChannels(doc, { keepTranslation: /Hips$/ });
  inPlace(anim, /Hips$/);
  if (opts.air) {
    const cut = airborne(doc, anim);
    if (cut) console.log(`  ${clipName}: trecho no ar de ${cut.takeoff.toFixed(2)} s a ${cut.landing.toFixed(2)} s`);
    else console.log(`  ${clipName}: quadril não sai do chão; clipe mantido inteiro`);
  }
  await optimize(doc);

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
  writeFileSync(join(OUT_SRC, 'extra-clips.js'), lines.join('\n'));
  console.log(`extra-clips.js: ${present.length ? present.map(([, , n]) => n).join(', ') : 'nenhum clipe opcional'}`);
}

async function runLibrary() {
  const failed = [];
  console.log('Iniciando processamento das animações 3D...');
  for (const [src, dest, name, opts] of [...(onlyExtra ? [] : LIST), ...EXTRA]) {
    try {
      await convertClip(src, dest, name, opts);
    } catch (err) {
      console.error(`✗ ${name}: ${err.message}`);
      failed.push(name);
    }
  }
  writeExtraManifest();
  if (failed.length) { console.error(`Falharam ${failed.length}: ${failed.join(', ')}`); process.exit(1); }
  console.log('Processamento concluído com sucesso!');
}

// ---------------------------------------------------------------- lote de combate
// Registro do runtime gerado do manifesto (uma entrada por clipe; `refY` sai do GLB no carregamento).
// `motion`: janela [início, fim] do deslocamento do quadril na fonte, em segundos (ver motionWindow).
function combatRegistry(list, motionOf) {
  const lines = [
    `// Gerado por tools/process-animations.mjs --combat a partir de tools/combat-manifest.mjs — não editar à mão.`,
    `// Lote ${COMBAT_BATCH}: ${list.length} clipes Mixamo de combate, obrigatórios no build.`,
    ...list.map((e) => `import ${e.key} from '../../assets/${e.glb}';`),
    '',
    `export const COMBAT_BATCH = '${COMBAT_BATCH}';`,
    'export default [',
    ...list.map((e) => `  { key: '${e.key}', bin: ${e.key}, glb: '${e.glb}', source: '${e.source}', contactMode: '${e.contactMode}', locomotionMode: '${e.locomotionMode}', motion: ${JSON.stringify(motionOf.get(e.key) || null)} },`),
    '];',
    '',
  ];
  return lines.join('\n');
}

function checkManifest(list) {
  const problems = [];
  if (list.length !== 20) problems.push(`manifesto com ${list.length} entradas (esperado 20)`);
  for (const field of ['source', 'key', 'glb']) {
    const seen = new Set();
    for (const e of list) { if (seen.has(e[field])) problems.push(`${field} repetido: ${e[field]}`); seen.add(e[field]); }
  }
  for (const e of list) {
    if (!/^[a-z][A-Za-z]+$/.test(e.key)) problems.push(`chave inválida: ${e.key}`);
    if (!/^anim_[a-z_]+\.glb$/.test(e.glb)) problems.push(`nome de GLB inválido: ${e.glb}`);
    if (!['feet', 'body'].includes(e.contactMode)) problems.push(`${e.key}: contactMode ${e.contactMode}`);
    if (!['cycle', 'pose', 'oneshot'].includes(e.locomotionMode)) problems.push(`${e.key}: locomotionMode ${e.locomotionMode}`);
  }
  return problems;
}

async function runCombat() {
  const list = COMBAT_2026_09_26;
  const dir = join(SRC_ANIM, COMBAT_DIR);
  console.log(`Lote ${COMBAT_BATCH}: ${list.length} clipes de ${dir}${dryRun ? ' (simulação)' : ''}`);
  const problems = checkManifest(list);
  // fontes: todas presentes e idênticas às recebidas, antes de converter qualquer uma
  for (const e of list) {
    const f = join(dir, e.source);
    if (!existsSync(f)) problems.push(`fonte ausente: ${e.source}`);
    else if (sha256File(f) !== e.sha256) problems.push(`fonte alterada (sha256 diferente): ${e.source}`);
  }
  if (problems.length) {
    for (const p of problems) console.error(`✗ ${p}`);
    console.error(`Lote recusado: ${problems.length} problema(s). Nada foi escrito.`);
    process.exit(1);
  }

  const work = join(TMP, 'combat');
  rmSync(work, { recursive: true, force: true });
  mkdirSync(join(work, 'out'), { recursive: true });
  const failures = [], done = [];
  for (const e of list) {
    try {
      const raw = fbx(join(dir, e.source), work, e.key, { framerate: 'bake30' });
      const doc = await io.read(raw);
      dropEmptyAnimations(doc);
      const anims = doc.getRoot().listAnimations();
      if (anims.length !== 1) throw new Error(`${anims.length} clipes não vazios no FBX (esperado 1)`);
      stripToSkeleton(doc);
      const anim = anims[0];
      anim.setName(e.key);
      dropStaticChannels(doc, { keepTranslation: /Hips$/ });
      const motion = motionWindow(anim);
      lockHipsXZ(anim);
      await optimize(doc);
      const out = join(work, 'out', e.glb);
      await io.write(out, doc);
      done.push({ e, out, motion });
    } catch (err) {
      failures.push(`${e.key} (${e.source}): ${err.message}`);
    }
  }

  // validação do que foi escrito em disco, relido do zero
  const byHash = new Map(), byClip = new Map(), report = [];
  const motionOf = new Map(done.map((d) => [d.e.key, d.motion]));
  for (const { e, out } of done) {
    if (!existsSync(out) || statSync(out).size === 0) { failures.push(`${e.key}: GLB vazio`); continue; }
    const doc = await io.read(out);
    const errs = validateClipDoc(doc, e.key);
    if (errs.length) { failures.push(`${e.key}: ${errs.join('; ')}`); continue; }
    const fileHash = sha256(readFileSync(out)), clipHash = clipFingerprint(doc.getRoot().listAnimations()[0]);
    if (byHash.has(fileHash)) failures.push(`${e.key}: GLB idêntico ao de ${byHash.get(fileHash)}`);
    if (byClip.has(clipHash)) failures.push(`${e.key}: clipe idêntico ao de ${byClip.get(clipHash)}`);
    byHash.set(fileHash, e.key); byClip.set(clipHash, e.key);
    const a = doc.getRoot().listAnimations()[0];
    report.push({ e, out, kb: statSync(out).size / 1024, T: dur(a), channels: a.listChannels().length, refY: hipsRestY(doc) });
  }
  if (failures.length) {
    for (const f of failures) console.error(`✗ ${f}`);
    console.error(`Lote recusado: ${failures.length} falha(s). Nenhum GLB nem registro foi escrito.`);
    process.exit(1);
  }

  for (const { e, out, kb, T, channels, refY } of report) {
    copyFileSync(out, join(OUT_ASSETS, e.glb));
    copyFileSync(out, join(OUT_GLB_LIB, e.glb));
    console.log(`✓ ${e.key.padEnd(17)} -> ${e.glb.padEnd(28)} ${kb.toFixed(1).padStart(6)} KB  ${T.toFixed(2)} s  ${String(channels).padStart(3)} canais  refY ${refY.toFixed(3)}`);
  }
  writeFileSync(join(OUT_SRC, 'combat-clips.js'), combatRegistry(list, motionOf));
  console.log(`combat-clips.js: ${list.length} entradas`);
  console.log(`Resumo: ${list.length} fontes, ${report.length} saídas, 0 falhas, 0 duplicatas, 0 malha/pele/material, nomes = chaves do manifesto, quadril sem X/Z em todas as amostras.`);
}

if (combat) await runCombat();
else await runLibrary();
