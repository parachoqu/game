// Painéis modais. Cada painel: {id, cls, render(), actions}. Ações via data-act/data-arg.
import { G, emit, stat } from '../state.js';
import { ITEMS, MARKETS, TRAININGS, RECIPES, ZONES, MOUNT_PRICE, RESTART_KIT_PRICE, SKILLS, WEAPONS } from '../config.js';
import {
  count, weightOf, capacity, saddleCap, loadState, addEntry, removeFrom, addTo, receive, saddleReachable, isGear, OVER_LIMIT,
} from '../game/inventory.js';
import { sellPrice, buyPrice, demandOf, sell, buy, buyMount, restartKit, learn, craft, craftCheck, repair, repairCost } from '../game/economy.js';
import { EVT, deliver, share } from '../game/event.js';
import { CAMP, CAMP_LABEL } from '../game/camp.js';
import { SKY, phaseOf, giveInstrument } from '../game/stars.js';
import { TS, canEnter, enter, DURATION } from '../game/turbulent.js';
import { inCombat } from '../game/combat.js';
import { respawn } from '../game/zones.js';
import { weaponFamily, CAM_PREFS, setCamPref } from '../game/player.js';
import { first } from '../game/book.js';
import { drawFullMap } from './map.js';
import { audio, setMuted, sfx } from '../engine/audio.js';
import { toast } from './hud.js';
import { LEVELS, QUALITY } from '../engine/quality.js';
import { KEYGUARD, setKeyguard, protectKeyboard } from '../engine/keyguard.js';

const el = () => document.getElementById('panel');
let cur = null;

export function openPanel(def) {
  cur = def;
  G.uiOpen = def.id;
  const p = el();
  p.className = def.cls || '';
  p.hidden = false;
  draw();
  sfx('ui');
}
function draw() {
  const p = el();
  const st = p.querySelector('.pb')?.scrollTop || 0;
  p.innerHTML = cur.render();
  const pb = p.querySelector('.pb'); if (pb) pb.scrollTop = st;
  if (cur.after) cur.after(p);
}
export function refreshPanel() { if (cur) draw(); }
export function closePanel() {
  if (!cur) return;
  const c = cur; cur = null;
  const p = el(); p.hidden = true; p.innerHTML = '';
  G.uiOpen = null;
  if (c.onClose) c.onClose();
}
export const panelOpen = () => cur && cur.id;

export function initPanels() {
  el().addEventListener('click', (ev) => {
    const b = ev.target.closest('[data-act]');
    if (!b || !cur) return;
    const act = b.dataset.act;
    if (act === 'close') { closePanel(); return; }
    const fn = cur.actions && cur.actions[act];
    if (fn) { const keep = cur; fn(b.dataset.arg, b); if (cur === keep) draw(); }
  });
}

// ---------- utilidades de marcação ----------
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));
const head = (title, sub, closeLabel = 'Fechar') => `<div class="ph"><div><h2>${title}</h2>${sub ? `<p>${sub}</p>` : ''}</div><button type="button" class="btn small x" data-act="close">${closeLabel} <kbd>Esc</kbd></button></div><div class="ribbon"></div>`;
const glyph = (id) => `<span class="g" style="--gc:${ITEMS[id].color}">${ITEMS[id].glyph}</span>`;
const kg = (n) => `${(Math.round(n * 10) / 10).toLocaleString('pt-BR')} kg`;
function row(id, name, sub, price, acts, dim, down) {
  return `<div class="row${dim ? ' dim' : ''}">${glyph(id)}<div class="n">${name}${sub ? `<small>${sub}</small>` : ''}</div><span class="p${down ? ' down' : ''}">${price ?? ''}</span><div class="acts">${acts || ''}</div></div>`;
}
const btn = (act, arg, label, opts = {}) => `<button type="button" class="btn small${opts.primary ? ' primary' : ''}" data-act="${act}" data-arg="${arg ?? ''}"${opts.disabled ? ' disabled' : ''}>${label}</button>`;
const loadLine = (P) => `Bolsa ${kg(weightOf(P.inv))} de ${capacity(P)} kg · ${P.coins} moedas`;
const condTxt = (g) => `${Math.round(g.cond)}% de condição`;
const entryName = (e) => ITEMS[e.id].name + (e.uid ? ` (${Math.round(e.cond)}%)` : '');

// ---------- mercado ----------
export function marketPanel(mk) {
  const P = G.player;
  const other = mk === 'vale' ? 'alto' : 'vale';
  openPanel({
    id: 'market', cls: 'wide',
    render() {
      const m = MARKETS[mk];
      const buyRows = Object.keys(m.buy).map((id) => row(id, ITEMS[id].name, `${kg(ITEMS[id].w)} · ${ITEMS[id].desc}`, `${buyPrice(mk, id)} m`, btn('buy', id, 'Comprar', { disabled: P.coins < buyPrice(mk, id) }))).join('');
      const ids = Object.keys(m.sell);
      const sellRows = ids.map((id) => {
        const n = count(P.inv, id);
        const d = demandOf(mk, id);
        const e = P.inv.find((x) => x.id === id);
        const price = sellPrice(mk, id, e);
        const trend = d < 0.97 ? ` · procura baixa (${Math.round(d * 100)}%)` : '';
        const otherP = MARKETS[other].sell[id];
        return row(id, ITEMS[id].name, `${n ? `${n} na bolsa` : 'nada na bolsa'}${trend}${otherP ? ` · no ${other === 'alto' ? 'Alto' : 'Vale'}: ${otherP} m` : ''}`, `${price} m`,
          n ? btn('sell', id, 'Vender 1') + btn('sellall', id, 'Todos') : '', !n, d < 0.97);
      }).join('');
      return head(m.name, `${loadLine(P)}. Compra e retirada acontecem aqui; o que está em outro entreposto fica lá.`) +
        `<div class="pb"><div class="cols"><section><h3>O mercado vende</h3><div class="rows">${buyRows}</div></section><section><h3>O mercado compra</h3><div class="rows">${sellRows}</div>
        <p class="note">Cada venda reduz a procura local; ela se recupera com o tempo. Preços do outro entreposto são apenas consulta.</p></section></div></div>`;
    },
    actions: {
      buy: (id) => buy(P, mk, id, 1),
      sell: (id) => sell(P, mk, id, 1),
      sellall: (id) => sell(P, mk, id, count(P.inv, id)),
    },
  });
}

// ---------- bancada ----------
export function forgePanel() {
  const P = G.player;
  let useStorage = true;
  openPanel({
    id: 'forge', cls: 'wide',
    render() {
      const rec = RECIPES.map((r, i) => {
        const why = craftCheck(P, r, useStorage);
        const ins = Object.entries(r.in).map(([id, n]) => {
          const have = count(P.inv, id) + (useStorage ? count(P.storage, id) : 0);
          return `<span style="color:${have >= n ? 'var(--bone)' : 'var(--bad)'}">${n}× ${ITEMS[id].name.toLowerCase()} (${have})</span>`;
        }).join(', ');
        return row(r.out, `${r.qty > 1 ? r.qty + '× ' : ''}${ITEMS[r.out].name}`, `${r.note}${r.req ? ` · ${TRAININGS[r.req].name}` : ''} — ${ins}`, why ? `<span style="color:var(--muted);font-family:var(--body)">${why}</span>` : '', btn('craft', i, 'Fabricar', { disabled: !!why, primary: !why }), !!why);
      }).join('');
      const rep = ['weapon', 'armor'].map((s) => {
        const g = P.equip[s];
        if (!g) return '';
        const c = repairCost(g);
        return row(g.id, ITEMS[g.id].name, `${condTxt(g)}${g.cond <= 0 ? ' · inutilizado' : ''}`, g.cond >= 100 ? 'intacto' : `${c.coins} m${c.lingote ? ' + 1 lingote' : ''}`, g.cond < 100 ? btn('repair', s, 'Reparar') : '');
      }).join('') || '<p class="note">Nenhum equipamento equipado para reparar.</p>';
      return head('Bancada do Vale', `${loadLine(P)}. Fabricar consome materiais; reparar consome moedas e, se o item estiver muito gasto, um lingote.`) +
        `<div class="pb"><div class="cols"><section><h3>Receitas</h3><div class="rows">${rec}</div>
        <p class="note"><label><input type="checkbox" id="use-storage" data-act="toggleStorage" ${useStorage ? 'checked' : ''}> Usar materiais do armazém do Vale</label></p></section>
        <section><h3>Reparo do conjunto equipado</h3><div class="rows">${rep}</div>
        <div class="callout" style="margin-top:12px">Desgaste não destrói o item: em 0% ele fica inutilizado até o reparo. Derrotas em áreas perigosas seguem outra regra.</div></section></div></div>`;
    },
    actions: {
      craft: (i) => craft(P, RECIPES[+i], useStorage),
      repair: (s) => repair(P, s),
      toggleStorage: () => { useStorage = !useStorage; },
    },
  });
}

// ---------- instrutor ----------
const TRAIN_ICON = {
  espadachim: 'espada', arqueiro: 'arco', artesao: 'martelo', alquimista: 'pocao',
  lutador: 'manoplas', combatente_haste: 'lanca', concussao_corte: 'machado', assassino: 'adaga',
  magia_elemental: 'cajado_gelo', magia_natural: 'fetiche_metamorfose', artes_espirituais: 'tomo_sagrado',
};

const TRAIN_GROUPS = [
  { title: 'Armas e Famílias de Combate (Cap. 21)', ids: ['espadachim', 'lutador', 'concussao_corte', 'combatente_haste', 'assassino', 'arqueiro'] },
  { title: 'Escolas e Tradições de Magia (Cap. 22)', ids: ['magia_elemental', 'magia_natural', 'artes_espirituais'] },
  { title: 'Ofícios e Produção (Cap. 05)', ids: ['artesao', 'alquimista'] },
];

export function trainerPanel() {
  const P = G.player;
  openPanel({
    id: 'trainer', cls: 'wide',
    render() {
      const renderGroup = (g) => {
        const rows = g.ids.map((id) => {
          const t = TRAININGS[id];
          if (!t) return '';
          const icon = TRAIN_ICON[id] || 'espada';
          const learned = P.trainings.has(id);
          return row(icon, t.name, t.desc, learned ? 'aprendido' : `${t.cost} m`, learned ? '' : btn('learn', id, 'Aprender', { disabled: P.coins < t.cost, primary: P.coins >= t.cost }), learned);
        }).join('');
        return `<section><h3>${g.title}</h3><div class="rows">${rows}</div></section>`;
      };
      const sections = TRAIN_GROUPS.map(renderGroup).join('');
      const noWeapon = !P.equip.weapon && !P.inv.some((e) => ITEMS[e.id].kind === 'weapon');
      return head('Instrutor do Vale', 'Aprender libera o uso de famílias de armas, técnicas e tradições mágicas. Sem classes obrigatórias.') +
        `<div class="pb stack">${sections}
        <div class="callout">Conhecimentos, receitas e o Livro nunca entram no saque. Uma derrota retira bens elegíveis na zona, mas preserva sua trajetória.</div>
        ${noWeapon ? `<div class="callout" style="--cc:var(--warn)"><b>Sem arma?</b> O armazém cede um kit básico para recomeçar: martelo de oficina e uma poção. ${btn('kit', '', P.coins >= RESTART_KIT_PRICE ? `Kit de recomeço — ${RESTART_KIT_PRICE} m` : 'Kit de recomeço — cedido')}</div>` : ''}</div>`;
    },
    actions: { learn: (id) => learn(P, id), kit: () => restartKit(P) },
  });
}

// ---------- armazém ----------
export function storagePanel() {
  const P = G.player;
  openPanel({
    id: 'storage', cls: 'wide',
    render() {
      const list = (arr, act, label) => arr.length ? arr.map((e, i) => row(e.id, entryName(e), `${e.qty || 1} · ${kg(ITEMS[e.id].w * (e.qty || 1))}`, '', btn(act, i, label) + (e.qty > 1 ? btn(act + 'All', i, 'Todos') : ''))).join('') : '<p class="note">Vazio.</p>';
      return head('Armazém do Vale', `${loadLine(P)}. Itens guardados aqui não entram em nenhuma regra de saque.`) +
        `<div class="pb"><div class="cols"><section><h3>Bolsa</h3><div class="rows">${list(P.inv, 'store', 'Guardar')}</div>${P.inv.length ? `<p>${btn('storeMats', '', 'Guardar todos os materiais')}</p>` : ''}</section>
        <section><h3>Guardado</h3><div class="rows">${list(P.storage, 'take', 'Retirar')}</div></section></div></div>`;
    },
    actions: {
      store: (i) => moveOne(P.inv, P.storage, +i, 1),
      storeAll: (i) => moveOne(P.inv, P.storage, +i, Infinity),
      storeMats: () => { for (let i = P.inv.length - 1; i >= 0; i--) if (!P.inv[i].uid && P.inv[i].id !== 'pocao') moveOne(P.inv, P.storage, i, Infinity); },
      take: (i) => takeOne(P, +i, 1),
      takeAll: (i) => takeOne(P, +i, Infinity),
    },
  });
}
function moveOne(from, to, i, n) {
  const e = from[i]; if (!e) return;
  if (e.uid) { from.splice(i, 1); to.push(e); return; }
  const q = Math.min(n, e.qty);
  e.qty -= q; if (e.qty <= 0) from.splice(i, 1);
  addTo(to, e.id, q);
}
function takeOne(P, i, n) {
  const e = P.storage[i]; if (!e) return;
  if (e.uid) { P.storage.splice(i, 1); P.inv.push(e); return; }
  const q = Math.min(n, e.qty);
  let got = 0;
  for (let k = 0; k < q; k++) { if (weightOf(P.inv) + ITEMS[e.id].w > capacity(P) * OVER_LIMIT) break; addTo(P.inv, e.id, 1); got++; }
  if (!got) toast('Sem capacidade na bolsa.', 'warn');
  e.qty -= got; if (e.qty <= 0) P.storage.splice(P.storage.indexOf(e), 1);
}

// ---------- estábulo ----------
export function stablePanel() {
  const P = G.player;
  openPanel({
    id: 'stable', cls: 'narrow',
    render() {
      const bags = P.stableBags || [];
      return head('Estábulo do Vale', 'Montarias ampliam decisões de transporte; não são fuga instantânea.') +
        `<div class="pb stack">${row('montaria', ITEMS.montaria.name, '+60 kg em alforjes · mais rápido em estrada · ser atingido derruba da sela', P.equip.mount ? 'possui' : `${MOUNT_PRICE} m`, P.equip.mount ? '' : btn('buy', '', 'Comprar', { disabled: P.coins < MOUNT_PRICE, primary: true }), !!P.equip.mount)}
        <dl class="kv"><dt>Chamar e montar</dt><dd>Tecla R, fora de combate. Leva alguns segundos.</dd><dt>Alforjes</dt><dd>Seguem com o animal. Em Fronteira e Full Loot fazem parte da carga transportada.</dd><dt>Full Loot</dt><dd>O item de montaria entra na regra de perda.</dd><dt>Região Turbulenta</dt><dd>Montarias não atravessam.</dd></dl>
        ${bags.length ? `<div class="callout">O cuidador guardou os alforjes que voltaram com o animal: ${bags.map(entryName).join(', ')}. ${btn('bags', '', 'Levar ao armazém')}</div>` : ''}</div>`;
    },
    actions: {
      buy: () => buyMount(P),
      bags: () => { for (const e of P.stableBags.splice(0)) addEntry(P.storage, e); toast('Alforjes levados ao armazém.', 'item'); },
    },
  });
}

// ---------- quadro de relatos: voz local sobre o estado do mundo ----------
export function boardPanel() {
  openPanel({
    id: 'board',
    render() {
      const camp = CAMP_LABEL[CAMP.state];
      const ph = phaseOf();
      const alto = MARKETS.alto.sell.ferramentas;
      const notes = [
        `<div class="callout" style="--cc:var(--z-front)"><b>Passagem Estreita — ${EVT.done ? 'reaberta' : 'instável'}.</b> ${EVT.done ? 'Guardas no canteiro e no meio da garganta. Continua sendo Fronteira.' : `O canteiro na entrada pede ferramentas e lingotes. Abastecimento: ${Math.floor(EVT.progress)}%.`}</div>`,
        `<div class="callout" style="--cc:var(--z-loot)"><b>Acampamento das Garras — ${camp.name}.</b> ${camp.sign}</div>`,
        `<div class="callout" style="--cc:var(--cobalt)"><b>Comércio.</b> O Entreposto Alto paga cerca de ${alto} moedas por ferramenta. A Estrada Longa é protegida, porém mais demorada; a Passagem é curta e exposta.</div>`,
        `<div class="callout" style="--cc:#dfe8ff"><b>O céu.</b> ${SKY.vanished.length ? `Relatos de ${SKY.vanished.length} ponto${SKY.vanished.length > 1 ? 's' : ''} de luz que sumiram. Fase observada: ${ph.name.toLowerCase()} — ${ph.desc.toLowerCase()}` : 'Astrônomos falam de brilhos desiguais. Ninguém concorda sobre o motivo.'} Há quem diga que existe um instrumento antigo a oeste da Passagem.</div>`,
        TS.portal ? `<div class="callout" style="--cc:var(--z-turb)"><b>Rasgo violeta.</b> Visto em ${TS.portal.place}. Quem atravessa entra sem companhia e sem montaria.</div>` : `<div class="callout" style="--cc:var(--z-turb)"><b>Rasgos violetas.</b> Aparecem em lugares diferentes e fecham depois de pouco tempo. Quem volta traz fragmentos que valem bem no Alto.</div>`,
      ];
      first('disc_board', 'historia', 'exploracao', 'Relatos do Vale', 'Leu o quadro de relatos do Entreposto do Vale: a Passagem instável, as Garras e um céu que ninguém explica do mesmo jeito.');
      return head('Quadro de relatos', 'O que se comenta no Vale. Cada relato descreve o estado atual do mundo.') + `<div class="pb stack">${notes.join('')}</div>`;
    },
  });
}

// ---------- canteiro ----------
export function canteiroPanel() {
  const P = G.player;
  openPanel({
    id: 'canteiro',
    render() {
      const pct = Math.round(share() * 100);
      const nf = count(P.inv, 'ferramentas'), nl = count(P.inv, 'lingote');
      const pay = (id) => EVT.done ? Math.round({ ferramentas: 16, lingote: 6 }[id] * 0.6) : { ferramentas: 16, lingote: 6 }[id];
      const parts = Object.entries(EVT.contrib).filter(([, v]) => v).map(([k, v]) => `${{ ferramentas: 'ferramentas', lingotes: 'lingotes', reconhecimento: 'reconhecimento', saqueadores: 'saqueadores afastados', acampamento: 'pressão sobre as Garras' }[k]} (${Math.round(v)})`).join(', ');
      return head('Canteiro da Passagem', EVT.done ? 'A Passagem foi reaberta. O canteiro segue comprando, com pagamento menor.' : 'Um trecho da Passagem deixou de ser confiável. A mestra do canteiro reúne materiais, relatos e escoltas.') +
        `<div class="pb stack"><div><strong>Abastecimento ${Math.floor(EVT.progress)}%</strong><div class="meterbig"><i style="width:${EVT.progress}%"></i></div>
        <p class="note">Sua parte: ${EVT.playerPts ? `${pct}% das contribuições registradas — ${parts}` : 'nenhuma contribuição ainda'}. Outros participantes também abastecem o canteiro.</p></div>
        <div class="rows">${row('ferramentas', 'Entregar ferramentas', `${nf} na bolsa · ${pay('ferramentas')} m cada`, '', btn('give', 'ferramentas', 'Entregar todas', { disabled: !nf, primary: !!nf }), !nf)}
        ${row('lingote', 'Entregar lingotes', `${nl} na bolsa · ${pay('lingote')} m cada`, '', btn('give', 'lingote', 'Entregar todos', { disabled: !nl }), !nl)}</div>
        <div class="callout" style="--cc:var(--z-front)">Também contam: registrar o movimento na ruína de vigia, afastar saqueadores dentro da Passagem e pressionar o Acampamento das Garras. Nada disso é obrigatório.</div></div>`;
    },
    actions: { give: (id) => deliver(P, id) },
  });
}

// ---------- astrônoma ----------
export function astronomerPanel() {
  const P = G.player;
  openPanel({
    id: 'astronomer', cls: 'narrow',
    render() {
      const has = count(P.inv, 'instrumento');
      return head('A astrônoma do observatório', 'Observação, interpretação e o que ninguém sabe ainda.') +
        `<div class="pb stack"><p>“${SKY.vanished.length ? `Contei ${SKY.vanished.length} ausência${SKY.vanished.length > 1 ? 's' : ''} desde que cheguei.` : 'O brilho da Lanterna oscila de um jeito que não oscilava.'} Não sei a causa. Ninguém sabe.”</p>
        <div class="callout" style="--cc:#dfe8ff"><b>Interpretações que circulam.</b> Os navegadores do Alto falam em névoa alta. Os ferreiros do Vale dizem que é sinal de minério novo sob a serra. Os guardiões da ruína de vigia juram que as estrelas estão sendo apagadas de propósito. Nenhuma delas foi confirmada.</div>
        ${SKY.instrumentGiven ? '<p class="note">O instrumento que você trouxe já está em uso.</p>' : `<p>“Um instrumento novo ajudaria nas medições: dois lingotes e um cristal celeste, trabalho de artesão.”</p>${btn('give', '', has ? 'Entregar instrumento (60 m)' : 'Você não tem um instrumento', { disabled: !has, primary: !!has })}`}</div>`;
    },
    actions: { give: () => { if (removeFrom(P.inv, 'instrumento', 1)) giveInstrument(P); } },
  });
}

// ---------- inspeção de viajante: equipamento separado da biografia ----------
export function travelerPanel(n) {
  let tab = 'eq';
  const t = n.travel;
  openPanel({
    id: 'traveler', cls: 'narrow',
    render() {
      const tabs = `<p>${btn('tab', 'eq', 'Equipamento', { primary: tab === 'eq' })} ${btn('tab', 'bio', 'Biografia pública', { primary: tab === 'bio' })}</p>`;
      const body = tab === 'eq'
        ? `<div class="rows">${t.gear.map((g) => `<div class="row"><span class="g">·</span><div class="n">${esc(g)}</div><span></span><div></div></div>`).join('')}</div><p class="note">A inspeção de equipamento não revela a biografia.</p>`
        : `<div class="stack">${t.public.map(([k, v]) => `<div class="callout" style="--cc:var(--vellum)"><b>${k}.</b> ${esc(v)}</div>`).join('')}</div><p class="note">Só aparecem os marcos que ${esc(t.name.split(' ')[0])} escolheu exibir. O resto do Livro é pessoal.</p>`;
      return head(esc(t.name), `${t.role} · viajante (simula outro jogador)`) + `<div class="pb">${tabs}${body}</div>`;
    },
    actions: { tab: (a) => { tab = a; } },
  });
}

// ---------- saco de carga no chão ----------
export function lootPanel(bag) {
  const P = G.player;
  openPanel({
    id: 'loot', cls: 'narrow',
    render() {
      if (!bag.items.length) return head('Carga', 'Vazia.') + '<div class="pb"><p class="note">Nada restou.</p></div>';
      const left = Math.max(0, bag.expires - G.time);
      return head(bag.own ? 'Sua carga deixada para trás' : 'Carga no chão', `${bag.place} · ${ZONES[bag.zone].name} · some em ${Math.ceil(left / 60)} min`) +
        `<div class="pb"><div class="rows">${bag.items.map((e, i) => row(e.id, entryName(e), `${e.qty || 1} · ${kg(ITEMS[e.id].w * (e.qty || 1))}`, '', btn('take', i, 'Pegar'))).join('')}</div>
        <p>${btn('all', '', 'Pegar tudo', { primary: true })}</p><p class="note">${loadLine(P)}</p></div>`;
    },
    actions: {
      take: (i) => takeFromBag(P, bag, +i),
      all: () => { for (let i = bag.items.length - 1; i >= 0; i--) takeFromBag(P, bag, i); },
    },
  });
}
function takeFromBag(P, bag, i) {
  const e = bag.items[i]; if (!e) return;
  if (e.uid) {
    if (weightOf(P.inv) + ITEMS[e.id].w > capacity(P) * OVER_LIMIT) { toast('Sem capacidade.', 'warn'); return; }
    bag.items.splice(i, 1); P.inv.push(e);
  } else {
    const got = receive(P, e.id, e.qty);
    e.qty -= got; if (e.qty <= 0) bag.items.splice(i, 1);
    if (!got) { toast('Sem capacidade.', 'warn'); return; }
  }
  sfx('pickup');
  if (bag.own) {
    stat('recovered');
    first('recuperou-carga', 'feitos', 'risco', 'Carga recuperada', `Voltou a ${bag.place} e recolheu parte do que havia deixado para trás.`);
  }
  if (!bag.items.length) bag.remove();
}

// ---------- portal ----------
export function portalPanel() {
  const P = G.player;
  openPanel({
    id: 'portal',
    render() {
      const why = canEnter(P);
      const left = TS.portal ? Math.ceil(TS.portal.expires - G.time) : 0;
      return head('Um rasgo de luz violeta', `Fecha em ${left} s. Leia antes de atravessar: não há confirmação dentro da região.`) +
        `<div class="pb stack"><dl class="kv">
          <dt>Categoria</dt><dd><span class="tag" style="color:var(--z-turb)">Turbulenta</span> — Full Loot</dd>
          <dt>Acesso</dt><dd>Individual. Montarias e alforjes ficam do lado de fora.</dd>
          <dt>Outras pessoas</dt><dd>Aparecem como silhuetas sem nome. Arma e ataques continuam legíveis.</dd>
          <dt>Objetivo</dt><dd>Fragmentos anômalos em três santuários.</dd>
          <dt>Saídas</dt><dd>Três pedras de extração. Extrair leva 4 s e é interrompido por dano.</dd>
          <dt>Colapso</dt><dd>Em ${DURATION / 60} minutos a região colapsa. Quem estiver dentro perde tudo o que leva.</dd>
          <dt>Retorno</dt><dd>Ao local deste portal.</dd>
          <dt>Se cair</dt><dd>Tudo o que você leva fica na região. Conhecimentos, moedas e o Livro permanecem.</dd>
        </dl>
        ${why ? `<div class="callout" style="--cc:var(--warn)">${why}</div>` : ''}</div>
        <div class="pf">${btn('close', '', 'Não atravessar')}${btn('enter', '', 'Atravessar sem companhia', { primary: true, disabled: !!why })}</div>`;
    },
    actions: { enter: () => { if (canEnter(P)) return; closePanel(); enter(P); } },
  });
}

// ---------- derrota ----------
export function deathPanel(report) {
  const P = G.player;
  const z = ZONES[report.zone];
  openPanel({
    id: 'death',
    render() {
      const list = (arr) => arr.length ? `<ul>${arr.map((x) => `<li>${x.qty > 1 ? x.qty + '× ' : ''}${esc(x.name)}</li>`).join('')}</ul>` : '<p class="note">Nada.</p>';
      return `<div class="ph"><div><h2>Você caiu em ${esc(report.place)}</h2><p><span class="tag" style="color:${z.color}">${z.mark} ${z.name}</span> ${z.death}</p></div></div><div class="ribbon"></div>
        <div class="pb"><div class="cols"><section><h3>Ficou no local</h3>${list(report.lost)}${report.destroyed.length ? `<h3>Destruído</h3>${list(report.destroyed)}` : ''}
        ${report.bag && report.zone !== 'turbulenta' ? '<p class="note">Um saco com a carga ficou no chão, marcado no mapa. Saqueadores podem recolhê-lo; derrotá-los devolve a carga ao chão.</p>' : ''}</section>
        <section><h3>Permanece com você</h3><ul>${report.kept.map((k) => `<li>${esc(k)}</li>`).join('')}</ul>
        ${!P.equip.weapon ? '<div class="callout" style="--cc:var(--warn)">Sem arma equipada: o instrutor do Vale oferece um kit de recomeço barato.</div>' : ''}</section></div></div>
        <div class="pf">${btn('respawn', '', 'Retornar ao abrigo do Vale', { primary: true })}</div>`;
    },
    actions: { respawn: () => { closePanel(); respawn(P); } },
    onClose: () => { if (P.state === 'dead') respawn(P); },
  });
}

// ---------- as quatro perguntas (cap. 13) ----------
export function fourQuestionsPanel(q) {
  openPanel({
    id: 'event', cls: 'wide',
    render() {
      const c = q.contributed;
      return head(q.title, 'Toda mudança importante responde a quatro perguntas.', 'Entendi') +
        `<div class="pb"><div class="q4">
          <div><h4>O que mudou</h4><p>${q.changed}</p></div>
          <div><h4>Quais ações contribuíram</h4><div class="split"><i style="width:${c.you}%;background:var(--glaze)"></i><i style="width:${c.others}%;background:var(--cobalt)"></i></div>
            <p>Você: <b>${c.you}%</b> · outros participantes: <b>${c.others}%</b>.</p>
            <p class="note">${c.parts.length ? c.parts.map((p) => `${p.label} (${p.pts})`).join(' · ') : 'Você não contribuiu desta vez.'}</p><p class="note">${c.othersNote}</p></div>
          <div><h4>Quem foi afetado</h4><p>${q.affected}</p></div>
          <div><h4>Que respostas ainda são possíveis</h4><p>${q.responses}</p></div>
        </div><p class="note">O Livro registra a participação comprovada; a história da região registra o resultado coletivo.</p></div>`;
    },
  });
}

// ---------- inventário ----------
export function inventoryPanel() {
  const P = G.player;
  openPanel({
    id: 'inventory', cls: 'wide',
    render() {
      const slot = (s, label) => {
        const g = P.equip[s];
        return `<div class="eq"><small>${label}</small>${g ? `<b>${ITEMS[g.id].name}</b><span>${s === 'mount' ? 'Alforjes de 60 kg' : condTxt(g)}</span>${s !== 'mount' ? btn('unequip', s, 'Remover') : ''}` : '<b style="color:var(--muted)">Vazio</b>'}</div>`;
      };
      const reach = saddleReachable(P);
      const invRows = P.inv.length ? P.inv.map((e, i) => {
        const it = ITEMS[e.id];
        let acts = '';
        if (it.kind === 'weapon' || it.kind === 'armor' || it.kind === 'bag') acts += btn('equip', i, 'Equipar');
        if (e.id === 'pocao') acts += btn('drink', i, 'Beber');
        if (reach) acts += btn('toSaddle', i, '→ Alforjes');
        acts += btn('drop', i, 'Largar');
        let sub = `${e.qty || 1} · ${kg(it.w * (e.qty || 1))}`;
        if (it.req && !P.trainings.has(it.req)) sub += ` · requer ${TRAININGS[it.req].name}`;
        return row(e.id, entryName(e), sub, '', acts);
      }).join('') : '<p class="note">Bolsa vazia.</p>';
      const sadRows = P.equip.mount ? (P.saddle.length ? P.saddle.map((e, i) => row(e.id, entryName(e), `${e.qty || 1} · ${kg(ITEMS[e.id].w * (e.qty || 1))}`, '', reach ? btn('fromSaddle', i, '← Bolsa') : '')).join('') : '<p class="note">Alforjes vazios.</p>') : '<p class="note">Sem montaria. O estábulo do Vale vende uma.</p>';
      const ls = loadState(P);
      const trainings = [...P.trainings].map((t) => TRAININGS[t].name).join(', ');
      const fam = weaponFamily(P);
      return head('Inventário', `Bolsa ${kg(weightOf(P.inv))} de ${capacity(P)} kg (${ls === 'normal' ? 'carga normal' : ls === 'sobrecarga' ? 'sobrecarga' : 'no limite'}) · ${P.coins} moedas`) +
        `<div class="pb stack"><div class="slots4">${slot('weapon', 'Arma')}${slot('armor', 'Armadura')}${slot('bag', 'Bolsa')}${slot('mount', 'Montaria')}</div>
        <p class="note">Técnicas da arma atual: ${WEAPONS[fam].skills.map((s) => `<b>${SKILLS[s].name}</b> — ${SKILLS[s].desc}`).join(' · ') || 'nenhuma.'} Conhecimentos: ${trainings}.</p>
        <div class="cols"><section><h3>Bolsa (carga transportada)</h3><div class="rows">${invRows}</div></section>
        <section><h3>Alforjes ${P.equip.mount ? `— ${kg(weightOf(P.saddle))} de ${saddleCap(P)} kg` : ''}</h3><div class="rows">${sadRows}</div>
        ${P.equip.mount && !reach ? '<p class="note">Aproxime-se da montaria para mexer nos alforjes.</p>' : ''}
        <div class="callout" style="margin-top:10px">Peso em três estados: normal até 100%; sobrecarga até 130% (lento e sem esquiva); acima disso nada mais entra. Trocar de conjunto não é possível em combate.</div></section></div></div>`;
    },
    actions: {
      equip: (i) => equipFromInv(P, +i),
      unequip: (s) => {
        if (inCombat()) { toast('Não é possível trocar o conjunto em combate.', 'warn'); sfx('deny'); return; }
        const g = P.equip[s]; if (!g) return;
        P.equip[s] = null; P.inv.push(g);
        if (s === 'weapon') P.model.setWeapon(null);
      },
      drink: () => { if (P.hp < P.maxHp && removeFrom(P.inv, 'pocao', 1)) { P.hp = Math.min(P.maxHp, P.hp + 45); sfx('pickup'); } },
      toSaddle: (i) => {
        const e = P.inv[+i]; if (!e) return;
        const w = ITEMS[e.id].w;
        if (weightOf(P.saddle) + w > saddleCap(P)) { toast('Alforjes cheios.', 'warn'); return; }
        if (e.uid) { P.inv.splice(+i, 1); P.saddle.push(e); } else { removeFrom(P.inv, e.id, 1); addTo(P.saddle, e.id, 1); }
      },
      fromSaddle: (i) => {
        const e = P.saddle[+i]; if (!e) return;
        if (weightOf(P.inv) + ITEMS[e.id].w > capacity(P) * OVER_LIMIT) { toast('Bolsa no limite.', 'warn'); return; }
        if (e.uid) { P.saddle.splice(+i, 1); P.inv.push(e); } else { removeFrom(P.saddle, e.id, 1); addTo(P.inv, e.id, 1); }
      },
      drop: (i) => {
        const e = P.inv[+i]; if (!e) return;
        P.inv.splice(+i, 1);
        emit('drop', e);
      },
    },
  });
}
export function equipFromInv(P, i) {
  const e = P.inv[i]; if (!e) return;
  const it = ITEMS[e.id];
  if (inCombat()) { toast('Não é possível trocar o conjunto em combate.', 'warn'); sfx('deny'); return; }
  if (it.req && !P.trainings.has(it.req)) { toast(`Requer ${TRAININGS[it.req].name}. O instrutor do Vale ensina.`, 'warn'); sfx('deny'); return; }
  const slot = it.kind === 'weapon' ? 'weapon' : it.kind === 'armor' ? 'armor' : it.kind === 'bag' ? 'bag' : null;
  if (!slot) return;
  P.inv.splice(i, 1);
  if (P.equip[slot]) P.inv.push(P.equip[slot]);
  P.equip[slot] = e;
  if (slot === 'weapon') { P.model.setWeapon(e.cond > 0 ? it.family : null); P.warnedBroken = false; }
  sfx('ui');
}

// ---------- intenções (cap. 02: escolher uma intenção) ----------
export function intentsPanel(intro) {
  openPanel({
    id: 'intents',
    render() {
      const s = G.stats;
      const list = [
        ['Produzir', 'Forjar ferramentas na bancada do Vale', 'Minério e madeira no Bosque das Forjas, a oeste. Dois minérios viram um lingote.', (s.crafted_ferramentas || 0) >= 3, `${Math.min(3, s.crafted_ferramentas || 0)}/3`],
        ['Comerciar', 'Vender ferramentas no Entreposto Alto', 'O Alto paga mais. Escolha entre a Estrada Longa (protegida, longa) e a Passagem (curta, Fronteira).', (s.sold_alto_ferramentas || 0) > 0],
        ['Contribuir', 'Ajudar a reabrir a Passagem Estreita', 'O canteiro na entrada da Passagem aceita materiais e relatos.', EVT.done && EVT.playerPts > 0, EVT.playerPts ? `${Math.round(share() * 100)}% das contribuições` : ''],
        ['Explorar', 'Encontrar a estrutura antiga que os mapas não mostram', 'Relatos falam de um instrumento a oeste da Passagem.', !!G.flags.disc_observatorio],
        ['Observar', 'Registrar uma ausência no céu', 'Estrelas somem aos poucos. À noite, a diferença é visível.', SKY.observed.size > 0],
        ['Arriscar', 'Atravessar um rasgo violeta e voltar', 'Portais temporários levam a uma Região Turbulenta. Leve só o que aceita perder.', (s.extracoes || 0) > 0],
      ];
      return head(intro ? 'Escolha uma intenção' : 'Intenções', intro ? 'Nada aqui é obrigatório. O mundo continua mudando enquanto você decide como viver nele.' : 'Sugestões de caminho. O Livro registra o que você fizer, não o que estava na lista.', intro ? 'Começar' : 'Fechar') +
        `<div class="pb">${list.map(([k, t, h, done, prog]) => `<div class="intent${done ? ' done' : ''}"><span class="ck">${done ? '✓︎' : ''}</span><div><span class="kind">${k}${prog && !done ? ` · ${prog}` : ''}</span><h4>${t}</h4><p>${h}</p></div></div>`).join('')}
        ${intro ? `<div class="callout" style="margin-top:14px"><b>Controles.</b> WASD anda · Shift + WASD corre · arraste o chão com o mouse para girar e inclinar a câmera · clique num inimigo para atacar · clique num lugar ou pessoa para usar (ou F) · segure o botão direito para mirar de perto e clique para disparar · Espaço pula (Shift + Espaço: salto impulsionado) · Ctrl agacha · C esquiva · Q/E técnicas da arma · Tab abre o menu · Tab + 1/2/3 escolhe a distância da câmera.</div>` : ''}</div>`;
    },
  });
}

// ---------- mapa ----------
export function mapPanel() {
  openPanel({
    id: 'map', cls: 'wide',
    render() {
      const leg = Object.values(ZONES).filter((z) => z.name !== 'Turbulenta').map((z) => `<span><i style="background:${z.color}"></i>${z.name}</span>`).join('');
      return head('Mapa do recorte', 'Lugares não descobertos aparecem como “?”. Nomes provisórios.') +
        `<div class="pb"><canvas id="fullmap" width="640" height="640"></canvas><div class="legend">${leg}<span><i style="background:#f2c86a"></i>Sua carga no chão</span><span><i style="background:#c9a8ff"></i>Rasgo violeta</span><span><i style="background:#c8b49a"></i>Montaria</span></div></div>`;
    },
    after(p) { drawFullMap(p.querySelector('#fullmap')); },
  });
}

// ---------- pausa ----------
export function pausePanel(onRestart, onQuality) {
  openPanel({
    id: 'pause', cls: 'narrow',
    render() {
      return head('Pausa', 'O mundo desta demo fica parado enquanto este menu está aberto.', 'Voltar') +
        `<div class="pb stack"><dl class="kv">
          <dt>Andar · Correr</dt><dd>WASD (relativo à câmera) · segure Shift para correr</dd>
          <dt>Agachar</dt><dd>Segure Ctrl: passo curto e sem corrida</dd>
          <dt>Pular · Salto impulsionado</dt><dd>Espaço · Shift + Espaço (mais alto e com impulso à frente; gasta mais vigor)</dd>
          <dt>Atacar</dt><dd>Clique no inimigo: aproxima e ataca até ele cair</dd>
          <dt>Usar</dt><dd>Clique no lugar, pessoa ou recurso — ou F no mais próximo</dd>
          <dt>Mirar</dt><dd>Segure o botão direito: a câmera aproxima por cima do ombro, o corpo acompanha a mira e o clique dispara na hora</dd>
          <dt>Esquivar</dt><dd>C</dd>
          <dt>Técnicas</dt><dd>Q e E, na direção do alvo ou do cursor</dd>
          <dt>Poção · Montaria</dt><dd>1 · R, ou clique na barra de ações</dd>
          <dt>Menu</dt><dd>Tab (ao soltar): Inventário, Livro, Mapa e Intenções · I inventário · B Livro · J intenções</dd>
          <dt>Distância da câmera</dt><dd>Segure Tab e aperte 1 (distante), 2 (média) ou 3 (próxima); a roda continua ajustando</dd>
          <dt>Minimapa</dt><dd>Sempre visível; M (ou o botão +) amplia e volta ao normal. O mapa completo fica na aba Mapa do Tab</dd>
          <dt>Câmera</dt><dd>Arraste o chão com o botão esquerdo: para os lados gira, para cima e para baixo inclina (ou use as setas), até olhar acima do horizonte. Mirando, o ponteiro trava no centro e o mouse gira. O “N” do minimapa recentraliza; a roda aproxima</dd>
        </dl>
        <h3>Câmera</h3>
        <p>${[['baixa', 'Lenta'], ['media', 'Normal'], ['alta', 'Rápida']].map(([id, label]) => btn('camsens', id, label, { primary: CAM_PREFS.sens === id })).join(' ')} ${btn('caminvert', '', CAM_PREFS.invert ? 'Vertical invertido' : 'Inverter vertical', { primary: CAM_PREFS.invert })}</p>
        <p class="note">Velocidade do giro, valendo para o mouse, o arrasto e as setas. Mirando, o ponteiro trava no centro da tela e o mouse gira a câmera nos dois eixos; se o navegador não permitir travar, o cursor perto das bordas gira. A escolha fica salva neste navegador.</p>
        <h3>Teclado protegido</h3>
        <p>${btn('keyguard', '', KEYGUARD.enabled ? 'Tela cheia com teclado protegido: ligada' : 'Tela cheia com teclado protegido: desligada', { primary: KEYGUARD.enabled })}${KEYGUARD.enabled && KEYGUARD.supported && !KEYGUARD.active ? ' ' + btn('keyguardgo', '', 'Voltar à tela cheia') : ''}</p>
        <p class="note">${KEYGUARD.supported
    ? `Em tela cheia, Ctrl+W, Ctrl+T e Ctrl+Tab chegam ao jogo e não fecham nem trocam a aba — dá para agachar e andar ao mesmo tempo. Esc abre a pausa; segure Esc para sair da tela cheia. ${KEYGUARD.active ? 'Ativo agora.' : 'Inativo agora.'}`
    : 'Este navegador (ou esta janela, dentro de outra página) não permite travar o teclado: agachado, Ctrl+W ainda fecha a aba. O navegador pergunta antes de sair. No Chrome ou no Edge, abra o HTML direto para ter a proteção.'}</p>
        <h3>Qualidade gráfica</h3>
        <p>${Object.entries(LEVELS).map(([id, q]) => btn('quality', id, q.label, { primary: QUALITY.level === id })).join(' ')}</p>
        <p class="note">Alta: oclusão de ambiente, anti-serrilhado e grama cheia. Média: menos grama e sem oclusão. Baixa: sem pós-processamento nem grama. A escolha fica salva neste navegador.</p>
        <div class="callout">Esta é uma demo de concepção. Nomes, números, regras e textos do Livro são provisórios e servem para discutir o documento-mestre, não para representar decisões aprovadas.</div>
        <h3>Créditos dos modelos 3D</h3>
        <p class="note">Personagens “Kachujin G Rosales” e “Eve By J.Gonzales” e a animação “Unarmed Walk Forward”: Mixamo (Adobe). Cavalo “HORSE - Realistic 3D Model (DEMO FREE)” e leoa “LIONESS - Realistic 3D Model (DEMO FREE)”: WildMesh 3D (sketchfab.com/WildMesh_3D), licença CC BY-NC 4.0 — uso não comercial, com crédito ao autor. Os modelos foram reduzidos e comprimidos para esta demo.</p>
        <p>${btn('mute', '', audio.muted ? 'Ativar som' : 'Silenciar')} ${btn('restart', '', 'Nova trajetória')}</p></div>`;
    },
    actions: {
      mute: () => setMuted(!audio.muted), restart: () => { closePanel(); onRestart(); }, quality: (lv) => onQuality && onQuality(lv),
      camsens: (id) => setCamPref('sens', id), caminvert: () => setCamPref('invert', !CAM_PREFS.invert),
      keyguard: () => { setKeyguard(!KEYGUARD.enabled); if (KEYGUARD.enabled) protectKeyboard().then(() => { if (cur) draw(); }); },
      keyguardgo: () => { protectKeyboard().then(() => { if (cur) draw(); }); },
    },
  });
}
