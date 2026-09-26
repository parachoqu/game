// Estado global compartilhado entre módulos + barramento de eventos simples.
import { DAY_LENGTH, START_HOUR } from './config.js';

export const G = {
  time: 0,            // segundos de jogo desde o início
  running: false,
  uiOpen: null,       // nome do painel aberto (bloqueia ações de combate)
  debug: /[?&]debug=1/.test(location.search),
  // preenchidos em main.js
  three: null, scene: null, camera: null,
  player: null, enemies: [], projectiles: [], lootBags: [], interactables: [],
  flags: {},          // descobertas e marcos já registrados
  stats: {},          // contadores usados pelo Livro e pelas intenções
};

const listeners = {};
export function on(ev, fn) { (listeners[ev] ||= []).push(fn); }
export function emit(ev, data) { const l = listeners[ev]; if (l) for (const fn of l) fn(data); }

export function stat(key, n = 1) { G.stats[key] = (G.stats[key] || 0) + n; return G.stats[key]; }

export function clock(time = G.time) {
  const total = time + (START_HOUR / 24) * DAY_LENGTH;
  const day = Math.floor(total / DAY_LENGTH) + 1;
  const hour = ((total % DAY_LENGTH) / DAY_LENGTH) * 24;
  return { day, hour, label: `Dia ${day} · ${String(Math.floor(hour)).padStart(2, '0')}h` };
}

export const rand = (a, b) => a + Math.random() * (b - a);
export const randi = (a, b) => Math.floor(rand(a, b + 1));
export const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
export const lerp = (a, b, t) => a + (b - a) * t;
export const smooth = (e0, e1, x) => { const t = clamp((x - e0) / (e1 - e0), 0, 1); return t * t * (3 - 2 * t); };
export const dist2 = (ax, az, bx, bz) => { const dx = ax - bx, dz = az - bz; return Math.sqrt(dx * dx + dz * dz); };
export const angleDiff = (a, b) => { let d = a - b; while (d > Math.PI) d -= Math.PI * 2; while (d < -Math.PI) d += Math.PI * 2; return d; };
export const pick = (arr) => arr[Math.floor(Math.random() * arr.length)];
