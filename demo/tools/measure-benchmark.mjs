import { spawn } from 'node:child_process';
import { writeFileSync } from 'node:fs';
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const TOOLS = dirname(fileURLToPath(import.meta.url));
const DEMO = dirname(TOOLS);
const DIST = join(DEMO, 'dist');
const OUT_FILE = process.argv[2] || join(DEMO, 'benchmark-baseline.json');

const HTTP_PORT = 8795;
const CDP_PORT = 9325;

const server = createServer(async (req, res) => {
  const url = new URL(req.url, `http://localhost:${HTTP_PORT}`);
  let file = join(DIST, url.pathname === '/' ? 'Projeto_Game_Demo.html' : url.pathname.slice(1));
  try {
    const data = await readFile(file);
    const ext = file.split('.').pop();
    const mime = ext === 'html' ? 'text/html' : ext === 'js' ? 'application/javascript' : 'application/octet-stream';
    res.writeHead(200, { 'Content-Type': mime });
    res.end(data);
  } catch (err) {
    res.writeHead(404);
    res.end('Not found');
  }
});

server.listen(HTTP_PORT, '127.0.0.1');

const chromeProc = spawn('google-chrome', [
  '--headless=new',
  `--remote-debugging-port=${CDP_PORT}`,
  '--window-size=1100,700',
  '--hide-scrollbars',
  '--disable-gpu-sandbox',
  '--no-sandbox',
  '--disable-background-timer-throttling',
  '--disable-renderer-backgrounding',
  '--disable-backgrounding-occluded-windows',
  'about:blank',
], { stdio: 'ignore' });

async function sleep(ms) { return new Promise((r) => setTimeout(r, ms)); }

async function cdpRequest(ws, method, params = {}) {
  const id = Math.floor(Math.random() * 1e9);
  return new Promise((resolve, reject) => {
    const handler = (event) => {
      const msg = JSON.parse(event.data);
      if (msg.id === id) {
        ws.removeEventListener('message', handler);
        if (msg.error) reject(new Error(msg.error.message));
        else resolve(msg.result);
      }
    };
    ws.addEventListener('message', handler);
    ws.send(JSON.stringify({ id, method, params }));
  });
}

async function evalJs(ws, expr) {
  const res = await cdpRequest(ws, 'Runtime.evaluate', {
    expression: expr,
    awaitPromise: true,
    returnByValue: true,
  });
  return res.result?.value;
}

async function main() {
  await sleep(1500);
  const targetUrl = encodeURIComponent(`http://127.0.0.1:${HTTP_PORT}/Projeto_Game_Demo.html?debug=1`);
  const res = await fetch(`http://127.0.0.1:${CDP_PORT}/json/new?${targetUrl}`, { method: 'PUT' });
  const target = await res.json();
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((resolve) => ws.addEventListener('open', resolve));

  await cdpRequest(ws, 'Page.enable');
  await cdpRequest(ws, 'Runtime.enable');
  await cdpRequest(ws, 'Emulation.setDeviceMetricsOverride', {
    width: 1100, height: 700, deviceScaleFactor: 1, mobile: false,
  });

  let loaded = false;
  for (let i = 0; i < 90; i++) {
    await sleep(500);
    const mode = await evalJs(ws, 'window.__demo ? window.__demo.mode() : null');
    if (mode === 'title' || mode === 'game') {
      loaded = true;
      break;
    }
  }
  if (!loaded) throw new Error('Demo failed to reach title state');

  // Enter game
  await evalJs(ws, `
    const btnNew = document.getElementById('btn-new');
    if (btnNew) btnNew.click();
    const form = document.getElementById('create-form');
    if (form) form.requestSubmit();
  `);

  for (let i = 0; i < 30; i++) {
    await sleep(500);
    const m = await evalJs(ws, 'window.__demo ? window.__demo.mode() : null');
    if (m === 'game') break;
  }

  await evalJs(ws, `
    if (window.__demo) {
      window.__demo.G.time = 52.5;
      window.__demo.G.uiOpen = null;
      const toHide = ['hud', 'minimapa', 'panel', 'dialogo', 'screen-title', 'screen-create', 'screen-loading'];
      for (const id of toHide) {
        const el = document.getElementById(id);
        if (el) { el.hidden = true; el.style.display = 'none'; }
      }
    }
  `);
  await sleep(1000);

  const POINTS = [
    ['mercado', [40, 24, 405], [40, 18, 375]],
    ['ponte_principal', [-15, 23, 120], [0, 18, 145]],
    ['garganta', [-25, 30, -50], [-25, 24, -95]],
    ['bosque', [-70, 30, -80], [-120, 24, -80]],
    ['entreposto_norte', [35, 62, -395], [35, 58, -435]],
    ['ermos', [-315, 102, -335], [-360, 98, -375]],
  ];

  const TIERS = ['alta', 'media', 'baixa'];
  const fullResults = {};

  for (const tier of TIERS) {
    console.log(`\n=== Medindo Qualidade: ${tier.toUpperCase()} ===`);
    await evalJs(ws, `window.__demo.applyQuality('${tier}');`);
    await sleep(500);

    const tierResults = {};
    for (const [name, pos, look] of POINTS) {
      console.log(`Benchmarking [${tier}] ${name}...`);
      const metrics = await evalJs(ws, `
        (async () => {
          const dx = ${look[0]} - ${pos[0]};
          const dz = ${look[2]} - ${pos[2]};
          const len = Math.hypot(dx, dz) || 1;
          window.__demo.tp(${pos[0]} - (dx/len) * 8, ${pos[2]} - (dz/len) * 8);

          const gh = window.__demo.groundHeight ? window.__demo.groundHeight(${pos[0]}, ${pos[2]}) : 15;
          const lgh = window.__demo.groundHeight ? window.__demo.groundHeight(${look[0]}, ${look[2]}) : 15;
          const cy = Math.max(${pos[1]}, gh + 2.5);
          const ly = Math.max(${look[1]}, lgh + 1.8);
          window.__demo.freecam(${pos[0]}, cy, ${pos[2]}, ${look[0]}, ly, ${look[2]});

          const ctx = window.__demo.ctx;
          const renderer = ctx.renderer;
          const gl = renderer.getContext();
          const pixel = new Uint8Array(4);

          // 2 frames warmup on headless CPU
          for (let i = 0; i < 2; i++) {
            ctx.render();
            gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
          }

          // Pure scene render triangles and draw calls
          renderer.render(ctx.scene, ctx.camera);
          const tris = renderer.info.render.triangles;
          const calls = renderer.info.render.calls;

          // 4 frames measurement
          const times = [];
          for (let i = 0; i < 4; i++) {
            const t0 = performance.now();
            ctx.render();
            gl.readPixels(0, 0, 1, 1, gl.RGBA, gl.UNSIGNED_BYTE, pixel);
            times.push(performance.now() - t0);
          }
          times.sort((a, b) => a - b);
          const median = times[Math.floor(times.length * 0.5)];
          const p95 = times[Math.floor(times.length * 0.95)];
          return { median: Math.round(median * 100) / 100, p95: Math.round(p95 * 100) / 100, tris, calls };
        })()
      `);
      tierResults[name] = metrics;
      console.log(`  ${name}: median=${metrics.median}ms, p95=${metrics.p95}ms, tris=${metrics.tris}, calls=${metrics.calls}`);
    }
    fullResults[tier] = tierResults;
  }

  // Restore Alta
  await evalJs(ws, `window.__demo.applyQuality('alta');`);

  writeFileSync(OUT_FILE, JSON.stringify(fullResults, null, 2));
  console.log('\nSaved benchmark results to:', OUT_FILE);

  ws.close();
  chromeProc.kill();
  server.close();
  process.exit(0);
}

main().catch((err) => {
  console.error(err);
  chromeProc.kill();
  server.close();
  process.exit(1);
});
