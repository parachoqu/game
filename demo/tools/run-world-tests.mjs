// Executa os testes do mundo pelo mesmo caminho do build.
//
//   node tools/run-world-tests.mjs
//
// Os módulos do mundo importam binários (.bin), o manifesto (.json) e GLB (.glb) pelos loaders do
// esbuild, exatamente como no HTML final. Aqui o mesmo empacotador produz um arquivo temporário
// que roda no Node com um ambiente mínimo de navegador (canvas, localStorage, performance).
import { execFileSync } from 'node:child_process';
import { rmSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { findEsbuild, installBrowserShim } from './lib/node-env.mjs';

const TOOLS = dirname(fileURLToPath(import.meta.url));
const DEMO = dirname(TOOLS);

const esbuild = findEsbuild();
if (!esbuild) { console.error('esbuild não encontrado. Defina ESBUILD_BIN.'); process.exit(1); }

const bundle = join(DEMO, 'tests', '.world.test.bundle.mjs');
execFileSync(esbuild, [
  process.env.TEST_ENTRY || 'tests/world.test.js', '--bundle', '--format=esm', '--target=es2022', '--platform=neutral',
  '--alias:three=./vendor/three/three.module.js',
  '--loader:.glb=binary', '--loader:.bin=binary', '--loader:.json=json', '--loader:.webp=dataurl',
  `--outfile=${bundle}`, '--log-level=warning',
], { cwd: DEMO, stdio: 'inherit' });

// ---- ambiente mínimo de navegador (os módulos testados não desenham nada) ----
installBrowserShim();

try {
  await import(pathToFileURL(bundle).href);
} finally {
  rmSync(bundle, { force: true });
}
process.exit(globalThis.__testExit ?? 0);
