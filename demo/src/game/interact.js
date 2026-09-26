// Alvos de interação: serviços, coleta, cargas no chão, viajantes, portal, santuários, saídas e montaria.
// A mesma lista serve à tecla F (mais próximo dentro do raio) e ao clique (pointer.js).
import { G, stat } from '../state.js';
import { ITEMS } from '../config.js';
import { hit } from '../engine/input.js';
import { sfx } from '../engine/audio.js';
import { groundHeight } from '../engine/terrain.js';
import { startChannel, dismount } from './player.js';
import { receive } from './inventory.js';
import { NPCS } from './npcs.js';
import { EVT, recon } from './event.js';
import { observe } from './stars.js';
import { TS, takeShrine, extract } from './turbulent.js';
import { mountAction } from './mount.js';
import { tally } from './book.js';
import { inCombat } from './combat.js';
import { setPrompt, toast } from '../ui/hud.js';
import {
  marketPanel, forgePanel, trainerPanel, storagePanel, stablePanel, boardPanel, canteiroPanel, astronomerPanel,
  travelerPanel, lootPanel, portalPanel,
} from '../ui/panels.js';

const VERB = { minerio: 'Minerar', madeira: 'Cortar madeira', erva: 'Colher erva-lume', cristal: 'Extrair cristal' };
const GATHER_BOOK = {
  minerio: ['Mineração', 'minério de ferro'], madeira: ['Corte de madeira', 'madeira de freixo'],
  erva: ['Colheita de erva-lume', 'erva-lume'], cristal: ['Cristais dos Ermos', 'cristal celeste'],
};

// Cada alvo: {x, z, r (raio de uso), label, run, live? (posição viva), valid? ()}
export function targetsNear(P, maxDist) {
  const list = [];
  const near = (x, z) => Math.hypot(P.pos.x - x, P.pos.z - z) <= maxDist;
  if (TS.inside) {
    for (const s of TS.shrines) if (!s.taken && near(s.x, s.z)) list.push({ x: s.x, z: s.z, r: 3.2, label: 'Recolher fragmento anômalo', run: () => takeShrine(P, s), valid: () => !s.taken });
    for (const e of TS.exits) if (near(e.x, e.z)) list.push({ x: e.x, z: e.z, r: 3.6, label: 'Extrair (4 s, interrompível)', run: () => extract(P, e) });
    return list;
  }
  for (const it of G.interactables) {
    if (!near(it.x, it.z)) continue;
    if (it.kind === 'node') {
      const n = it.data;
      if (n.charges <= 0) { list.push({ x: it.x, z: it.z, r: it.r, label: `${VERB[n.kind]} — esgotado, volta em ${Math.ceil(n.regrowAt - G.time)} s`, run: null }); continue; }
      list.push({ x: it.x, z: it.z, r: it.r, label: `${VERB[n.kind]} (${ITEMS[n.kind].name})`, run: () => gather(P, n), valid: () => n.charges > 0 });
      continue;
    }
    if (it.kind === 'vigia' && EVT.contrib.reconhecimento > 0) continue;
    list.push({ x: it.x, z: it.z, r: it.r, label: it.label, run: () => service(P, it) });
  }
  for (const b of G.lootBags) {
    if (b.items.length && near(b.x, b.z)) list.push({ x: b.x, z: b.z, r: 2.6, label: b.own ? 'Recolher sua carga' : 'Examinar carga no chão', run: () => lootPanel(b), valid: () => G.lootBags.includes(b) && b.items.length > 0, bias: 0.5 });
  }
  for (const n of NPCS) if (n.kind === 'traveler' && near(n.pos.x, n.pos.z)) {
    list.push({ x: n.pos.x, z: n.pos.z, live: n.pos, r: 3, label: `Inspecionar ${n.travel.name}`, run: () => travelerPanel(n) });
  }
  const p = TS.portal;
  if (p && near(p.x, p.z)) list.push({ x: p.x, z: p.z, r: 4.5, y: groundHeight(p.x, p.z) + 3, label: 'Examinar o rasgo violeta', run: () => portalPanel(), valid: () => TS.portal === p, bias: 1 });
  const m = G.mount;
  if (m && m.present && !P.mounted && near(m.pos.x, m.pos.z)) {
    list.push({ x: m.pos.x, z: m.pos.z, live: m.pos, r: 3.5, y: m.pos.y + 1.6, label: 'Montar', run: () => mountAction(P), valid: () => m.present && !P.mounted });
  }
  return list;
}

function service(P, it) {
  switch (it.kind) {
    case 'market': return marketPanel(it.data);
    case 'forge': return forgePanel();
    case 'trainer': return trainerPanel();
    case 'storage': return storagePanel();
    case 'stable': return stablePanel();
    case 'board': return boardPanel();
    case 'canteiro': return canteiroPanel();
    case 'astronomer': return astronomerPanel();
    case 'vigia': return startChannel(P, 'Registrando o movimento', 3, () => recon());
    case 'observatory': return observe(G.nightNow || 0);
  }
}

function gather(P, n) {
  if (inCombat()) { toast('Não dá para coletar em combate.', 'warn'); sfx('deny'); return; }
  startChannel(P, VERB[n.kind], 1.3, () => {
    const q = 1 + n.yieldBonus + (Math.random() < 0.35 ? 1 : 0);
    const got = receive(P, n.kind, q);
    if (!got) { toast('Sem capacidade de carga. Guarde itens ou use alforjes.', 'warn'); sfx('deny'); return; }
    n.charges--;
    if (n.charges <= 0) { n.regrowAt = G.time + 55; n.mesh.scale.setScalar(0.55); }
    sfx('gather');
    toast(`+${got} ${ITEMS[n.kind].name}${got < q ? ' (sem espaço para o resto)' : ''}`, 'item', 2400);
    stat('gather_' + n.kind, got);
    const [title, noun] = GATHER_BOOK[n.kind];
    tally('coleta-' + n.kind, 'feitos', 'trabalho', (c) => ({ title, text: `${c} unidade${c > 1 ? 's' : ''} de ${noun} coletada${c > 1 ? 's' : ''} com as próprias mãos.` }), got);
  });
}

// alvo mais próximo dentro do próprio raio de uso
export function nearestInRange(P) {
  let best = null, bd = Infinity;
  for (const t of targetsNear(P, 8)) {
    const d = Math.hypot(P.pos.x - t.x, P.pos.z - t.z) - (t.bias || 0);
    if (d <= t.r && d < bd) { bd = d; best = t; }
  }
  return best;
}

export function updateInteract() {
  const P = G.player;
  for (const n of G.interactables) if (n.kind === 'node' && n.data.charges <= 0 && G.time >= n.data.regrowAt) { n.data.charges = n.data.max; n.data.mesh.scale.setScalar(1); }
  if (G.uiOpen || G.cinematic || P.state === 'down' || P.state === 'dead' || P.state === 'channel') { setPrompt(null); return; }
  const best = nearestInRange(P);
  const hov = G.hover;
  if (best) setPrompt(`Clique ou <kbd>F</kbd> ${best.label}`);
  else if (hov && hov.kind === 'use') setPrompt(`Clique: ${hov.t.label}`);
  else if (hov && hov.kind === 'enemy') setPrompt(`Clique: atacar ${hov.e.faction === 'shadow' ? 'figura desconhecida' : hov.e.cfg.name.toLowerCase()}`);
  else if (P.mounted) setPrompt('<kbd>F</kbd> Desmontar');
  else setPrompt(null);
  if (hit('KeyF') && P.state === 'free') {
    if (best && best.run) { P.cmd = null; best.run(); }
    else if (P.mounted) dismount(P);
  }
}
