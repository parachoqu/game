// Mercados locais, fabricação, treinamento e reparo (cap. 05 e 08).
import { G, stat, emit } from '../state.js';
import { ITEMS, MARKETS, TRAININGS, RECIPES, MOUNT_PRICE, RESTART_KIT_PRICE } from '../config.js';
import { count, removeFrom, receive, addTo, haveAll, consume, isGear, makeGear } from './inventory.js';
import { first, tally } from './book.js';
import { sfx } from '../engine/audio.js';
import { toast } from '../ui/hud.js';

export const MKT = { vale: { demand: {} }, alto: { demand: {} } };

export function sellPrice(mk, id, entry) {
  const base = MARKETS[mk].sell[id];
  if (base == null) return null;
  const d = MKT[mk].demand[id] ?? 1;
  const condK = entry && entry.uid ? Math.max(0.3, entry.cond / 100) : 1;
  return Math.max(1, Math.round(base * d * condK));
}
export const buyPrice = (mk, id) => MARKETS[mk].buy[id] ?? null;
export const demandOf = (mk, id) => MKT[mk].demand[id] ?? 1;

export function updateMarkets(dt) {
  for (const mk of Object.values(MKT)) for (const id in mk.demand) {
    mk.demand[id] = Math.min(1, mk.demand[id] + 0.007 * dt);
    if (mk.demand[id] >= 1) delete mk.demand[id];
  }
}

export function sell(P, mk, id, qty = 1) {
  let earned = 0, sold = 0;
  for (let i = 0; i < qty; i++) {
    const entry = P.inv.find((e) => e.id === id);
    if (!entry) break;
    const price = sellPrice(mk, id, entry);
    if (price == null) break;
    if (entry.uid) P.inv.splice(P.inv.indexOf(entry), 1); else removeFrom(P.inv, id, 1);
    earned += price; sold++;
    MKT[mk].demand[id] = Math.max(0.35, (MKT[mk].demand[id] ?? 1) - 0.055);
  }
  if (!sold) return 0;
  P.coins += earned;
  stat('earned', earned); stat(`sold_${mk}_${id}`, sold);
  sfx('coin');
  toast(`Vendeu ${sold}× ${ITEMS[id].name} por ${earned} moedas.`, 'coin');
  if (mk === 'alto' && id === 'ferramentas') {
    tally('entregas-alto', 'historia', 'comercio', (n, d) => ({
      title: n > 1 ? 'Temporada de entregas' : 'Primeira venda no Entreposto Alto',
      text: n > 1 ? `Levou ferramentas ao Entreposto Alto: ${n} unidades vendidas, ${d.total} moedas no total.` : `Vendeu ferramentas no Entreposto Alto, onde a Passagem instável deixou o abastecimento escasso.`,
    }), sold, { total: (G.stats['earned_alto_ferr'] = (G.stats['earned_alto_ferr'] || 0) + earned) });
    emit('trade', { mk, id, sold });
  }
  if (id === 'cristal' || id === 'fragmento') first('venda-' + id, 'historia', 'comercio', `Vendeu ${ITEMS[id].name.toLowerCase()}`, `Negociou ${ITEMS[id].name.toLowerCase()} no ${MARKETS[mk].name}. Materiais de risco encontram compradores próprios.`);
  emit('sold', { mk, id, sold, earned });
  return earned;
}

export function buy(P, mk, id, qty = 1) {
  const price = buyPrice(mk, id);
  if (price == null) return 0;
  let n = 0;
  for (let i = 0; i < qty; i++) {
    if (P.coins < price) { toast('Moedas insuficientes.', 'warn'); sfx('deny'); break; }
    if (isGear(id)) { addTo(P.inv, id, 1); n++; P.coins -= price; continue; }
    if (!receive(P, id, 1)) { toast('Sem capacidade de carga para isso.', 'warn'); sfx('deny'); break; }
    P.coins -= price; n++;
  }
  if (n) { sfx('coin'); toast(`Comprou ${n}× ${ITEMS[id].name}.`, 'coin'); stat('spent', n * price); }
  return n;
}

export function buyMount(P) {
  if (P.equip.mount) { toast('Você já possui uma montaria.', 'info'); return; }
  if (P.coins < MOUNT_PRICE) { toast('Moedas insuficientes.', 'warn'); sfx('deny'); return; }
  P.coins -= MOUNT_PRICE; P.equip.mount = makeGear('montaria');
  sfx('coin');
  toast('Cavalo de carga adquirido. Pressione R para chamá-lo.', 'item');
  first('compra-montaria', 'historia', 'comercio', 'Montaria de carga', 'Comprou um cavalo de carga no estábulo do Vale. Mais capacidade, mais exposição.');
}
export function restartKit(P) {
  const price = P.coins >= RESTART_KIT_PRICE ? RESTART_KIT_PRICE : 0;
  P.coins -= price;
  addTo(P.inv, 'martelo', 1, 70); addTo(P.inv, 'pocao', 1);
  toast(price ? `Kit de recomeço por ${price} moedas: martelo de oficina e uma poção.` : 'Kit de recomeço cedido pelo armazém.', 'item');
  stat('kits');
  first('recomeco', 'historia', 'trabalho', 'Recomeço', 'Recebeu um kit básico no Vale para retomar o trabalho depois de uma perda.');
}

export function learn(P, id) {
  const t = TRAININGS[id];
  if (P.trainings.has(id)) return;
  if (P.coins < t.cost) { toast('Moedas insuficientes.', 'warn'); sfx('deny'); return; }
  P.coins -= t.cost; P.trainings.add(id);
  sfx('book');
  toast(`Aprendeu ${t.name}.`, 'item');
  first('treino-' + id, 'historia', 'trabalho', `Aprendeu ${t.name}`, `Estudou com o instrutor do Vale. ${t.desc} O conhecimento acompanha a trajetória, mesmo após perdas.`);
}

export function craftCheck(P, r, useStorage) {
  if (r.req && !P.trainings.has(r.req)) return `Requer ${TRAININGS[r.req].name}`;
  if (!haveAll(P, r.in, useStorage)) return 'Materiais insuficientes';
  return null;
}
export function craft(P, r, useStorage) {
  if (craftCheck(P, r, useStorage)) return false;
  consume(P, r.in, useStorage);
  if (isGear(r.out)) addTo(P.inv, r.out, r.qty, 100);
  else {
    const got = receive(P, r.out, r.qty);
    if (got < r.qty) addTo(P.storage, r.out, r.qty - got);
  }
  sfx('craft');
  stat('crafted'); stat('crafted_' + r.out, r.qty);
  const counts = {};
  for (const k of Object.keys(G.stats)) if (k.startsWith('crafted_')) counts[k.slice(8)] = G.stats[k];
  tally('oficio', 'feitos', 'trabalho', (n, d) => {
    const parts = Object.entries(d).map(([id, q]) => `${q} ${ITEMS[id].name.toLowerCase()}`);
    return { title: n >= 10 ? 'Ofício reconhecido na bancada' : 'Trabalho de bancada', text: `Fabricou ${parts.join(', ')} na bancada do Vale.` };
  }, 1, counts);
  emit('crafted', r);
  return true;
}

export function repairCost(g) {
  const missing = 100 - g.cond;
  return { coins: Math.ceil(missing / 10) * 2, lingote: g.cond < 40 ? 1 : 0 };
}
export function repair(P, slot) {
  const g = P.equip[slot];
  if (!g || g.cond >= 100) return;
  const c = repairCost(g);
  if (P.coins < c.coins) { toast('Moedas insuficientes para o reparo.', 'warn'); sfx('deny'); return; }
  if (c.lingote && count(P.inv, 'lingote') + count(P.storage, 'lingote') < 1) { toast('O reparo de um item muito gasto consome 1 lingote.', 'warn'); sfx('deny'); return; }
  P.coins -= c.coins;
  if (c.lingote && !removeFrom(P.inv, 'lingote', 1)) removeFrom(P.storage, 'lingote', 1);
  g.cond = 100; sfx('craft');
  toast(`${ITEMS[g.id].name} reparado.`, 'item');
  stat('repairs');
  tally('reparos', 'feitos', 'trabalho', (n) => ({ title: 'Reparos na bancada', text: `Reparou equipamentos ${n} ${n > 1 ? 'vezes' : 'vez'}. Desgaste virou trabalho de oficina.` }));
}
