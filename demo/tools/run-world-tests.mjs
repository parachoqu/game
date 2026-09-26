// Executa os testes do mundo pelo mesmo caminho do build.
//
//   node tools/run-world-tests.mjs
//
// Os módulos do mundo importam binários (.bin), o manifesto (.json) e GLB (.glb) pelos loaders do
// esbuild, exatamente como no HTML final. Aqui o mesmo empacotador produz um arquivo temporário
// que roda no Node com um ambiente mínimo de navegador (canvas, localStorage, performance).
import { execFileSync } from 'node:child_process';
import { existsSync, rmSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const TOOLS = dirname(fileURLToPath(import.meta.url));
const DEMO = dirname(TOOLS);

const candidates = [
  process.env.ESBUILD_BIN,
  '/home/https/.npm/_npx/7f657307477966ff/node_modules/esbuild/bin/esbuild',
  '/home/https/.npm/_npx/74274893b9fe65d3/node_modules/esbuild/bin/esbuild',
  '/home/https/open-design/node_modules/.pnpm/esbuild@0.28.0/node_modules/esbuild/bin/esbuild',
  '/usr/local/bin/esbuild',
  '/usr/bin/esbuild',
].filter(Boolean);
const esbuild = candidates.find((p) => existsSync(p));
if (!esbuild) { console.error('esbuild não encontrado. Defina ESBUILD_BIN.'); process.exit(1); }

const bundle = join(DEMO, 'tests', '.world.test.bundle.mjs');
execFileSync(esbuild, [
  process.env.TEST_ENTRY || 'tests/world.test.js', '--bundle', '--format=esm', '--target=es2022', '--platform=neutral',
  '--alias:three=./vendor/three/three.module.js',
  '--loader:.glb=binary', '--loader:.bin=binary', '--loader:.json=json', '--loader:.webp=dataurl',
  `--outfile=${bundle}`, '--log-level=warning',
], { cwd: DEMO, stdio: 'inherit' });

// ---- ambiente mínimo de navegador (os módulos testados não desenham nada) ----
const noop = () => {};
const fakeCanvasCtx = new Proxy({}, {
  get: (t, k) => {
    if (k === 'canvas') return { width: 1, height: 1 };
    if (k === 'getImageData') return (x, y, w, h) => ({ data: new Uint8ClampedArray(w * h * 4), width: w, height: h });
    if (k === 'createImageData') return (w, h) => ({ data: new Uint8ClampedArray(w * h * 4), width: w, height: h });
    if (k === 'measureText') return () => ({ width: 0 });
    return noop;
  },
});
const makeElement = () => ({
  width: 1, height: 1, style: {}, dataset: {}, children: [], classList: { add: noop, remove: noop, toggle: noop },
  getContext: () => fakeCanvasCtx, appendChild: noop, addEventListener: noop, removeEventListener: noop,
  setAttribute: noop, insertAdjacentHTML: noop, focus: noop, blur: noop, remove: noop, querySelector: () => null,
  querySelectorAll: () => [], toDataURL: () => 'data:,',
});
const store = new Map();
globalThis.window = globalThis;
globalThis.document = {
  createElement: makeElement, getElementById: () => makeElement(), querySelector: () => null,
  querySelectorAll: () => [], addEventListener: noop, removeEventListener: noop,
  body: makeElement(), documentElement: makeElement(), activeElement: null, visibilityState: 'visible',
};
globalThis.location = { search: '', href: 'file:///teste', reload: noop };
globalThis.localStorage = {
  getItem: (k) => (store.has(k) ? store.get(k) : null),
  setItem: (k, v) => store.set(k, String(v)),
  removeItem: (k) => store.delete(k),
};
globalThis.devicePixelRatio = 1;
globalThis.innerWidth = 1280;
globalThis.innerHeight = 720;
globalThis.matchMedia = () => ({ matches: false, addEventListener: noop, removeEventListener: noop });
globalThis.AudioContext = function AudioContext() { return { state: 'suspended', createGain: () => ({ connect: noop, gain: { value: 0, setValueAtTime: noop } }), destination: {}, resume: noop, currentTime: 0 }; };
globalThis.Image = function Image() { return makeElement(); };
globalThis.requestAnimationFrame = noop;
globalThis.addEventListener = noop;
globalThis.createImageBitmap = async () => ({ width: 1, height: 1, close: noop });
if (!globalThis.performance) globalThis.performance = { now: () => Date.now() };

try {
  await import(pathToFileURL(bundle).href);
} finally {
  rmSync(bundle, { force: true });
}
process.exit(globalThis.__testExit ?? 0);
