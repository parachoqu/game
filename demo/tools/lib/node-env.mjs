// Peças comuns às ferramentas que rodam os módulos da demo no Node (testes do mundo e o pacote de
// simulação do C++): onde está o esbuild e o ambiente mínimo de navegador que os módulos esperam.
import { existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const TOOLS = dirname(dirname(fileURLToPath(import.meta.url)));

// Mesmo esbuild do build (ESBUILD_BIN, o de `npm ci` em demo/tools, ou um dos caminhos conhecidos).
export function findEsbuild() {
  const candidates = [
    process.env.ESBUILD_BIN,
    join(TOOLS, 'node_modules', 'esbuild', 'bin', 'esbuild'),
    '/home/https/.npm/_npx/7f657307477966ff/node_modules/esbuild/bin/esbuild',
    '/home/https/.npm/_npx/74274893b9fe65d3/node_modules/esbuild/bin/esbuild',
    '/home/https/open-design/node_modules/.pnpm/esbuild@0.28.0/node_modules/esbuild/bin/esbuild',
    '/usr/local/bin/esbuild',
    '/usr/bin/esbuild',
  ].filter(Boolean);
  return candidates.find((p) => existsSync(p)) || null;
}

// Ambiente mínimo de navegador: os módulos carregados não desenham nada, só precisam que as APIs
// existam (canvas, localStorage, performance, áudio, imagens).
//
// `loadImages`: `new Image()` dispara `onload` logo depois de receber `src` (1 × 1 px, pixels
// zerados no canvas falso). Os testes não precisam; o pacote de simulação sim, porque o kit de
// natureza só registra variantes e colisores depois de decodificar as texturas.
export function installBrowserShim({ loadImages = false } = {}) {
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
  globalThis.Image = loadImages
    ? function Image() {
      const img = makeElement();
      let src = '';
      Object.defineProperty(img, 'src', {
        get: () => src,
        set: (v) => { src = v; setTimeout(() => img.onload?.(), 0); },
      });
      return img;
    }
    : function Image() { return makeElement(); };
  globalThis.requestAnimationFrame = noop;
  globalThis.addEventListener = noop;
  globalThis.createImageBitmap = async () => ({ width: 1, height: 1, close: noop });
  if (!globalThis.performance) globalThis.performance = { now: () => Date.now() };
}
