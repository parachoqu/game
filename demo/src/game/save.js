// Salvamento local opcional. O armazenamento pode falhar (janela privada, iframe): tudo em try/catch.
//
// A partir da migração para o mundo do Blender, o save carrega a versão do layout do mundo. Saves
// do recorte antigo (400 × 400 unidades) continuam válidos para inventário, economia, progressão,
// equipamentos e missões, mas a posição guardada por eles não existe mais: ela é descartada e o
// personagem reaparece numa âncora segura. Nada é reiniciado em silêncio.
import { G } from '../state.js';
import { MARKETS } from '../config.js';
import { BOOK } from './book.js';
import { EVT, applyReopenEffects } from './event.js';
import { CAMP } from './camp.js';
import { SKY } from './stars.js';
import { MKT } from './economy.js';
import { setUidSeq } from './inventory.js';
import { isTurbulentSpace, HALF, WORLD_LAYOUT_VERSION } from './layout.js';
import { SHELTER } from './zones.js';
import { groundHeight, waterAt } from '../engine/terrain.js';
import { testPoint } from './collide.js';

const KEY = 'projeto-game-demo-v1';
export const SAVE_VERSION = 2;
export { WORLD_LAYOUT_VERSION };

export function hasSave() {
  try { return !!localStorage.getItem(KEY); } catch { return false; }
}
export function clearSave() { try { localStorage.removeItem(KEY); } catch { /* sem armazenamento */ } }

export function saveGame() {
  const P = G.player;
  if (!P || isTurbulentSpace(P.pos.x)) return;   // não salva dentro da Região Turbulenta
  const data = {
    v: SAVE_VERSION,
    world: { layout: WORLD_LAYOUT_VERSION, pos: [+P.pos.x.toFixed(2), +P.pos.z.toFixed(2)], yaw: +P.yaw.toFixed(3) },
    time: G.time, stats: G.stats, flags: G.flags,
    profile: { name: P.name, origin: P.origin, start: P.start, model: P.model },
    P: { coins: P.coins, trainings: [...P.trainings], inv: P.inv, saddle: P.saddle, storage: P.storage, equip: P.equip, stableBags: P.stableBags || [] },
    book: BOOK,
    evt: EVT, camp: { pop: CAMP.pop, state: CAMP.state },
    sky: { vanished: SKY.vanished, observed: [...SKY.observed], instrumentGiven: SKY.instrumentGiven },
    mkt: MKT, marketBase: { alto: MARKETS.alto.sell.ferramentas, vale: MARKETS.vale.sell.ferramentas },
  };
  try { localStorage.setItem(KEY, JSON.stringify(data)); } catch { /* sem armazenamento: segue sem salvar */ }
}

export function readSave() {
  try {
    const s = localStorage.getItem(KEY);
    return s ? migrateSave(JSON.parse(s)) : null;
  } catch { return null; }
}

// ---------------------------------------------------------------- migração
// Âncora segura de retorno: a praça do entreposto sul, que existe nos dois layouts.
export function safeSpawn() {
  return { x: SHELTER.x, z: SHELTER.z };
}

// Uma posição só é aceita se estiver dentro do novo mundo, fora da Turbulenta, em terreno
// caminhável, fora de um colisor e fora da água.
export function isValidPosition(x, z) {
  if (!Number.isFinite(x) || !Number.isFinite(z)) return false;
  if (isTurbulentSpace(x)) return false;
  const lim = HALF - 12;
  if (Math.abs(x) > lim || Math.abs(z) > lim) return false;
  if (testPoint(x, z, 0.6)) return false;
  if (waterAt(x, z) - groundHeight(x, z) > 0.8) return false;
  return true;
}

// Normaliza qualquer save conhecido para o formato atual. Devolve o objeto com `migrated`
// descrevendo o que mudou, para a interface poder avisar o jogador.
export function migrateSave(d) {
  if (!d || typeof d !== 'object') return null;
  const notes = [];
  const fromVersion = d.v ?? 1;
  const fromLayout = d.world?.layout ?? 1;

  if (!d.world) {
    d.world = { layout: 1, pos: null, yaw: 0 };
    notes.push('save do recorte anterior: sem posição gravada');
  }
  if (fromLayout !== WORLD_LAYOUT_VERSION) {
    // As coordenadas do recorte de 400 × 400 não têm equivalente no mundo de 1.024 × 1.024:
    // qualquer conversão por escala cairia dentro de construções, no rio ou fora do mapa.
    d.world.pos = null;
    d.world.layout = WORLD_LAYOUT_VERSION;
    notes.push(`layout do mundo ${fromLayout} → ${WORLD_LAYOUT_VERSION}: posição reposicionada`);
  }
  if (d.world.pos && !isValidPosition(d.world.pos[0], d.world.pos[1])) {
    d.world.pos = null;
    notes.push('posição gravada inválida no mundo atual');
  }
  if (!d.world.pos) {
    const s = safeSpawn();
    d.world.pos = [s.x, s.z];
    d.world.reposicionado = true;
  }
  d.v = SAVE_VERSION;
  d.migrated = { from: fromVersion, fromLayout, notes };
  return d;
}

// ---------------------------------------------------------------- aplicação
// aplica o estado salvo depois que o jogador foi criado com o mesmo perfil
export function applySave(d) {
  const P = G.player;
  G.time = d.time; G.stats = d.stats || {}; G.flags = d.flags || {};
  Object.assign(P, { coins: d.P.coins, trainings: new Set(d.P.trainings), inv: d.P.inv, saddle: d.P.saddle, storage: d.P.storage, equip: d.P.equip, stableBags: d.P.stableBags });
  let maxUid = 0;
  for (const list of [P.inv, P.saddle, P.storage, Object.values(P.equip), P.stableBags]) for (const e of list) if (e && e.uid) maxUid = Math.max(maxUid, e.uid);
  setUidSeq(maxUid + 1);
  Object.assign(BOOK, d.book);
  Object.assign(EVT, d.evt);
  if (EVT.done) applyReopenEffects();
  CAMP.pop = d.camp.pop; CAMP.state = d.camp.state; CAMP.since = G.time; CAMP.pending = true;
  SKY.vanished = d.sky.vanished; SKY.observed = new Set(d.sky.observed); SKY.instrumentGiven = d.sky.instrumentGiven;
  SKY.next = G.time + 60;
  for (const v of SKY.vanished) G.sky.vanish(v.index, true);
  Object.assign(MKT, d.mkt);
  MARKETS.alto.sell.ferramentas = d.marketBase.alto; MARKETS.vale.sell.ferramentas = d.marketBase.vale;

  const [x, z] = d.world.pos;
  P.pos.set(x, groundHeight(x, z), z);
  if (Number.isFinite(d.world.yaw)) P.yaw = d.world.yaw;
  return d.migrated || null;
}
