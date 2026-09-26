// Prepara os derivados locais das texturas CC0 (Poly Haven) que a demo embute no HTML.
// Os PNGs originais e suas licenças ficam intactos em "modelos 3d animados/texturas".
//
// Por superfície saem três WebP: cor (sRGB), normal e um mapa de dados com altura (R) e rugosidade (G).
// Altura e rugosidade não entram no alfa da cor: o canvas devolve a cor dividida pelo alfa, e valores
// baixos destruiriam o albedo. A folhagem usa alfa de verdade, porque ali o recorte é a própria máscara.
//
// Uso: node demo/tools/build-nature-assets.mjs
import { readFile, writeFile, mkdir, readdir, unlink } from 'node:fs/promises';
import { dirname, resolve, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createHash } from 'node:crypto';
import sharp from 'sharp';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '../..');
const library = join(root, 'modelos 3d animados/texturas');
const out = join(root, 'demo/assets/nature');
const manifest = JSON.parse(await readFile(join(library, 'manifesto.json'), 'utf8'));
await mkdir(out, { recursive: true });
for (const f of await readdir(out)) if (/\.(png|webp)$/.test(f)) await unlink(join(out, f));

const imports = [], entries = [], report = [];
const hash = (bytes) => createHash('sha256').update(bytes).digest('hex');

async function source(id, map) {
  const f = manifest.files.find((f) => f.assetId === id && f.map === map);
  if (!f) throw new Error(`Mapa ausente: ${id}/${map}`);
  const bytes = await readFile(join(library, f.relativePath));
  if (hash(bytes) !== f.sha256) throw new Error(`Original alterado: ${f.relativePath}`);
  return bytes;
}

async function emit(id, name, size, buffer, note) {
  const file = `${id}_${name}_${size}.webp`, symbol = `t${imports.length}`;
  await writeFile(join(out, file), buffer);
  imports.push(`import ${symbol} from './${file}';`);
  report.push({ assetId: id, file, width: size, height: size, content: note, bytes: buffer.length, sha256: hash(buffer) });
  return symbol;
}

const raw = async (bytes, size, opts = {}) => sharp(bytes).resize(size, size, { fit: 'fill', kernel: 'lanczos3' }).raw().toBuffer({ resolveWithObject: true }).then((r) => r);

// cor: RGB em sRGB, sem alfa
async function colorMap(id, map, size, quality = 82) {
  const bytes = await source(id, map);
  const buf = await sharp(bytes).resize(size, size, { fit: 'fill', kernel: 'lanczos3' })
    .removeAlpha().webp({ quality, effort: 6 }).toBuffer();
  return emit(id, 'col', size, buf, 'cor sRGB');
}
// normal: RGB linear (subamostragem inteligente evita borrar a inclinação)
async function normalMap(id, map, size, quality = 90) {
  const bytes = await source(id, map);
  const buf = await sharp(bytes).resize(size, size, { fit: 'fill', kernel: 'lanczos3' })
    .removeAlpha().webp({ quality, effort: 6, smartSubsample: true }).toBuffer();
  return emit(id, 'nrm', size, buf, 'normal (linear)');
}
// dados: R = altura, G = rugosidade
async function dataMap(id, heightMap, roughMap, size, quality = 80) {
  const h = await sharp(await source(id, heightMap)).resize(size, size, { fit: 'fill', kernel: 'lanczos3' }).greyscale().raw().toBuffer();
  const r = await sharp(await source(id, roughMap)).resize(size, size, { fit: 'fill', kernel: 'lanczos3' }).greyscale().raw().toBuffer();
  const rgb = Buffer.alloc(size * size * 3);
  for (let i = 0; i < size * size; i++) { rgb[i * 3] = h[i]; rgb[i * 3 + 1] = r[i]; }
  const buf = await sharp(rgb, { raw: { width: size, height: size, channels: 3 } }).webp({ quality, effort: 6 }).toBuffer();
  return emit(id, 'hr', size, buf, 'altura (R) + rugosidade (G)');
}
// folhagem: cor com o recorte no alfa
async function cutoutMap(id, colorName, alphaName, size, quality = 84) {
  const c = await sharp(await source(id, colorName)).resize(size, size, { fit: 'fill', kernel: 'lanczos3' }).removeAlpha().raw().toBuffer();
  const a = await sharp(await source(id, alphaName)).resize(size, size, { fit: 'fill', kernel: 'lanczos3' }).greyscale().raw().toBuffer();
  const rgba = Buffer.alloc(size * size * 4);
  for (let i = 0; i < size * size; i++) {
    rgba[i * 4] = c[i * 3]; rgba[i * 4 + 1] = c[i * 3 + 1]; rgba[i * 4 + 2] = c[i * 3 + 2];
    rgba[i * 4 + 3] = a[i];
  }
  const buf = await sharp(rgba, { raw: { width: size, height: size, channels: 4 } })
    .webp({ quality, alphaQuality: 92, effort: 6 }).toBuffer();
  return emit(id, 'col', size, buf, 'cor sRGB + recorte no alfa');
}
async function greyMap(id, map, size, name, quality = 78) {
  const bytes = await source(id, map);
  const buf = await sharp(bytes).resize(size, size, { fit: 'fill', kernel: 'lanczos3' }).greyscale().webp({ quality, effort: 6 }).toBuffer();
  return emit(id, name, size, buf, 'rugosidade');
}

// Tamanhos por uso: a cor sustenta a leitura de perto; normal e dados podem ser menores porque o
// terreno já mistura camadas e a projeção triplanar repete o material.
const SURFACES = [
  ['sparse_grass', 1024, 512], ['rocky_trail_02', 1024, 512], ['forrest_ground_01', 1024, 512],
  ['river_small_rocks', 1024, 512], ['rock_3', 1024, 1024], ['dark_rock', 1024, 1024],
  ['bark_brown_02', 1024, 1024], ['pine_bark', 1024, 1024],
];
for (const [id, colorSize, dataSize] of SURFACES) {
  const spec = manifest.assets.find((a) => a.asset === id);
  const col = await colorMap(id, 'Diffuse', colorSize);
  const nrm = await normalMap(id, 'nor_gl', dataSize);
  const hr = await dataMap(id, 'Displacement', 'Rough', Math.min(dataSize, 512));
  entries.push(`${JSON.stringify(id)}: { col:${col},nrm:${nrm},hr:${hr},size:${colorSize},metres:${spec.surface_repeat_metres[0]} }`);
}

for (const [id, prefix, key] of [['island_tree_03', 'leaves', 'leaves'], ['pine_tree_01', 'twig', 'needles']]) {
  // 512 basta: cada folha ocupa uma fração do atlas e os cartões são pequenos na tela
  const col = await cutoutMap(id, `${prefix}_diff`, `${prefix}_alpha`, 512, 86);
  const nrm = await normalMap(id, `${prefix}_nor_gl`, 512);
  const rgh = await greyMap(id, `${prefix}_rough`, 256, 'rgh');
  entries.push(`${JSON.stringify(key)}: { col:${col},nrm:${nrm},rgh:${rgh},size:512 }`);
}

await writeFile(join(out, 'textures.js'), `// Gerado por demo/tools/build-nature-assets.mjs.\n${imports.join('\n')}\nexport const NATURE = {\n${entries.join(',\n')}\n};\n`);
await writeFile(join(out, 'manifesto-derivados.json'), JSON.stringify({
  generatedFrom: 'modelos 3d animados/texturas/manifesto.json',
  originalFilesUnchanged: true, format: 'webp', files: report,
}, null, 2) + '\n');
const total = report.reduce((sum, f) => sum + f.bytes, 0);
console.log(`${report.length} derivados WebP · ${(total / 1048576).toFixed(2)} MiB (base64 ≈ ${(total * 1.37 / 1048576).toFixed(2)} MiB no HTML)`);
