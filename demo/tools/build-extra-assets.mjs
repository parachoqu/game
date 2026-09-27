// Prepara os assets extras da demo (pontes, construções, marcos, a criatura e as armas) a partir dos
// originais em `modelos 3d animados/extras-2026-09-26/`.
//
//   node tools/build-extra-assets.mjs            → demo/assets/extra-props/ + src/engine/extra-assets.js
//   node tools/build-extra-assets.mjs poco ...   → só os ids indicados (o registro é refeito com todos)
//
// Os originais são lidos e nunca alterados. Cada um passa por: extração num temporário do sistema;
// conversão (FBX2glTF para FBX; Blender em segundo plano para .blend; o leitor USD do three.js para
// USDZ); transformação assada, metros, Y para cima, frente para +Z, base na origem; redução de malha
// (meshoptimizer); assentamento (plano de contato da base, base aplanada e normais refeitas, pegada
// da base no manifesto); materiais opacos com uma face e pedra fosca sem metal; recortes ladrilháveis
// para o tabuleiro das pontes; texturas em WebP no tamanho do jogo; compressão meshopt. As armas perdem a paleta
// (que não veio no pacote) e ganham zonas nomeadas (lâmina, guarda, cabo…) que o jogo pinta com as
// cores e superfícies das armas atuais.
//
// Qualquer fonte ausente ou conversão que falhe encerra com código ≠ 0, sem escrever o registro.
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, readdirSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { basename, dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inflateRawSync } from 'node:zlib';
import sharp from 'sharp';
import { Document, NodeIO, TextureInfo } from '@gltf-transform/core';
import { ALL_EXTENSIONS } from '@gltf-transform/extensions';
import { dedup, prune, weld, simplify, simplifyPrimitive, textureCompress, meshopt, join as joinPrims, mergeDocuments, unpartition, unweld, normals } from '@gltf-transform/functions';
import { MeshoptEncoder, MeshoptDecoder, MeshoptSimplifier } from 'meshoptimizer';

const TOOLS = dirname(fileURLToPath(import.meta.url));
const DEMO = dirname(TOOLS);
const ROOT = dirname(DEMO);
const SRC = join(ROOT, 'modelos 3d animados', 'extras-2026-09-26');
const OUT = join(DEMO, 'assets', 'extra-props');
const REGISTRY = join(DEMO, 'src', 'engine', 'extra-assets.js');
const TMP = join(tmpdir(), 'projeto-game-extras');
const FBX2GLTF = join(TOOLS, 'node_modules/fbx2gltf/bin',
  process.platform === 'win32' ? 'Windows_NT' : process.platform === 'darwin' ? 'Darwin' : 'Linux',
  process.platform === 'win32' ? 'FBX2glTF.exe' : 'FBX2glTF');
const BLENDER = process.env.BLENDER_BIN || 'blender';

const fail = (m) => { console.error(`build-extra-assets: ${m}`); process.exit(1); };
const sha256 = (b) => createHash('sha256').update(b).digest('hex');
const round = (v, n = 3) => Math.round(v * 10 ** n) / 10 ** n;
const KB = (n) => `${(n / 1024).toFixed(0)} KB`;

// ---------------------------------------------------------------- catálogo
const USER = { author: null, license: 'gratuito/licença pública conforme declaração do usuário em 26/09/2026' };
const PIGCRAFT = { author: 'Pigcraft (https://sketchfab.com/s8819296)', license: 'CC-BY-4.0 (http://creativecommons.org/licenses/by/4.0/)' };

// `stone`: pedra fosca e sem metal; `cleanBase`: base aplanada no plano de contato; `support`: como o
// jogo assenta o modelo ('flat' base plana, 'point' tronco, 'bridge' e 'deck' datum funcional)
const BUILDING = { stone: true, cleanBase: true, support: 'flat' };
const SET32 = { file: 'fantasy_architecture_set_32.zip', blend: 'fantasy architecture set 32.blend', tris: 9000, tex: 1024, ...BUILDING, ...USER };
const KENNEY = {
  file: 'kenney_retro-fantasy-kit.zip', author: 'Kenney (https://kenney.nl)', license: 'CC0-1.0 (https://creativecommons.org/publicdomain/zero/1.0/)',
  url: 'https://kenney.nl/assets', tex: 64, support: 'flat',
};
const KENNEY_DIR = 'Models/GLB format/';

// Montagens do kit Kenney: [peça, [x, y, z], giro em Y, [escala x, y, z], materiais mantidos].
// A frente de cada montagem fica em +Z, como nos outros modelos.
const Q = Math.PI / 2;
const BANCA = [
  // seis postes, balcão de tábuas na frente e telhado de duas águas ao longo de x (só as placas)
  ...[-1.45, 0, 1.45].flatMap((x) => [-0.95, 0.95].map((z) => ['structure-pole', [x, 0, z], 0, [1.6, 1.8, 1.6]])),
  ...[-1, 0, 1].map((x) => ['wood-floor-half', [x, 0.8, 0.45]]),
  ...[-1, 0, 1].map((x) => ['fence-wood', [x, 0.05, 0.92], 0, [1, 2.5, 1]]),
  ['detail-crate', [-1, 0.93, 0.72], 0.3], ['detail-barrel', [0.2, 0.93, 0.7]], ['detail-crate-small', [0.95, 0.93, 0.75], 0.6],
  ['barrels', [0.85, 0, -0.45]], ['detail-crate-ropes', [-0.9, 0, -0.5], 0.2], ['detail-crate', [-0.45, 0, -0.55], 0.9],
  ...[-1, 0, 1].map((x) => ['roof', [x, 1.8, 0], Q, [2.2, 0.6, 1.04], ['roof', 'planks']]),
];
const DOCA_POSTE = 2.3, DOCA_PISO = DOCA_POSTE + 0.13;
const DOCA = [
  // piso de 5 × 3 ladrilhos sobre estacas, guarda-corpo nos dois lados compridos e na ponta
  ...[-2, -1, 0, 1, 2].flatMap((x) => [-1, 0, 1].map((z) => ['wood-floor', [x, DOCA_POSTE, z]])),
  ...[-2.4, -1.2, 0, 1.2, 2.4].flatMap((x) => [-1.4, 1.4].map((z) => ['structure-pole', [x, 0, z], 0, [2.4, DOCA_POSTE, 2.4]])),
  ...[-1.5, -0.5, 0.5, 1.5].flatMap((x) => [-1.35, 1.35].map((z) => ['overhang-fence', [x, DOCA_PISO, z]])),
  ...[-0.7, 0.7].map((z) => ['overhang-fence', [2.35, DOCA_PISO, z], Q, [1.3, 1, 1]]),
  ['detail-crate-ropes', [-1.9, DOCA_PISO, 0.6], 0.4], ['barrels', [-1.8, DOCA_PISO, -0.7], 1.2],
];
const MURO_A = [
  // pano de muro com alturas desiguais, ameias no trecho mais alto, coluna quebrada e pedras soltas
  ['wall', [-2, 0, 0]], ['wall', [-2, 1, 0]], ['wall-half', [-2.5, 2, 0]],
  ['wall-fortified', [-1, 0, 0]], ['wall-fortified', [-1, 1, 0]], ['wall-fortified', [-1, 2, 0]],
  ['battlement', [-1, 3, 0]], ['battlement', [-1, 3, 0], Math.PI],
  ['wall', [0, 0, 0]], ['wall-fortified-window', [0, 1, 0]],
  ['wall', [1, 0, 0]], ['wall-half', [1, 1, 0]],
  ['wall-low', [2, 0, 0]], ['column-damaged', [2.62, 0, 0.15], 0, [1.6, 1.3, 1.6]],
  ...[[-1.6, 0.55, 0.3], [-0.3, -0.6, 2], [0.7, 0.55, 1], [1.8, -0.55, 2.6], [2.25, 0.5, 0.5], [0.3, 0.6, 4]].map(([x, z, r]) => ['bricks', [x, 0, z], r, [1.5, 1.5, 1.5]]),
];
const MURO_B = [
  // canto de muro: pano ao fundo, um trecho de lado, duas colunas de pé e entulho
  ['wall', [-1.5, 0, -0.8]], ['wall', [-1.5, 1, -0.8]], ['wall-fortified-half', [-2, 2, -0.8]],
  ['wall-fortified', [-0.5, 0, -0.8]], ['wall-fortified-window', [-0.5, 1, -0.8]],
  ['wall', [0.5, 0, -0.8]], ['wall-half', [0.5, 1, -0.8]],
  ['wall-low', [1.5, 0, -0.8]],
  ['wall', [-1.5, 0, 0.2], Q], ['wall-half', [-1.5, 1, 0.2], Q],
  ['column-damaged', [0.55, 0, 0.45], 0, [1.7, 2, 1.7]], ['column-damaged', [1.55, 0, 0.3], 0.8, [1.7, 1.3, 1.7]],
  ...[[-0.6, 0.55, 0.4], [0.3, 0.7, 2.2], [1.2, -0.1, 1], [1.8, 0.65, 3], [-1, 0.75, 5]].map(([x, z, r]) => ['bricks', [x, 0, z], r, [1.5, 1.5, 1.5]]),
];

// texturas por material: qual arquivo da pasta alimenta cada canal
const CH = {
  base: /(albedo|albe\b|albe\.|basecolor|diffuse|_d\.|_do\.)/i, normal: /(normal|_norm\.)/i,
  rough: /(roughness|_roug\.)/i, metal: /(metalness|metallic|_meta\.)/i, ao: /_ao\./i, opacity: /opacity/i,
};

// id, arquivo de origem e como converter. `height` fixa a altura em metros; sem ela a escala é a do
// arquivo e o jogo encaixa o modelo na caixa do volume que ele substitui.
const ASSETS = [
  { id: 'ponte_pedra', title: 'Stone Bridge', file: 'stone-bridge.zip', fbx: 'source/StoneBridge.fbx', texDir: 'textures/',
    texFor: (mat, f) => f.startsWith(`StoneBridge_${mat}_`), tris: 22000, tex: 1024, deck: 'Stone_Road_Material', stone: true, support: 'bridge',
    // fiadas de cantaria (faixa de cima do atlas das lajes) e o piso da própria pista (metade de cima
    // do atlas da pista, sem as bordas escuras do recorte)
    roadway: { cantaria: { from: 'Slab_Stone_Material', crop: [0.01, 0.01, 0.94, 0.2] }, calcamento: { from: 'Stone_Road_Material', crop: [0.06, 0.05, 0.94, 0.55] } },
    ...USER },
  { id: 'ponte_quebrada', title: 'Ruined Stone Bridge, Broken Arch', file: 'ruined_stone_bridge_broken_arch.glb', blender: true, tris: 15000, tex: 1024, height: 9, ...BUILDING,
    url: 'https://sketchfab.com/3d-models/ruined-stone-bridge-broken-arch-57fa861b90ff4793bccf7930a43c8bd0', ...PIGCRAFT },
  { id: 'poco', title: 'Medieval Stone Well - Game Prop', file: 'medieval_stone_well_-_game_prop.glb', blender: true, tris: 6000, tex: 1024, height: 2.6, ...BUILDING,
    url: 'https://sketchfab.com/3d-models/medieval-stone-well-game-prop-a0ca279889b84afb9f24b88bff9c6860', ...PIGCRAFT },
  { id: 'casa_palha', title: 'Medieval Straw House', file: 'medieval-straw-house.zip', usdz: 'source/haus.usdz', tris: 13000, tex: 1024, cleanBase: true, support: 'flat', ...USER },
  { id: 'ferraria', title: 'The Blacksmiths', file: 'the-blacksmiths.zip', fbx: 'source/Blacksmith.fbx', texDir: 'textures/',
    texFor: (mat, f) => f.startsWith(`${mat}_`) || (mat === 'GroundMat' && /^Ground_opacity/i.test(f)), tris: 17000, tex: 1024, cleanBase: true, support: 'flat', ...USER },
  { id: 'torre_a', title: 'Free Fantasy Castle Towers (torre redonda de pedra)', file: 'free_fantasy_castle_towers.zip', blend: 'Free Fantasy Castle Towers.blend', object: 'mesh_0', tris: 9000, tex: 1024, ...BUILDING, ...USER },
  { id: 'torre_b', title: 'Free Fantasy Castle Towers (torre redonda azulada)', file: 'free_fantasy_castle_towers.zip', blend: 'Free Fantasy Castle Towers.blend', object: 'mesh_3', tris: 9000, tex: 1024, ...BUILDING, ...USER },
  { id: 'moinho', title: 'Free Windmills Set (moinho com pás)', file: 'free_windmills_set.blend', object: 'mesh.003', tris: 10000, tex: 1024, height: 9, ...BUILDING, ...USER },
  { id: 'arvore_marco', title: 'HighPoly Tree Model', file: 'highpoly_tree_model.zip', fbx: 'HighPoly Tree Model/Model/SM_HP_Tree.FBX', texDir: 'HighPoly Tree Model/Textures/',
    texFor: (mat, f) => (/Trunk/i.test(mat) ? /Trunk/i.test(f) : /Leaf/i.test(f)), lods: [1, 0.22], tex: 1024, height: 16.5, support: 'point',
    ...USER, author: 'Next Spring (https://www.fab.com/sellers/Next%20Spring)' },
  { id: 'cao_vazio', title: 'Void Hound', file: 'void_hound.glb', blender: true, tris: 15000, tex: 1024, keepSides: true, ...USER },
  // construções do Vale: as três casas do conjunto 32 e duas peças que sobraram do conjunto de moinhos
  { id: 'casa_s32_a', title: 'Fantasy Architecture Set 32 (casa de telhado vermelho)', object: 'mesh', ...SET32 },
  { id: 'casa_s32_b', title: 'Fantasy Architecture Set 32 (casa alta de telhado azul)', object: 'mesh.001', ...SET32 },
  { id: 'casa_s32_c', title: 'Fantasy Architecture Set 32 (casa-galpão azul)', object: 'mesh.002', ...SET32 },
  { id: 'casa_pedra', title: 'Free Windmills Set (casa de pedra, telhado vermelho)', file: 'free_windmills_set.blend', object: 'mesh.002', tris: 9000, tex: 1024, ...BUILDING, ...USER },
  { id: 'armazem', title: 'Free Windmills Set (prédio de pedra e madeira)', file: 'free_windmills_set.blend', object: 'mesh', tris: 9000, tex: 1024, ...BUILDING, ...USER },
  { id: 'torre_escombros', title: 'Free Fantasy Castle Towers (torre com escombros)', file: 'free_fantasy_castle_towers.zip', blend: 'Free Fantasy Castle Towers.blend', object: 'mesh_1', tris: 9000, tex: 1024, ...BUILDING, ...USER },
  // montagens com peças do kit Kenney (1 unidade = 1 ladrilho do kit; o jogo encaixa na caixa do volume)
  { id: 'banca', title: 'Retro Fantasy Kit: banca de mercado', kit: BANCA, tint: 'roof', doubleSided: ['telhado'], ...KENNEY },
  { id: 'doca', title: 'Retro Fantasy Kit: cais de madeira', kit: DOCA, deckTop: DOCA_PISO, ...KENNEY, support: 'deck' },
  { id: 'muro_a', title: 'Retro Fantasy Kit: muro fortificado em ruína', kit: MURO_A, ...KENNEY },
  { id: 'muro_b', title: 'Retro Fantasy Kit: canto de muro em ruína', kit: MURO_B, ...KENNEY },
];

// Armas PurePoly: modelo de origem, eixos e zonas. `t` vai do pé (0) à ponta (1) do eixo longo; `u` é
// a posição no eixo de largura, de 0 a 1. A primeira regra que casa dá o nome da zona.
const W = 'PP_FreeFantasyRPGWeapons_FBX_files/';
const WEAPONS = [
  { id: 'arma_espada', src: 'PP_Theme_11_Sword_One-Handed_003', zones: [['pomo', (t) => t < 0.035], ['cabo', (t) => t < 0.21], ['guarda', (t) => t < 0.36], ['lamina', () => true]] },
  { id: 'arma_machado', src: 'PP_Theme_11_Axe_One_Handed_002', zones: [['pomo', (t) => t < 0.04], ['cabo', (t) => t < 0.5], ['guarda', (t) => t < 0.57], ['lamina', () => true]] },
  { id: 'arma_martelo', src: 'PP_Theme_03_Hammer_001', zones: [['lamina', (t) => t > 0.78], ['cabo', () => true]] },
  { id: 'arma_lanca', src: 'PP_Theme_07_Spear_002', zones: [['lamina', (t) => t > 0.87], ['pomo', (t) => t < 0.03], ['cabo', () => true]] },
  { id: 'arma_adaga', src: 'PP_Theme_10_Dagger_002', zones: [['pomo', (t) => t < 0.05], ['cabo', (t) => t < 0.34], ['guarda', (t) => t < 0.42], ['lamina', () => true]] },
  { id: 'arma_foice', src: 'PP_Theme_04_Scythe_001', zones: [['lamina', (t) => t > 0.8], ['pomo', (t) => t < 0.04], ['cabo', () => true]] },
  { id: 'arma_arco', src: 'PP_Theme_02_Bow_001', zones: [['corda', (t, u) => u > 0.9], ['cabo', (t, u) => Math.abs(t - 0.5) < 0.07 && u < 0.45], ['arco', () => true]] },
  { id: 'arma_manopla', src: 'PP_Theme_02_Fist_Weapon_001', long: 'x', zones: [['lamina', (t) => t > 0.5], ['guarda', () => true]] },
  { id: 'arma_tomo', src: 'PP_Theme_04_Spellbook_003', zones: [['ornamento', (t, u, w) => w > 0.85 || w < 0.12], ['capa', () => true]] },
  { id: 'arma_cajado', src: 'PP_Theme_07_Scepter_001', zones: [['ornamento', (t) => t > 0.79], ['pomo', (t) => t < 0.03], ['cabo', () => true]] },
];
const WEAPON_TRIS = 2500;

// ---------------------------------------------------------------- utilidades
function readZip(file, wanted) {
  const buf = readFileSync(file);
  let e = buf.length - 22;
  while (e >= 0 && buf.readUInt32LE(e) !== 0x06054b50) e--;
  if (e < 0) fail(`${basename(file)}: não é um ZIP`);
  const out = new Map();
  const n = buf.readUInt16LE(e + 10);
  let p = buf.readUInt32LE(e + 16);
  for (let i = 0; i < n; i++) {
    const method = buf.readUInt16LE(p + 10), size = buf.readUInt32LE(p + 20);
    const nl = buf.readUInt16LE(p + 28), xl = buf.readUInt16LE(p + 30), cl = buf.readUInt16LE(p + 32);
    const off = buf.readUInt32LE(p + 42);
    const name = buf.toString('utf8', p + 46, p + 46 + nl);
    p += 46 + nl + xl + cl;
    if (name.endsWith('/') || !wanted(name)) continue;
    const start = off + 30 + buf.readUInt16LE(off + 26) + buf.readUInt16LE(off + 28);
    const data = buf.subarray(start, start + size);
    out.set(name, method === 8 ? inflateRawSync(data) : Buffer.from(data));
  }
  return out;
}

function extract(spec, wanted) {
  const dir = join(TMP, spec.id);
  rmSync(dir, { recursive: true, force: true });
  mkdirSync(dir, { recursive: true });
  for (const [name, data] of readZip(join(SRC, spec.file), wanted)) {
    const to = join(dir, name);
    mkdirSync(dirname(to), { recursive: true });
    writeFileSync(to, data);
  }
  return dir;
}

function fbxToGlb(fbxPath, name) {
  const out = join(TMP, name);
  rmSync(out + '.glb', { force: true });
  execFileSync(FBX2GLTF, ['--binary', '--input', fbxPath, '--output', out], { stdio: 'ignore' });
  if (!existsSync(out + '.glb')) throw new Error(`FBX2glTF não gerou ${name}.glb`);
  return out + '.glb';
}

// Blender em segundo plano: abre o .blend (ou importa o GLB), junta, reduz por colapso e exporta.
function blender(name, { blend = null, glb = null, object = '*', tris = 0 }) {
  const out = join(TMP, `${name}.blender.glb`);
  rmSync(out, { force: true });
  const args = ['-b', '--factory-startup', ...(blend ? [blend] : []), '--python', join(TOOLS, 'extra-assets-blender.py'), '--', glb || '-', object, String(tris), out];
  const log = execFileSync(BLENDER, args, { stdio: ['ignore', 'pipe', 'ignore'], timeout: 1800000, maxBuffer: 64 << 20 }).toString();
  if (!existsSync(out)) throw new Error(`Blender não exportou ${object}`);
  const m = /#### exportado .*: (\d+) → (\d+) triângulos/.exec(log);
  return { path: out, before: m ? +m[1] : null };
}

const mul = (m, v) => [m[0] * v[0] + m[4] * v[1] + m[8] * v[2] + m[12], m[1] * v[0] + m[5] * v[1] + m[9] * v[2] + m[13], m[2] * v[0] + m[6] * v[1] + m[10] * v[2] + m[14]];
const mulDir = (m, v) => { const r = [m[0] * v[0] + m[4] * v[1] + m[8] * v[2], m[1] * v[0] + m[5] * v[1] + m[9] * v[2], m[2] * v[0] + m[6] * v[1] + m[10] * v[2]]; const l = Math.hypot(...r) || 1; return r.map((x) => x / l); };

// Assa a transformação de cada nó na geometria (malha própria por nó) e deixa tudo num nó raiz só.
function bakeScene(doc) {
  const root = doc.getRoot();
  const scene = root.getDefaultScene() || root.listScenes()[0];
  const holder = doc.createNode('modelo');
  // malha usada por vários nós: cada nó ganha a sua cópia antes de qualquer transformação (senão a
  // segunda instância clonaria a primeira já transformada e a escala se acumularia)
  const seen = new Set(), pairs = [];
  for (const node of root.listNodes()) {
    const mesh = node.getMesh();
    if (!mesh) continue;
    let own = mesh;
    if (seen.has(mesh)) {
      // `Mesh.clone()` é raso (as primitivas continuam as mesmas); a cópia precisa de primitivas próprias
      own = doc.createMesh(mesh.getName());
      for (const p of mesh.listPrimitives()) own.addPrimitive(p.clone());
    }
    pairs.push([node, own]);
    seen.add(mesh);
  }
  for (const [node, own] of pairs) {
    const m = node.getWorldMatrix();
    const flip = m[0] * (m[5] * m[10] - m[6] * m[9]) - m[4] * (m[1] * m[10] - m[2] * m[9]) + m[8] * (m[1] * m[6] - m[2] * m[5]) < 0;
    for (const prim of own.listPrimitives()) {
      const pos = prim.getAttribute('POSITION').clone(), nor = prim.getAttribute('NORMAL')?.clone();
      const v = [0, 0, 0];
      for (let i = 0; i < pos.getCount(); i++) pos.setElement(i, mul(m, pos.getElement(i, v)));
      if (nor) for (let i = 0; i < nor.getCount(); i++) nor.setElement(i, mulDir(m, nor.getElement(i, v)));
      prim.setAttribute('POSITION', pos);
      if (nor) prim.setAttribute('NORMAL', nor);
      prim.setAttribute('TANGENT', null);
      if (flip && prim.getIndices()) {
        const idx = prim.getIndices().clone(), a = idx.getArray().slice();
        for (let i = 0; i < a.length; i += 3) { const t = a[i + 1]; a[i + 1] = a[i + 2]; a[i + 2] = t; }
        prim.setIndices(idx.setArray(a));
      }
    }
    holder.addChild(doc.createNode(node.getName() || 'parte').setMesh(own));
  }
  for (const s of root.listScenes()) s.dispose();
  for (const n of root.listNodes()) if (n !== holder && !holder.listChildren().includes(n)) n.dispose();
  const s = doc.createScene('cena').addChild(holder);
  root.setDefaultScene(s);
  return holder;
}

// acessores de posição sem repetição: primitivas de um mesmo corpo compartilham o acessor, e mover
// duas vezes o mesmo acessor deslocaria o modelo em dobro
function positions(doc, sem = 'POSITION') {
  const out = new Set();
  for (const mesh of doc.getRoot().listMeshes()) for (const p of mesh.listPrimitives()) if (p.getAttribute(sem)) out.add(p.getAttribute(sem));
  return [...out];
}
function bounds(doc) {
  const mn = [Infinity, Infinity, Infinity], mx = [-Infinity, -Infinity, -Infinity], v = [0, 0, 0];
  for (const a of positions(doc)) for (let i = 0; i < a.getCount(); i++) {
    a.getElement(i, v);
    for (let k = 0; k < 3; k++) { if (v[k] < mn[k]) mn[k] = v[k]; if (v[k] > mx[k]) mx[k] = v[k]; }
  }
  return { mn, mx, size: mx.map((x, k) => x - mn[k]) };
}
// aplica x' = s·(x − c) nas posições (s escalar), sem mexer nas normais
function moveScale(doc, c, s) {
  const v = [0, 0, 0];
  for (const a of positions(doc)) for (let i = 0; i < a.getCount(); i++) {
    a.getElement(i, v);
    a.setElement(i, [(v[0] - c[0]) * s, (v[1] - c[1]) * s, (v[2] - c[2]) * s]);
  }
}
const triCount = (doc) => doc.getRoot().listMeshes().reduce((n, m) => n + m.listPrimitives().reduce((k, p) => k + (p.getIndices() ? p.getIndices().getCount() : p.getAttribute('POSITION').getCount()) / 3, 0), 0);

// Normaliza: base na origem, centro da pegada em x = z = 0 e, com `height`, altura em metros.
function normalize(doc, spec) {
  const b = bounds(doc);
  const s = spec.height ? spec.height / b.size[1] : 1;
  moveScale(doc, [(b.mn[0] + b.mx[0]) / 2, b.mn[1], (b.mn[2] + b.mx[2]) / 2], s);
  return bounds(doc);
}

// ---------------------------------------------------------------- assentamento
// O menor vértice raramente é a base: pedras soltas, pontas de entulho e as faces serrilhadas que os
// geradores deixam por baixo ficam abaixo da fundação visível. O plano de contato é a mediana (por
// área) da altura das faces voltadas para baixo na faixa inferior do modelo; sem faces assim (fundo
// aberto), o 20º percentil dos vértices da faixa. A pegada é o contorno convexo (XZ) do que fica até
// um pouco acima desse plano — a base, sem beirais nem pás.
function trianglesOf(prim) {
  const pos = prim.getAttribute('POSITION'), idx = prim.getIndices()?.getArray();
  const n = idx ? idx.length : pos.getCount();
  return { pos, idx, n };
}
function contactPlane(doc) {
  const b = bounds(doc), h = b.size[1], top = b.mn[1] + 0.12 * h;
  const A = [0, 0, 0], B = [0, 0, 0], C = [0, 0, 0];
  const down = [], ys = [];
  for (const mesh of doc.getRoot().listMeshes()) for (const p of mesh.listPrimitives()) {
    if (/_L1$/.test(mesh.getName())) continue;
    const { pos, idx, n } = trianglesOf(p);
    for (let i = 0; i < n; i += 3) {
      pos.getElement(idx ? idx[i] : i, A); pos.getElement(idx ? idx[i + 1] : i + 1, B); pos.getElement(idx ? idx[i + 2] : i + 2, C);
      const cy = (A[1] + B[1] + C[1]) / 3;
      if (cy > top) continue;
      const ux = B[0] - A[0], uz = B[2] - A[2], wx = C[0] - A[0], wz = C[2] - A[2];
      const ny = (uz * wx - ux * wz) / 2;   // área projetada com sinal (negativa = face para baixo)
      if (ny < 0) down.push([cy, -ny]);
      ys.push(A[1], B[1], C[1]);
    }
  }
  const total = down.reduce((s, [, a]) => s + a, 0);
  if (total > 1e-6 * b.size[0] * b.size[2]) {
    down.sort((p, q) => p[0] - q[0]);
    let acc = 0;
    for (const [y, a] of down) { acc += a; if (acc >= total / 2) return y; }
  }
  ys.sort((p, q) => p - q);
  return ys.length ? ys[Math.floor(0.2 * (ys.length - 1))] : b.mn[1];
}

// Aplana a base: o que desce abaixo do plano sobe até ele (some o serrilhado); as faces que ficam
// deitadas no plano e viradas para baixo (nunca vistas com o modelo assentado) e os triângulos que
// ficaram sem área saem. As normais são refeitas depois.
function flattenBase(doc, y0) {
  for (const a of positions(doc)) for (let i = 0; i < a.getCount(); i++) {
    const v = a.getElement(i, [0, 0, 0]);
    if (v[1] < y0) { v[1] = y0; a.setElement(i, v); }
  }
  const A = [0, 0, 0], B = [0, 0, 0], C = [0, 0, 0], eps = 1e-5;
  let removed = 0;
  for (const mesh of doc.getRoot().listMeshes()) for (const p of mesh.listPrimitives()) {
    const { pos, idx } = trianglesOf(p);
    if (!idx) continue;
    const keep = [];
    for (let i = 0; i < idx.length; i += 3) {
      pos.getElement(idx[i], A); pos.getElement(idx[i + 1], B); pos.getElement(idx[i + 2], C);
      const ux = B[0] - A[0], uy = B[1] - A[1], uz = B[2] - A[2], wx = C[0] - A[0], wy = C[1] - A[1], wz = C[2] - A[2];
      const nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
      const area2 = Math.hypot(nx, ny, nz);
      const flat = A[1] < y0 + eps && B[1] < y0 + eps && C[1] < y0 + eps;
      if (area2 < 1e-10 || (flat && ny <= 0)) { removed++; continue; }
      keep.push(idx[i], idx[i + 1], idx[i + 2]);
    }
    if (keep.length !== idx.length) p.setIndices(p.getIndices().clone().setArray(new Uint32Array(keep)));
  }
  return removed;
}

// Normais refeitas depois de aplanar a base, sem desfazer os índices: cada vértice soma as faces
// (por área) que tocam a mesma posição e cuja normal não se afasta mais de ~60° da normal que ele já
// tinha — costuras de UV continuam lisas e as arestas vivas continuam vivas.
function smoothNormals(doc) {
  const COS = 0.5, A = [0, 0, 0], B = [0, 0, 0], C = [0, 0, 0];
  const done = new Set();
  for (const mesh of doc.getRoot().listMeshes()) for (const p of mesh.listPrimitives()) {
    const pos = p.getAttribute('POSITION'), nor = p.getAttribute('NORMAL'), idx = p.getIndices()?.getArray();
    if (!pos || !idx || !nor || done.has(nor)) continue;
    done.add(nor);
    const n = pos.getCount(), key = new Array(n), faces = new Map(), v = [0, 0, 0];
    for (let i = 0; i < n; i++) { pos.getElement(i, v); key[i] = `${Math.round(v[0] * 1e5)},${Math.round(v[1] * 1e5)},${Math.round(v[2] * 1e5)}`; }
    for (let i = 0; i < idx.length; i += 3) {
      pos.getElement(idx[i], A); pos.getElement(idx[i + 1], B); pos.getElement(idx[i + 2], C);
      const ux = B[0] - A[0], uy = B[1] - A[1], uz = B[2] - A[2], wx = C[0] - A[0], wy = C[1] - A[1], wz = C[2] - A[2];
      const nx = uy * wz - uz * wy, ny = uz * wx - ux * wz, nz = ux * wy - uy * wx;
      for (let k = 0; k < 3; k++) {
        const kk = key[idx[i + k]];
        if (!faces.has(kk)) faces.set(kk, []);
        faces.get(kk).push([nx, ny, nz]);
      }
    }
    const out = nor.clone(), o = [0, 0, 0];
    for (let i = 0; i < n; i++) {
      nor.getElement(i, o);
      let sx = 0, sy = 0, sz = 0;
      for (const [nx, ny, nz] of faces.get(key[i]) || []) {
        const l = Math.hypot(nx, ny, nz) || 1;
        if ((nx * o[0] + ny * o[1] + nz * o[2]) / l < COS) continue;
        sx += nx; sy += ny; sz += nz;
      }
      const l = Math.hypot(sx, sy, sz);
      if (l > 1e-12) out.setElement(i, [sx / l, sy / l, sz / l]);
    }
    for (const q of mesh.listPrimitives()) if (q.getAttribute('NORMAL') === nor) q.setAttribute('NORMAL', out);
  }
}

// contorno convexo (cadeia monótona) e simplificação até `max` pontos, tirando o ponto que menos área perde
function convexHull(pts) {
  const P = [...pts].sort((a, b) => a[0] - b[0] || a[1] - b[1]);
  const cross = (o, a, b) => (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0]);
  const lo = [], hi = [];
  for (const p of P) { while (lo.length >= 2 && cross(lo.at(-2), lo.at(-1), p) <= 0) lo.pop(); lo.push(p); }
  for (const p of P.reverse()) { while (hi.length >= 2 && cross(hi.at(-2), hi.at(-1), p) <= 0) hi.pop(); hi.push(p); }
  return lo.slice(0, -1).concat(hi.slice(0, -1));
}
function simplifyHull(h, max) {
  const H = [...h];
  while (H.length > max) {
    let best = -1, bestA = Infinity;
    for (let i = 0; i < H.length; i++) {
      const a = H[(i + H.length - 1) % H.length], b = H[i], c = H[(i + 1) % H.length];
      const area = Math.abs((b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0]));
      if (area < bestA) { bestA = area; best = i; }
    }
    H.splice(best, 1);
  }
  return H;
}
function footprintAt(doc, y0, band) {
  const pts = [], v = [0, 0, 0];
  for (const mesh of doc.getRoot().listMeshes()) {
    if (/_L1$/.test(mesh.getName())) continue;
    for (const p of mesh.listPrimitives()) {
      const a = p.getAttribute('POSITION');
      for (let i = 0; i < a.getCount(); i++) { a.getElement(i, v); if (v[1] <= y0 + band) pts.push([v[0], v[2]]); }
    }
  }
  return simplifyHull(convexHull(pts), 16).map(([x, z]) => [round(x), round(z)]);
}

// Pedra não é metal: o canal azul (metal) do mapa zera e o verde (aspereza) ganha um piso. Cada
// textura é tratada uma vez, mesmo quando dois materiais a usam.
async function stoneMaterials(doc, floor = 0.62) {
  const done = new Set();
  for (const m of doc.getRoot().listMaterials()) {
    if (m.getAlphaMode() !== 'OPAQUE') continue;
    m.setMetallicFactor(0);
    const t = m.getMetallicRoughnessTexture();
    if (!t) { m.setRoughnessFactor(Math.max(0.85, m.getRoughnessFactor())); continue; }
    m.setRoughnessFactor(1);
    if (done.has(t)) continue;
    done.add(t);
    const { data, info } = await sharp(t.getImage()).ensureAlpha().raw().toBuffer({ resolveWithObject: true });
    const g0 = Math.round(floor * 255);
    for (let i = 0; i < data.length; i += 4) { data[i + 1] = g0 + Math.round(data[i + 1] * (1 - floor)); data[i + 2] = 0; }
    t.setImage(await sharp(data, { raw: { width: info.width, height: info.height, channels: 4 } }).removeAlpha().png().toBuffer()).setMimeType('image/png');
  }
}

// Recortes ladrilháveis das texturas da própria ponte, para o tabuleiro plano que o jogo monta por
// cima do arco (`extra-props.js`). Viram dois materiais presos a uma amostra mínima, que o jogo tira
// da cena ao carregar.
async function roadwayMaterials(doc, spec) {
  const mats = new Map(doc.getRoot().listMaterials().map((m) => [m.getName(), m]));
  const holder = doc.getRoot().getDefaultScene().listChildren()[0];
  const mesh = doc.createMesh('_amostras');
  const buffer = doc.getRoot().listBuffers()[0];
  for (const [name, { from, crop }] of Object.entries(spec.roadway)) {
    const src = mats.get(from);
    if (!src) throw new Error(`${spec.id}: material ${from} ausente para o recorte ${name}`);
    const cut = async (tex) => {
      const img = sharp(tex.getImage()), meta = await img.metadata();
      const [x0, y0, x1, y1] = crop;
      const left = Math.round(x0 * meta.width), top = Math.round(y0 * meta.height);
      return img.extract({ left, top, width: Math.round((x1 - x0) * meta.width), height: Math.round((y1 - y0) * meta.height) }).png().toBuffer();
    };
    const mat = doc.createMaterial(name).setMetallicFactor(0).setRoughnessFactor(0.92).setDoubleSided(false);
    mat.setBaseColorTexture(doc.createTexture(`${name}_cor`).setImage(await cut(src.getBaseColorTexture())).setMimeType('image/png'));
    if (src.getNormalTexture()) mat.setNormalTexture(doc.createTexture(`${name}_normal`).setImage(await cut(src.getNormalTexture())).setMimeType('image/png'));
    for (const info of [mat.getBaseColorTextureInfo(), mat.getNormalTextureInfo()]) {
      info?.setWrapS(TextureInfo.WrapMode.MIRRORED_REPEAT).setWrapT(TextureInfo.WrapMode.MIRRORED_REPEAT);
    }
    const tri = (arr, type) => doc.createAccessor().setType(type).setArray(arr).setBuffer(buffer);
    mesh.addPrimitive(doc.createPrimitive().setMaterial(mat)
      .setAttribute('POSITION', tri(new Float32Array([0, 0.01, 0, 0.001, 0.01, 0, 0, 0.01, 0.001]), 'VEC3'))
      .setAttribute('NORMAL', tri(new Float32Array([0, 1, 0, 0, 1, 0, 0, 1, 0]), 'VEC3'))
      .setAttribute('TEXCOORD_0', tri(new Float32Array([0, 0, 1, 0, 0, 1]), 'VEC2'))
      .setIndices(tri(new Uint32Array([0, 2, 1]), 'SCALAR')));
  }
  holder.addChild(doc.createNode('_amostras').setMesh(mesh));
}

async function reduce(doc, target, error = 0.01) {
  const tris = triCount(doc);
  await doc.transform(weld({ tolerance: 1e-4 }));
  if (target && tris > target) {
    await doc.transform(simplify({ simplifier: MeshoptSimplifier, ratio: target / tris, error, lockBorder: false }));
  }
  await doc.transform(dedup(), prune());
}

// ---------------------------------------------------------------- texturas das pastas (FBX)
async function gray(file, size) {
  return sharp(file).resize(size, size, { fit: 'fill' }).removeAlpha().extractChannel(0).raw().toBuffer();
}
async function applyFolderTextures(doc, spec, texDir) {
  const files = existsSync(texDir) ? readdirSync(texDir) : [];
  const size = spec.tex;
  for (const mat of doc.getRoot().listMaterials()) {
    const name = mat.getName();
    const mine = files.filter((f) => spec.texFor(name, f));
    const pick = (re) => mine.find((f) => re.test(f));
    const base = pick(CH.base) || mine.find((f) => !CH.normal.test(f) && !CH.rough.test(f) && !CH.metal.test(f) && !CH.ao.test(f) && !CH.opacity.test(f));
    for (const t of [mat.getBaseColorTexture(), mat.getNormalTexture(), mat.getMetallicRoughnessTexture(), mat.getOcclusionTexture(), mat.getEmissiveTexture()]) t?.dispose();
    mat.setBaseColorFactor([1, 1, 1, 1]).setEmissiveFactor([0, 0, 0]);
    if (!base) { console.warn(`  aviso: ${spec.id}/${name} sem textura de cor`); continue; }
    let img = sharp(join(texDir, base)).resize(size, size, { fit: 'fill' }).ensureAlpha();
    const opacity = pick(CH.opacity);
    const baseMeta = await sharp(join(texDir, base)).metadata();
    let cut = false;
    if (opacity) {
      const a = await gray(join(texDir, opacity), size);
      img = sharp(await img.removeAlpha().raw().toBuffer(), { raw: { width: size, height: size, channels: 3 } }).joinChannel(a, { raw: { width: size, height: size, channels: 1 } });
      cut = true;
    } else if (baseMeta.hasAlpha) {
      const stats = await sharp(join(texDir, base)).stats();
      cut = stats.channels[3] && stats.channels[3].min < 128;
    }
    const png = await img.png().toBuffer();
    mat.setBaseColorTexture(doc.createTexture(`${name}_cor`).setImage(png).setMimeType('image/png'));
    mat.setAlphaMode(cut ? 'MASK' : 'OPAQUE');
    if (cut) { mat.setAlphaCutoff(0.5); mat.setDoubleSided(true); }
    const nrm = pick(CH.normal);
    if (nrm) mat.setNormalTexture(doc.createTexture(`${name}_normal`).setImage(await sharp(join(texDir, nrm)).resize(size / 2, size / 2, { fit: 'fill' }).png().toBuffer()).setMimeType('image/png'));
    const rough = pick(CH.rough), metal = pick(CH.metal), ao = pick(CH.ao);
    if (rough || metal || ao) {
      const h = size / 2, one = Buffer.alloc(h * h, 255), zero = Buffer.alloc(h * h, 0);
      const r = ao ? await gray(join(texDir, ao), h) : one;
      const g = rough ? await gray(join(texDir, rough), h) : Buffer.alloc(h * h, 230);
      const b = metal ? await gray(join(texDir, metal), h) : zero;
      const orm = Buffer.alloc(h * h * 3);
      for (let i = 0; i < h * h; i++) { orm[i * 3] = r[i]; orm[i * 3 + 1] = g[i]; orm[i * 3 + 2] = b[i]; }
      const tex = doc.createTexture(`${name}_orm`).setImage(await sharp(orm, { raw: { width: h, height: h, channels: 3 } }).png().toBuffer()).setMimeType('image/png');
      mat.setMetallicRoughnessTexture(tex).setRoughnessFactor(1).setMetallicFactor(metal ? 1 : 0);
      if (ao) mat.setOcclusionTexture(tex);
    } else {
      mat.setRoughnessFactor(0.9).setMetallicFactor(0);
    }
  }
}

// ---------------------------------------------------------------- USDZ (leitor do three.js)
async function usdzToDoc(file) {
  globalThis.self ??= globalThis;
  const three = join(TOOLS, 'node_modules/three');
  const { USDZLoader } = await import(join(three, 'examples/jsm/loaders/USDZLoader.js'));
  const { USDComposer } = await import(join(three, 'examples/jsm/loaders/usd/USDComposer.js'));
  const { Texture } = await import(join(three, 'build/three.module.js'));
  // o compositor cria imagens de navegador; aqui só interessa saber qual PNG do USDZ cada material usa
  USDComposer.prototype._createTextureFromData = function (data) {
    const t = new Texture();
    t.userData.bytes = data?.byteLength ?? data?.length ?? 0;
    return t;
  };
  const buf = readFileSync(file);
  const images = [...readZipBuffer(buf)].filter(([n]) => /\.(png|jpe?g)$/i.test(n));
  const group = new USDZLoader().parse(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength));
  group.updateMatrixWorld(true);
  const doc = new Document();
  const buffer = doc.createBuffer();
  const scene = doc.createScene('cena');
  const mats = new Map();
  const matFor = (m) => {
    const bytes = m?.map?.userData?.bytes || 0;
    const key = bytes || 'sem';
    if (mats.has(key)) return mats.get(key);
    const mat = doc.createMaterial(`usd_${mats.size}`).setRoughnessFactor(0.95).setMetallicFactor(0);
    const img = images.find(([, d]) => d.length === bytes);
    if (img) {
      mat.setBaseColorTexture(doc.createTexture(basename(img[0])).setImage(img[1]).setMimeType('image/png'));
      if (/_do\./i.test(img[0])) mat.setAlphaMode('MASK').setAlphaCutoff(0.5).setDoubleSided(true);
    }
    mats.set(key, mat);
    return mat;
  };
  group.traverse((o) => {
    if (!o.isMesh) return;
    const g = o.geometry.clone().applyMatrix4(o.matrixWorld);
    const idx = g.index ? g.index.array : Uint32Array.from({ length: g.attributes.position.count }, (_, i) => i);
    const groups = g.groups.length ? g.groups : [{ start: 0, count: idx.length, materialIndex: 0 }];
    const mesh = doc.createMesh(o.name);
    const acc = (arr, type) => doc.createAccessor().setType(type).setArray(arr).setBuffer(buffer);
    const P = acc(new Float32Array(g.attributes.position.array), 'VEC3');
    const N = g.attributes.normal ? acc(new Float32Array(g.attributes.normal.array), 'VEC3') : null;
    const T = g.attributes.uv ? acc(new Float32Array(g.attributes.uv.array), 'VEC2') : null;
    for (const gr of groups) {
      const mat = Array.isArray(o.material) ? o.material[gr.materialIndex] : o.material;
      const prim = doc.createPrimitive().setAttribute('POSITION', P).setIndices(acc(new Uint32Array(idx.slice(gr.start, gr.start + gr.count)), 'SCALAR')).setMaterial(matFor(mat));
      if (N) prim.setAttribute('NORMAL', N);
      if (T) prim.setAttribute('TEXCOORD_0', T);
      mesh.addPrimitive(prim);
    }
    scene.addChild(doc.createNode(o.name).setMesh(mesh));
  });
  doc.getRoot().setDefaultScene(scene);
  return doc;
}
function readZipBuffer(buf) {
  let e = buf.length - 22;
  while (e >= 0 && buf.readUInt32LE(e) !== 0x06054b50) e--;
  const out = new Map(), n = buf.readUInt16LE(e + 10);
  let p = buf.readUInt32LE(e + 16);
  for (let i = 0; i < n; i++) {
    const method = buf.readUInt16LE(p + 10), size = buf.readUInt32LE(p + 20);
    const nl = buf.readUInt16LE(p + 28), xl = buf.readUInt16LE(p + 30), cl = buf.readUInt16LE(p + 32), off = buf.readUInt32LE(p + 42);
    const name = buf.toString('utf8', p + 46, p + 46 + nl);
    const start = off + 30 + buf.readUInt16LE(off + 26) + buf.readUInt16LE(off + 28);
    const data = buf.subarray(start, start + size);
    out.set(name, method === 8 ? inflateRawSync(data) : Buffer.from(data));
    p += 46 + nl + xl + cl;
  }
  return out;
}

// ---------------------------------------------------------------- montagens do kit Kenney
// Cada peça é um GLB do kit (ladrilho de 1 unidade, texturas de 64 px em `Textures/`). As peças entram
// num documento só, cada uma num nó com a própria posição, giro e escala; o resto do caminho é o mesmo
// dos outros modelos (transformação assada, junção por material, base na origem).
async function kitToDoc(spec) {
  const dir = extract(spec, (n) => n.startsWith(KENNEY_DIR));
  const doc = new Document();
  doc.createBuffer();
  const scene = doc.createScene('cena');
  doc.getRoot().setDefaultScene(scene);
  for (const [piece, pos, rotY = 0, scale = [1, 1, 1], only = null] of spec.kit) {
    const file = join(dir, KENNEY_DIR, `${piece}.glb`);
    if (!existsSync(file)) throw new Error(`peça ausente no kit: ${piece}`);
    const src = await io.read(file);   // os GLB do kit apontam para `Textures/*.png` ao lado deles
    if (only) for (const m of src.getRoot().listMeshes()) for (const p of m.listPrimitives()) if (!only.includes(p.getMaterial()?.getName())) { m.removePrimitive(p); p.dispose(); }
    const map = mergeDocuments(doc, src);
    const merged = map.get(src.getRoot().listScenes()[0]);
    const node = doc.createNode(piece).setTranslation(pos).setRotation([0, Math.sin(rotY / 2), 0, Math.cos(rotY / 2)]).setScale(scale);
    for (const n of merged.listChildren()) node.addChild(n);
    scene.addChild(node);
    merged.dispose();
  }
  // o kit usa materiais sem luz (KHR_materials_unlit): aqui passam a receber luz e sombra como o resto,
  // com normais planas (faces desligadas antes do cálculo). A extensão sai antes do `prune`, que
  // descartaria as normais de um material sem luz.
  for (const e of doc.getRoot().listExtensionsUsed()) if (e.extensionName === 'KHR_materials_unlit') e.dispose();
  await doc.transform(unpartition(), unweld(), normals({ overwrite: true }), dedup(), prune());
  for (const m of doc.getRoot().listMaterials()) {
    m.setMetallicFactor(0).setRoughnessFactor(0.92);
    // pixels nítidos de perto, como no kit
    m.getBaseColorTextureInfo()?.setMagFilter(TextureInfo.MagFilter.NEAREST).setMinFilter(TextureInfo.MinFilter.LINEAR_MIPMAP_LINEAR);
    if (m.getName() === spec.tint) {
      // telhado em cinza claro: o jogo tinge com a cor do tecido de cada banca
      const tex = m.getBaseColorTexture();
      if (tex) tex.setImage(await sharp(tex.getImage()).grayscale().linear(1.35, 18).png().toBuffer()).setMimeType('image/png');
      m.setName('telhado').setDoubleSided(true);
    }
  }
  return doc;
}

// ---------------------------------------------------------------- armas
// Divide cada primitiva por zona (centro do triângulo) e troca o material por um nome de zona.
function zoneSplit(doc, spec) {
  const b = bounds(doc);
  const L = spec.long === 'x' ? 0 : 1, U = spec.long === 'x' ? 1 : 0, Wd = 2;
  const zmat = new Map();
  const matOf = (z) => zmat.get(z) || zmat.set(z, doc.createMaterial(z).setBaseColorFactor([0.8, 0.8, 0.8, 1]).setRoughnessFactor(0.6).setMetallicFactor(0)).get(z);
  const buffer = doc.getRoot().listBuffers()[0];
  for (const mesh of doc.getRoot().listMeshes()) {
    for (const prim of mesh.listPrimitives()) {
      const pos = prim.getAttribute('POSITION'), idx = prim.getIndices().getArray();
      const lists = new Map(), v = [0, 0, 0];
      for (let i = 0; i < idx.length; i += 3) {
        const c = [0, 0, 0];
        for (let k = 0; k < 3; k++) { pos.getElement(idx[i + k], v); c[0] += v[0] / 3; c[1] += v[1] / 3; c[2] += v[2] / 3; }
        const t = (c[L] - b.mn[L]) / b.size[L], u = (c[U] - b.mn[U]) / b.size[U], w = (c[Wd] - b.mn[Wd]) / b.size[Wd];
        const zone = spec.zones.find(([, f]) => f(t, u, w))[0];
        (lists.get(zone) || lists.set(zone, []).get(zone)).push(idx[i], idx[i + 1], idx[i + 2]);
      }
      for (const [zone, list] of lists) {
        const p = doc.createPrimitive().setAttribute('POSITION', pos).setIndices(doc.createAccessor().setType('SCALAR').setArray(new Uint32Array(list)).setBuffer(buffer)).setMaterial(matOf(zone));
        if (prim.getAttribute('NORMAL')) p.setAttribute('NORMAL', prim.getAttribute('NORMAL'));
        mesh.addPrimitive(p);
      }
      mesh.removePrimitive(prim);
      prim.dispose();
    }
  }
  for (const m of doc.getRoot().listMaterials()) if (!zmat.has(m.getName())) m.dispose();
  for (const t of doc.getRoot().listTextures()) t.dispose();
  for (const p of doc.getRoot().listMeshes().flatMap((m) => m.listPrimitives())) p.setAttribute('TEXCOORD_0', null);
  return [...zmat.keys()];
}

// Espaço da empunhadura do jogo: punho na origem, eixo longo em +Z, largura em +Y. O pacote traz o
// punho na origem e o eixo longo em Y (a manopla, em X).
function toGripSpace(doc, spec) {
  const v = [0, 0, 0];
  const map = spec.long === 'x' ? (p) => [-p[2], p[1], p[0]] : (p) => [p[2], p[0], p[1]];
  for (const sem of ['POSITION', 'NORMAL']) {
    for (const a of positions(doc, sem)) for (let i = 0; i < a.getCount(); i++) a.setElement(i, map(a.getElement(i, v)));
  }
}

// ---------------------------------------------------------------- saída
const io = new NodeIO().registerExtensions(ALL_EXTENSIONS).registerDependencies({ 'meshopt.decoder': MeshoptDecoder, 'meshopt.encoder': MeshoptEncoder });

async function finish(doc, spec, info) {
  if (spec.tex) {
    await doc.transform(
      textureCompress({ encoder: sharp, targetFormat: 'webp', slots: /^baseColor/, resize: [spec.tex, spec.tex], quality: 82, lossless: !!spec.kit }),
      textureCompress({ encoder: sharp, targetFormat: 'webp', slots: /^(normal|metallicRoughness|occlusion)/, resize: [spec.tex / 2, spec.tex / 2], quality: 85 }),
    );
  }
  await doc.transform(dedup(), prune(), meshopt({ encoder: MeshoptEncoder, level: 'medium' }));
  const glb = Buffer.from(await io.writeBinary(doc));
  writeFileSync(join(OUT, `${spec.id}.glb`), glb);
  const b = info.bounds;
  return {
    id: spec.id, glb: `${spec.id}.glb`, bytes: glb.length, sha256: sha256(glb), triangles: info.tris,
    source: { file: spec.file, entry: spec.fbx || spec.usdz || spec.blend || spec.object || (spec.kit ? `${KENNEY_DIR}(${[...new Set(spec.kit.map((k) => k[0]))].join(', ')})` : null), object: spec.object || null, sha256: info.srcSha, triangles: info.srcTris, title: spec.title || null },
    author: spec.author || null, license: spec.license, url: spec.url || null,
    size: b.size.map((x) => round(x)), extra: info.extra || {},
  };
}

async function buildAsset(spec) {
  const srcFile = join(SRC, spec.file);
  if (!existsSync(srcFile)) throw new Error(`fonte ausente: ${spec.file}`);
  const srcSha = sha256(readFileSync(srcFile));
  let doc, origTris = null;
  if (spec.fbx) {
    const dir = extract(spec, (n) => n === spec.fbx || (spec.texDir && n.startsWith(spec.texDir)));
    doc = await io.read(fbxToGlb(join(dir, spec.fbx), spec.id));
    await applyFolderTextures(doc, spec, join(dir, spec.texDir));
  } else if (spec.kit) {
    doc = await kitToDoc(spec);
  } else if (spec.usdz) {
    const dir = extract(spec, (n) => n === spec.usdz);
    doc = await usdzToDoc(join(dir, spec.usdz));
  } else if (spec.object) {
    let blend = srcFile;
    if (spec.blend) blend = join(extract(spec, (n) => n === spec.blend), spec.blend);
    const r = blender(spec.id, { blend, object: spec.object, tris: spec.tris });
    doc = await io.read(r.path); origTris = r.before;
  } else if (spec.blender) {
    const r = blender(spec.id, { glb: srcFile, tris: spec.tris });
    doc = await io.read(r.path); origTris = r.before;
  } else {
    doc = await io.read(srcFile);
  }
  const dbg = (stage) => { if (process.env.DEBUG_EXTRA) console.log(`  [${spec.id}] ${stage}: ${bounds(doc).size.map((v) => v.toPrecision(4)).join(' × ')}`); };
  dbg('fonte');
  bakeScene(doc);
  dbg('assado');
  await doc.transform(joinPrims({ keepNamed: false }));
  dbg('juntado');
  const srcTris = origTris ?? triCount(doc);
  const extra = {};
  if (spec.lods) {
    // níveis de detalhe no mesmo arquivo: grupo "L0" com o modelo inteiro e "L1" com cópias reduzidas
    await doc.transform(weld({ tolerance: 1e-4 }));
    normalize(doc, spec);
    const holder = doc.getRoot().getDefaultScene().listChildren()[0];
    const l0 = doc.createNode('L0'), l1 = doc.createNode('L1');
    const meshTris = (m) => m.listPrimitives().reduce((k, p) => k + p.getIndices().getCount() / 3, 0);
    let t0 = 0, t1 = 0;
    for (const part of holder.listChildren()) {
      holder.removeChild(part);
      l0.addChild(part);
      const full = part.getMesh(), cheap = doc.createMesh(`${full.getName()}_L1`);
      for (const p of full.listPrimitives()) {
        const q = doc.createPrimitive().setMaterial(p.getMaterial()).setIndices(p.getIndices().clone());
        for (const sem of p.listSemantics()) q.setAttribute(sem, p.getAttribute(sem).clone());
        cheap.addPrimitive(q);
        simplifyPrimitive(q, { simplifier: MeshoptSimplifier, ratio: spec.lods[1], error: 0.02, lockBorder: false });
      }
      l1.addChild(doc.createNode(`${part.getName()}_L1`).setMesh(cheap));
      t0 += meshTris(full); t1 += meshTris(cheap);
    }
    holder.addChild(l0).addChild(l1);
    extra.lodTriangles = [t0, t1];
  } else {
    // o Blender já reduziu os de .blend e os GLB marcados com `blender`
    await reduce(doc, spec.object || spec.blender || spec.kit ? 0 : spec.tris);
    dbg('reduzido');
    normalize(doc, spec);
    dbg('normalizado');
  }
  // assentamento: plano de contato, base aplanada e pegada da base (unidades do modelo, base em y = 0)
  if (spec.support) {
    let contactY = contactPlane(doc);
    if (spec.cleanBase) {
      extra.baseFacesRemoved = flattenBase(doc, contactY);
      smoothNormals(doc);
      normalize(doc, { height: 0 });   // a base aplanada vira y = 0, sem mudar a escala
      contactY = 0;
    }
    const h = bounds(doc).size[1];
    extra.support = spec.support;
    extra.contactY = round(contactY);
    extra.footprint = footprintAt(doc, contactY, Math.max(0.06 * h, spec.support === 'point' ? 0.04 * h : 0));
  }
  // materiais opacos com uma face só: a segunda face mostrava o avesso serrilhado das bases
  if (!spec.keepSides) {
    for (const m of doc.getRoot().listMaterials()) {
      if (m.getAlphaMode() === 'OPAQUE' && !(spec.doubleSided || []).includes(m.getName())) m.setDoubleSided(false);
    }
  }
  if (spec.stone) { await stoneMaterials(doc); extra.stone = true; }
  if (spec.roadway) await roadwayMaterials(doc, spec);
  if (spec.deckTop) extra.deckY = spec.deckTop;   // piso da doca: a montagem já nasce com a base em y = 0
  if (spec.deck) {
    // altura do piso da ponte: topo do material da pista no terço central do comprimento
    const b = bounds(doc), long = b.size[0] >= b.size[2] ? 0 : 2, v = [0, 0, 0];
    let top = -Infinity, c0 = Infinity, c1 = -Infinity;
    const cross = long === 0 ? 2 : 0;
    for (const mesh of doc.getRoot().listMeshes()) for (const p of mesh.listPrimitives()) {
      if (p.getMaterial()?.getName() !== spec.deck) continue;
      const a = p.getAttribute('POSITION');
      for (let i = 0; i < a.getCount(); i++) {
        a.getElement(i, v);
        if (Math.abs(v[long]) < b.size[long] / 6 && v[1] > top) top = v[1];
        c0 = Math.min(c0, v[cross]); c1 = Math.max(c1, v[cross]);
      }
    }
    extra.deckY = round(top);
    extra.roadHalf = round((c1 - c0) / 2);   // meia-largura da pista (unidades do modelo)
    extra.longAxis = long === 0 ? 'x' : 'z';
  }
  const info = { srcSha, srcTris, tris: triCount(doc), bounds: bounds(doc), extra };
  return finish(doc, spec, info);
}

async function buildWeapon(w) {
  const spec = { ...w, file: 'pp_freefantasyrpgweapons_fbx_files.zip', ...USER, author: 'PurePoly', tex: 0, title: w.src };
  const srcFile = join(SRC, spec.file);
  if (!existsSync(srcFile)) throw new Error(`fonte ausente: ${spec.file}`);
  const dir = extract({ ...spec, id: w.id }, (n) => n === `${W}${w.src}.fbx`);
  const doc = await io.read(fbxToGlb(join(dir, W, `${w.src}.fbx`), w.id));
  bakeScene(doc);
  await doc.transform(joinPrims({ keepNamed: false }));
  const srcTris = triCount(doc);
  await reduce(doc, WEAPON_TRIS, 0.005);
  const zones = zoneSplit(doc, w);
  toGripSpace(doc, w);
  const info = { srcSha: sha256(readFileSync(srcFile)), srcTris, tris: triCount(doc), bounds: bounds(doc), extra: { zones } };
  return finish(doc, spec, info);
}

// ---------------------------------------------------------------- main
async function main() {
  await MeshoptEncoder.ready; await MeshoptDecoder.ready; await MeshoptSimplifier.ready;
  if (!existsSync(FBX2GLTF)) fail(`FBX2glTF não encontrado em ${FBX2GLTF}`);
  if (!existsSync(SRC)) fail(`pasta de originais ausente: ${SRC}`);
  mkdirSync(OUT, { recursive: true });
  mkdirSync(TMP, { recursive: true });
  const only = process.argv.slice(2);
  const known = new Set([...ASSETS.map((a) => a.id), ...WEAPONS.map((w) => w.id)]);
  for (const id of only) if (!known.has(id)) fail(`id desconhecido: ${id}`);
  const manifestPath = join(OUT, 'extra-props-manifest.json');
  const prev = existsSync(manifestPath) ? JSON.parse(readFileSync(manifestPath, 'utf8')).assets : [];
  const byId = new Map(prev.map((a) => [a.id, a]));
  const failures = [];
  for (const spec of [...ASSETS, ...WEAPONS]) {
    if (only.length && !only.includes(spec.id)) continue;
    try {
      const t0 = Date.now();
      const entry = spec.src ? await buildWeapon(spec) : await buildAsset(spec);
      byId.set(entry.id, entry);
      const seat = entry.extra.support ? `  ${entry.extra.support}${entry.extra.baseFacesRemoved ? `, base −${entry.extra.baseFacesRemoved} faces` : ''}, pegada ${entry.extra.footprint.length} pts` : '';
      console.log(`✓ ${entry.id.padEnd(15)} ${String(entry.source.triangles).padStart(8)} → ${String(entry.triangles).padStart(6)} tri  ${entry.size.join(' × ')} m  ${KB(entry.bytes)}  ${((Date.now() - t0) / 1000).toFixed(0)} s${seat}`);
    } catch (err) {
      failures.push(`${spec.id}: ${err.message}`);
      console.error(`✗ ${spec.id}: ${err.message}`);
    }
  }
  rmSync(TMP, { recursive: true, force: true });
  if (failures.length) fail(`${failures.length} falha(s); registro não atualizado`);
  const order = [...ASSETS.map((a) => a.id), ...WEAPONS.map((w) => w.id)];
  const assets = order.map((id) => byId.get(id)).filter(Boolean);
  const missing = order.filter((id) => !byId.has(id));
  if (missing.length) fail(`sem derivado para: ${missing.join(', ')} (rode sem filtro)`);
  writeFileSync(manifestPath, JSON.stringify({ schema: '1.0.0', generated_by: 'tools/build-extra-assets.mjs', source_dir: 'modelos 3d animados/extras-2026-09-26', assets }, null, 2) + '\n');
  const lines = [
    '// Gerado por tools/build-extra-assets.mjs — não editar à mão.',
    '// Assets extras (pontes, construções, marcos, criatura e armas) e o manifesto de origem de cada um.',
    "import MANIFEST from '../../assets/extra-props/extra-props-manifest.json';",
    ...assets.map((a) => `import ${a.id} from '../../assets/extra-props/${a.glb}';`),
    '',
    `export const EXTRA_BINS = { ${assets.map((a) => a.id).join(', ')} };`,
    'export const EXTRA_META = Object.fromEntries(MANIFEST.assets.map((a) => [a.id, a]));',
    '',
  ];
  writeFileSync(REGISTRY, lines.join('\n'));
  const total = assets.reduce((n, a) => n + a.bytes, 0);
  console.log(`· ${assets.length} derivados, ${KB(total)} no total; registro em src/engine/extra-assets.js`);
}

main().catch((e) => fail(e.stack || e.message));
