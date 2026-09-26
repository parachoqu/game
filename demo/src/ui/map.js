// Mapa pré-renderizado do relevo e das zonas; minimapa e mapa completo com lugares descobertos.
//
// O fundo agora é o próprio mundo: relevo do heightfield, água pela máscara do rio, terreno pelo
// mapa de biomas e estradas pelas polilinhas do manifesto. Nada de ruído procedural.
import { G } from '../state.js';
import { ZONES } from '../config.js';
import { HALF, LOC, ROUTES, zoneAt, isTurbulentSpace } from '../game/layout.js';
import { regionHeightAt, biomeAt, isWaterCell } from '../world/heightfield.js';
import { REGION } from '../world/runtime-manifest.js';
import { TS } from '../game/turbulent.js';

const SIZE = 512;                      // 2 m por pixel na região de 1.024 m
let base = null;
const px = (x) => (x + HALF) / (HALF * 2) * SIZE;

function hexRgb(h) { const n = parseInt(h.slice(1), 16); return [(n >> 16) & 255, (n >> 8) & 255, n & 255]; }
const ZC = Object.fromEntries(Object.entries(ZONES).map(([k, v]) => [k, hexRgb(v.color)]));

// cor base por bioma da fonte (0 campo · 1 encosta · 2 rocha · 3 mata · 4 planalto seco · 5 água)
const BIOME_RGB = [
  [96, 118, 70], [104, 112, 66], [128, 124, 116], [64, 92, 58], [140, 126, 84], [46, 92, 112],
];

export function buildBaseMap() {
  base = document.createElement('canvas');
  base.width = base.height = SIZE;
  const c = base.getContext('2d');
  const img = c.createImageData(SIZE, SIZE);
  const span = HALF * 2 / SIZE;
  const hi = REGION.height.max, lo = REGION.height.min;
  for (let j = 0; j < SIZE; j++) for (let i = 0; i < SIZE; i++) {
    const x = i * span - HALF, z = j * span - HALF;
    const h = regionHeightAt(x, z);
    const slope = regionHeightAt(x + span, z) - h;
    let r, g, b;
    if (isWaterCell(x, z)) { [r, g, b] = BIOME_RGB[5]; } else {
      const biome = biomeAt(x, z);
      const rgb = BIOME_RGB[biome] || BIOME_RGB[0];
      // altitude clareia, encosta virada para o oeste escurece
      const t = Math.min(1, Math.max(0, (h - lo) / (hi - lo)));
      const lift = 0.78 + t * 0.55;
      const shade = Math.max(-34, Math.min(34, -slope * 9));
      r = rgb[0] * lift + shade; g = rgb[1] * lift + shade; b = rgb[2] * lift + shade;
      const zone = zoneAt(x, z);
      if (zone !== 'protegida') {
        const zc = ZC[zone] || ZC.fronteira;
        r = r * 0.8 + zc[0] * 0.2; g = g * 0.8 + zc[1] * 0.2; b = b * 0.8 + zc[2] * 0.2;
      }
    }
    const k = (j * SIZE + i) * 4;
    img.data[k] = Math.max(0, Math.min(255, r));
    img.data[k + 1] = Math.max(0, Math.min(255, g));
    img.data[k + 2] = Math.max(0, Math.min(255, b));
    img.data[k + 3] = 255;
  }
  c.putImageData(img, 0, 0);

  // estradas pela largura declarada de cada rota
  const road = (pts, w, col) => {
    c.strokeStyle = col; c.lineWidth = w; c.lineJoin = 'round'; c.lineCap = 'round';
    c.beginPath();
    pts.forEach(([x, z], i) => (i ? c.lineTo(px(x), px(z)) : c.moveTo(px(x), px(z))));
    c.stroke();
  };
  for (const r of Object.values(ROUTES)) {
    const w = Math.max(1, r.width / (HALF * 2 / SIZE));
    road(r.pts, w + 1.2, 'rgba(60,46,32,0.35)');
    road(r.pts, w, r.risk === 'alto' ? '#c08a5a' : '#d3a76c');
  }
  // assentamentos
  for (const l of [LOC.vale, LOC.alto, LOC.mercado]) {
    c.fillStyle = 'rgba(236,228,210,0.85)';
    c.beginPath(); c.arc(px(l.x), px(l.z), 6, 0, 7); c.fill();
  }
}

// chave de descoberta → lugar do layout
const PLACES = [
  ['vale', 'Entreposto do Vale'], ['mercado', 'Mercado do Vale'], ['alto', 'Entreposto Alto'],
  ['canteiro', 'Canteiro'], ['passagem', 'Passagem Estreita'], ['bosque', 'Bosque das Forjas'],
  ['acampamento', 'Acampamento das Garras'], ['observatorio', 'Observatório Antigo'],
  ['ermos', 'Ermos Quebrados'], ['vigia', 'Ruína de vigia'], ['mina', 'Boca da Mina'],
  ['caverna', 'Caverna do Oeste'], ['portal', 'Portal Instável'],
];

function markers(c, s, ox, oz, full) {
  const P = G.player;
  const tr = (x, z) => [(px(x) - ox) * s, (px(z) - oz) * s];
  if (full) {
    c.font = '600 12px Fraunces, Georgia, serif'; c.textAlign = 'center';
    for (const [k, name] of PLACES) {
      const l = LOC[k];
      if (!l) continue;
      const known = G.flags['disc_' + k] || k === 'vale' || k === 'mercado';
      const [x, y] = tr(l.x, l.z);
      c.fillStyle = known ? '#f3ead6' : 'rgba(243,234,214,0.45)';
      c.strokeStyle = 'rgba(0,0,0,0.8)'; c.lineWidth = 3;
      const label = known ? name : '?';
      c.strokeText(label, x, y - 10); c.fillText(label, x, y - 10);
    }
  }
  // saco da própria carga
  for (const b of G.lootBags) if (b.own && !isTurbulentSpace(b.x)) {
    const [x, y] = tr(b.x, b.z);
    c.fillStyle = '#f2c86a'; c.strokeStyle = '#000'; c.lineWidth = 1.5;
    c.beginPath(); c.moveTo(x, y - 5); c.lineTo(x + 4, y); c.lineTo(x, y + 5); c.lineTo(x - 4, y); c.closePath(); c.fill(); c.stroke();
  }
  if (TS.portal) {
    const [x, y] = tr(TS.portal.x, TS.portal.z);
    c.strokeStyle = '#c9a8ff'; c.lineWidth = 2; c.beginPath(); c.arc(x, y, 5 + Math.sin(G.time * 4) * 1.5, 0, 7); c.stroke();
  }
  if (G.mount && G.mount.present && !P.mounted && !isTurbulentSpace(G.mount.pos.x)) {
    const [x, y] = tr(G.mount.pos.x, G.mount.pos.z);
    c.fillStyle = '#c8b49a'; c.fillRect(x - 3, y - 3, 6, 6);
  }
  // jogador (com o cone de visão da câmera, que pode estar girada)
  const [x, y] = tr(P.pos.x, P.pos.z);
  const look = Math.atan2(-Math.cos(G.cam.yaw), -Math.sin(G.cam.yaw));
  c.fillStyle = 'rgba(236,228,210,0.32)';
  c.beginPath(); c.moveTo(x, y); c.arc(x, y, 34 * Math.min(1.6, s), look - 0.5, look + 0.5); c.closePath(); c.fill();
  c.save(); c.translate(x, y); c.rotate(-P.yaw + Math.PI);
  c.fillStyle = '#ffffff'; c.strokeStyle = '#0d141b'; c.lineWidth = 1.5;
  c.beginPath(); c.moveTo(0, -7); c.lineTo(5, 5); c.lineTo(0, 2.5); c.lineTo(-5, 5); c.closePath(); c.fill(); c.stroke();
  c.restore();
}

// big: minimapa ampliado (mostra mais área e os nomes dos lugares)
export function drawMinimap(cv, big) {
  if (!base) return;
  const c = cv.getContext('2d');
  const P = G.player;
  const W = cv.width;
  c.clearRect(0, 0, W, W);
  c.save();
  c.beginPath(); c.arc(W / 2, W / 2, W / 2, 0, 7); c.clip();
  if (isTurbulentSpace(P.pos.x)) {
    c.fillStyle = '#1a0f28'; c.fillRect(0, 0, W, W);
    c.fillStyle = '#b89cff'; c.font = '600 12px system-ui'; c.textAlign = 'center'; c.fillText('sem referência', W / 2, W / 2 + 30);
    c.restore(); return;
  }
  // o mundo tem 2,5× a largura do recorte antigo: a escala do minimapa acompanha para o
  // enquadramento continuar cobrindo a mesma distância em metros
  const scale = big ? 1.4 : 2.2;
  const ox = px(P.pos.x) - W / 2 / scale, oz = px(P.pos.z) - W / 2 / scale;
  c.imageSmoothingEnabled = true;
  c.drawImage(base, ox, oz, W / scale, W / scale, 0, 0, W, W);
  markers(c, scale, ox, oz, !!big);
  c.restore();
}

export function drawFullMap(cv) {
  if (!base) return;
  const c = cv.getContext('2d');
  const W = cv.width, s = W / SIZE;
  c.drawImage(base, 0, 0, W, W);
  markers(c, s, 0, 0, true);
}
