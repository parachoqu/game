// Capturas de referência da demo ATUAL (dist/Projeto_Game_Demo.html), nos mesmos enquadramentos de
// tools/capture-demo-views.mjs, para comparar com as do cliente C++ (cpp/tools/compare_captures.py).
//
// Roda no Chromium do Playwright com WebGL por software (SwiftShader), em 1100 × 700, meio-dia, sem
// HUD e sem o overlay de depuração. Diferente de demo/captures/after (gravadas antes das últimas
// mudanças da demo), estas refletem exatamente o build atual.
//
//   node demo/tools/capture-reference.mjs [pasta de saída] [--only 02_mercado,...] [--quality alta|media|baixa]
import { mkdirSync, writeFileSync } from 'node:fs';
import { createRequire } from 'node:module';
import { execSync } from 'node:child_process';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const TOOLS = dirname(fileURLToPath(import.meta.url));
const DEMO = dirname(TOOLS);
const args = process.argv.slice(2);
const flag = (name) => { const i = args.indexOf(name); return i >= 0 ? args.splice(i, 2)[1] : null; };
const only = flag('--only')?.split(',') ?? null;
const quality = flag('--quality') ?? 'alta';
const OUT = resolve(args[0] || join(DEMO, '..', 'cpp', 'build', 'reference-captures'));
mkdirSync(OUT, { recursive: true });

function loadPlaywright() {
  const require = createRequire(import.meta.url);
  try { return require('playwright'); } catch { /* tenta o global */ }
  const root = execSync('npm root -g').toString().trim();
  return require(join(root, 'playwright'));
}

// tools/capture-demo-views.mjs
const VIEWS = [
  ['02_mercado', [40, 24, 405], [40, 18, 375]],
  ['03_ponte_principal', [-15, 23, 120], [0, 18, 145]],
  ['04_passagem_garganta', [-25, 30, -50], [-25, 24, -95]],
  ['05_bosque_forjas', [-70, 30, -80], [-120, 24, -80]],
  ['06_mina', [-315, 32, 72], [-350, 24, 72]],
  ['07_observatorio', [-235, 116, -205], [-265, 110, -205]],
  ['08_entreposto_norte', [35, 36, -395], [35, 29, -435]],
  ['09_campos', [110, 28, 290], [145, 20, 240]],
  ['10_margens_rio', [80, 22, 145], [25, 16, 145]],
  ['11_ermos', [-315, 48, -335], [-360, 34, -375]],
  ['12_portal_turbulenta', [295, 31, -295], [315, 24, -320]],
  ['13_vista_elevada_horizonte', [-260, 140, -180], [60, 25, 180]],
];
const want = (name) => !only || only.includes(name);

const { chromium } = loadPlaywright();
const browser = await chromium.launch({
  headless: true,
  args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist', '--disable-gpu-sandbox'],
});
const page = await browser.newPage({ viewport: { width: 1100, height: 700 }, deviceScaleFactor: 1 });
page.setDefaultTimeout(900000);
page.on('pageerror', (e) => console.error('erro na página:', e.message));
const t0 = Date.now();
await page.goto(pathToFileURL(join(DEMO, 'dist', 'Projeto_Game_Demo.html')).href);
await page.waitForFunction(() => window.__demo && ['title', 'game'].includes(window.__demo.mode()), null, { timeout: 600000, polling: 1000 });
console.log(`demo carregada em ${((Date.now() - t0) / 1000).toFixed(0)} s`);
await page.evaluate((q) => window.__demo.applyQuality(q), quality);
const frames = (n) => page.evaluate((k) => new Promise((r) => {
  const step = () => (k-- <= 0 ? r() : requestAnimationFrame(step));
  requestAnimationFrame(step);
}), n);
const shot = async (name) => {
  await page.screenshot({ path: join(OUT, `${name}.png`), timeout: 900000 });
  console.log(`  ${name} (${((Date.now() - t0) / 1000).toFixed(0)} s)`);
};

if (want('01_title_screen')) {
  await frames(3);
  await shot('01_title_screen');
}

await page.evaluate(() => {
  document.getElementById('btn-new')?.click();
  document.getElementById('create-form')?.requestSubmit();
});
await page.waitForFunction(() => window.__demo.mode() === 'game', null, { timeout: 120000, polling: 500 });
await page.evaluate(() => {
  const D = window.__demo;
  D.G.time = 52.5;  // 12h (ciclo de 420 s começando às 9h)
  D.G.uiOpen = null;
  // só o canvas do jogo: HUD, menus, avisos e o overlay de depuração somem
  for (const el of document.body.children) if (el.tagName !== 'CANVAS' && el.tagName !== 'SCRIPT') el.style.display = 'none';
  if (D.ctx.scene.getObjectByName('world-debug')) D.ctx.scene.getObjectByName('world-debug').visible = false;
});

const place = (pos, look) => page.evaluate(([p, l]) => {
  const D = window.__demo;
  const dx = l[0] - p[0], dz = l[2] - p[2], len = Math.hypot(dx, dz) || 1;
  D.tp(p[0] - dx / len * 8, p[2] - dz / len * 8);
  const cy = Math.max(p[1], D.groundHeight(p[0], p[2]) + 2.5);
  const ly = Math.max(l[1], D.groundHeight(l[0], l[2]) + 1.8);
  D.freecam(p[0], cy, p[2], l[0], ly, l[2]);
  D.G.time = 52.5;
  D.settle();
}, [pos, look]);

for (const [name, pos, look] of VIEWS) {
  if (!want(name)) continue;
  await place(pos, look);
  await frames(4);
  await shot(name);
}
if (want('12b_turbulenta_interior')) {
  await page.evaluate(() => {
    const D = window.__demo;
    D.tp(1365, -30);
    D.freecam(1365, 30, -30, 1400, 24, 0);
    D.G.time = 52.5;
    D.settle();
  });
  await frames(4);
  await shot('12b_turbulenta_interior');
}
await browser.close();
console.log(`capturas em ${OUT}`);
