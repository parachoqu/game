// Gera a demo em um único HTML offline.
//   node demo/build.mjs            → dist/Projeto_Game_Demo.html (documento completo, abre via file://)
//                                   dist/projeto-game-demo.html (mesmo conteúdo sem <html>/<head>, para Artifact)
// Usa o esbuild local (defina ESBUILD_BIN para outro caminho). Three.js vem de vendor/three (MIT).
import { execFileSync } from 'node:child_process';
import { readFileSync, writeFileSync, existsSync, statSync, rmSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = dirname(fileURLToPath(import.meta.url));

// Os pacotes preparados são obrigatórios: sem eles a demo não tem relevo, cenário nem vegetação.
const required = [
  ['assets/world-runtime', 'node tools/build-world-runtime.mjs', ['world-manifest.json',
    'region-height.bin', 'region-mask.bin', 'turbulent-height.bin',
    'region-props.glb', 'region-water.glb', 'turbulent-props.glb']],
  ['assets/nature-kit', 'node tools/build-nature-kit.mjs', ['kit-manifest.json', 'nature-kit.glb',
    'tex/palette.webp', 'tex/branch.webp', 'tex/leaves.webp', 'tex/grass.webp',
    'tex/flower.webp', 'tex/flowerLeaf.webp']],
];
for (const [dir, cmd, files] of required) {
  for (const f of files) {
    if (!existsSync(join(root, dir, f))) {
      console.error(`pacote incompleto: falta ${dir}/${f}\nRode: ${cmd}`);
      process.exit(1);
    }
  }
}
const candidateEsbuilds = [
  process.env.ESBUILD_BIN,
  '/home/https/.npm/_npx/7f657307477966ff/node_modules/esbuild/bin/esbuild',
  '/home/https/.npm/_npx/74274893b9fe65d3/node_modules/esbuild/bin/esbuild',
  '/home/https/open-design/node_modules/.pnpm/esbuild@0.28.0/node_modules/esbuild/bin/esbuild',
  '/usr/local/bin/esbuild',
  '/usr/bin/esbuild',
].filter(Boolean);
const esbuild = candidateEsbuilds.find((p) => existsSync(p));
if (!esbuild) {
  console.error(`esbuild não encontrado. Defina ESBUILD_BIN.`);
  process.exit(1);
}

const bundlePath = join(root, 'dist', '.bundle.js');
execFileSync(esbuild, [
  'src/main.js', '--bundle', '--format=iife', '--minify', '--target=es2020',
  '--alias:three=./vendor/three/three.module.js',
  '--loader:.glb=binary',   // modelos de assets/ e cenário do mundo (base64 → Uint8Array)
  '--loader:.bin=binary',   // heightfields e máscaras do mundo preparado (Uint16/nibbles)
  '--loader:.json=json',    // manifesto compacto do mundo (assets/world-runtime)
  '--loader:.webp=dataurl', // materiais de natureza (WebP) embutidos no HTML offline
  '--legal-comments=eof', `--outfile=${bundlePath}`, '--log-level=warning',
], { cwd: root, stdio: 'inherit' });

const font = readFileSync(join(root, 'vendor/fonts/fraunces-latin-600-normal.woff2')).toString('base64');
const css = readFileSync(join(root, 'src/styles.css'), 'utf8').split('__FRAUNCES__').join(font);
const js = readFileSync(bundlePath, 'utf8').split('</script').join('<\\/script');
rmSync(bundlePath);
const page = readFileSync(join(root, 'src/index.html'), 'utf8')
  .split('/*__CSS__*/').join(css)
  .split('/*__JS__*/').join(js);

const full = '<!doctype html>\n<html lang="pt-BR">\n<head>\n<meta charset="utf-8">\n' +
  '<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">\n' +
  page + '\n</html>\n';

const out = join(root, 'dist', 'Projeto_Game_Demo.html');
writeFileSync(out, full);
writeFileSync(join(root, 'dist', 'projeto-game-demo.html'), page);
const bytes = statSync(out).size;
console.log(`ok  ${out}  (${(bytes / 1048576).toFixed(1)} MB)`);
// Metas declaradas para o HTML autônomo: 45 MB de alvo, 70 MB de limite.
if (bytes > 70 * 1048576) console.warn(`aviso: ${(bytes / 1048576).toFixed(1)} MB passa do limite de 70 MB`);
else if (bytes > 45 * 1048576) console.warn(`aviso: ${(bytes / 1048576).toFixed(1)} MB passa da meta de 45 MB`);
