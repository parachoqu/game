import { spawn } from 'node:child_process';
import { writeFileSync, mkdirSync } from 'node:fs';
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const TOOLS = dirname(fileURLToPath(import.meta.url));
const DEMO = dirname(TOOLS);
import { resolve } from 'node:path';

const DIST = join(DEMO, 'dist');
const OUT_DIR = process.argv[2] || join(DEMO, 'captures', 'before');
const HTML_FILE = process.argv[3] ? resolve(process.argv[3]) : join(DIST, 'Projeto_Game_Demo.html');

mkdirSync(OUT_DIR, { recursive: true });

const HTTP_PORT = 8790 + Math.floor(Math.random() * 50);
const CDP_PORT = 9310 + Math.floor(Math.random() * 50);

// Simple HTTP server for dist
const server = createServer(async (req, res) => {
  const url = new URL(req.url, `http://localhost:${HTTP_PORT}`);
  let file = url.pathname === '/' || url.pathname.endsWith('Projeto_Game_Demo.html') ? HTML_FILE : join(DIST, url.pathname.slice(1));
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

// Launch headless chrome
const chromeProc = spawn('google-chrome', [
  '--headless=new',
  `--remote-debugging-port=${CDP_PORT}`,
  '--window-size=1100,700',
  '--hide-scrollbars',
  '--disable-gpu-sandbox',
  '--no-sandbox',
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
  if (res.exceptionDetails) {
    console.error('JS eval error:', res.exceptionDetails);
    throw new Error(res.exceptionDetails.text || 'eval failed');
  }
  return res.result?.value;
}

async function main() {
  await sleep(1500); // wait for chrome to start
  console.log('Fetching CDP target on port', CDP_PORT);
  const targetUrl = encodeURIComponent(`http://127.0.0.1:${HTTP_PORT}/Projeto_Game_Demo.html?debug=1`);
  const res = await fetch(`http://127.0.0.1:${CDP_PORT}/json/new?${targetUrl}`, { method: 'PUT' });
  const target = await res.json();
  const wsUrl = target.webSocketDebuggerUrl;
  console.log('Connecting to WebSocket:', wsUrl);

  const ws = new WebSocket(wsUrl);
  await new Promise((resolve) => ws.addEventListener('open', resolve));

  await cdpRequest(ws, 'Page.enable');
  await cdpRequest(ws, 'Runtime.enable');
  await cdpRequest(ws, 'Emulation.setDeviceMetricsOverride', {
    width: 1100, height: 700, deviceScaleFactor: 1, mobile: false,
  });

  console.log('Waiting for world to load and initialize...');
  // Wait until loading finishes and title screen is ready
  let loaded = false;
  for (let i = 0; i < 90; i++) {
    await sleep(500);
    const mode = await evalJs(ws, 'window.__demo ? window.__demo.mode() : null');
    if (mode === 'title' || mode === 'game') {
      console.log(`World ready! Current demo mode: ${mode}`);
      loaded = true;
      break;
    }
    if (i % 6 === 0) console.log(`Waiting for world to load... current mode=${mode}`);
  }

  if (!loaded) throw new Error('Demo failed to reach ready state within 45 seconds');

  // Let title screen render for 1 second
  await sleep(1000);

  // 1. Title Screen
  console.log('Capturing: 01_title_screen');
  let shot = await cdpRequest(ws, 'Page.captureScreenshot', { format: 'png' });
  writeFileSync(join(OUT_DIR, '01_title_screen.png'), Buffer.from(shot.data, 'base64'));

  // Enter game: simulate clicking "Nova trajetória" and start game
  console.log('Entering game...');
  await evalJs(ws, `
    const btnNew = document.getElementById('btn-new');
    if (btnNew) btnNew.click();
    const form = document.getElementById('create-form');
    if (form) form.requestSubmit();
  `);

  // Wait for game mode
  for (let i = 0; i < 30; i++) {
    await sleep(500);
    const m = await evalJs(ws, 'window.__demo ? window.__demo.mode() : null');
    if (m === 'game') {
      console.log('Entered game mode successfully!');
      break;
    }
  }

  // Ensure game is at midday (noon = 12h, sun highest, beautiful lighting) and hide HUD overlays
  await evalJs(ws, `
    if (window.__demo) {
      window.__demo.G.time = 52.5; // midday (12h in 420s cycle with START_HOUR=9)
      window.__demo.G.uiOpen = null;
      const toHide = ['hud', 'minimapa', 'panel', 'dialogo', 'screen-title', 'screen-create', 'screen-loading'];
      for (const id of toHide) {
        const el = document.getElementById(id);
        if (el) { el.hidden = true; el.style.display = 'none'; }
      }
    }
  `);

  // Definition of 12 world viewpoints:
  // [name, cameraPos, lookAtPos, timeOfDayOffset, note]
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

  for (const [name, pos, look] of VIEWS) {
    console.log(`Capturing: ${name}`);
    await evalJs(ws, `(() => {
      const dx = ${look[0]} - ${pos[0]};
      const dz = ${look[2]} - ${pos[2]};
      const len = Math.hypot(dx, dz) || 1;
      const nx = dx / len;
      const nz = dz / len;
      // Coloca o jogador atrás da câmera para não ter mãos/armas na tela
      window.__demo.tp(${pos[0]} - nx * 8, ${pos[2]} - nz * 8);

      const gh = window.__demo.groundHeight ? window.__demo.groundHeight(${pos[0]}, ${pos[2]}) : 15;
      const lgh = window.__demo.groundHeight ? window.__demo.groundHeight(${look[0]}, ${look[2]}) : 15;
      const cy = Math.max(${pos[1]}, gh + 2.5);
      const ly = Math.max(${look[1]}, lgh + 1.8);
      window.__demo.freecam(${pos[0]}, cy, ${pos[2]}, ${look[0]}, ly, ${look[2]});
    })()`);
    await sleep(500);
    shot = await cdpRequest(ws, 'Page.captureScreenshot', { format: 'png' });
    writeFileSync(join(OUT_DIR, `${name}.png`), Buffer.from(shot.data, 'base64'));
  }

  // Also capture inside Turbulent Region for completeness
  console.log('Capturing: 12b_turbulenta_interior');
  await evalJs(ws, `
    window.__demo.tp(1365, -30);
    window.__demo.freecam(1365, 30, -30, 1400, 24, 0);
  `);
  await sleep(500);
  shot = await cdpRequest(ws, 'Page.captureScreenshot', { format: 'png' });
  writeFileSync(join(OUT_DIR, `12b_turbulenta_interior.png`), Buffer.from(shot.data, 'base64'));

  console.log(`All views captured successfully to: ${OUT_DIR}`);

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
