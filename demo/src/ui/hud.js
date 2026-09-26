// HUD: vitais, faixa de zona (regra visível antes da travessia), carga, ações, avisos e minimapa.
import { G, on, clock } from '../state.js';
import { input } from '../engine/input.js';
import { sfx } from '../engine/audio.js';
import { ITEMS, ZONES, WEAPONS, SKILLS } from '../config.js';
import { weightOf, capacity, loadState, saddleCap, count } from '../game/inventory.js';
import { drawMinimap } from './map.js';
import { EVT } from '../game/event.js';
import { TS } from '../game/turbulent.js';
import { BOOK } from '../game/book.js';

const $ = (id) => document.getElementById(id);
let els = null, zoneTimer = 0, miniT = 0, textT = 0;
let miniBig = false;

// M (ou o botão +) alterna o minimapa entre o tamanho normal e o ampliado; ele nunca some
export function toggleMinimap() {
  miniBig = !miniBig;
  const px = miniBig ? 320 : 176;
  els.mini.width = els.mini.height = px;
  els.corner.classList.toggle('big', miniBig);
  els.miniSize.textContent = miniBig ? '−' : '+';
  els.miniSize.title = miniBig ? 'Voltar ao tamanho normal (M)' : 'Ampliar o minimapa (M)';
  els.miniSize.setAttribute('aria-pressed', String(miniBig));
  drawMinimap(els.mini, miniBig);
}

export function toast(msg, kind = 'info', ms = 4200) {
  const box = document.getElementById('toasts');
  if (!box) return;
  const el = document.createElement('div');
  el.className = 'toast ' + kind;
  el.innerHTML = msg;
  box.prepend(el);
  while (box.children.length > 4) box.lastChild.remove();
  setTimeout(() => { el.classList.add('out'); setTimeout(() => el.remove(), 520); }, ms);
}

export function initHud() {
  els = {
    hud: $('hud'), name: $('hud-name'), title: $('hud-title'), hpFill: $('hp-fill'), hpText: $('hp-text'), vgFill: $('vg-fill'), vgText: $('vg-text'), chips: $('chips'),
    zone: $('zone'), zmark: $('zone-mark'), zname: $('zone-name'), zplace: $('zone-place'), zrule: $('zone-rule'),
    mini: $('minimap'), clock: $('clock'), widgets: $('widgets'), prompt: $('prompt'), channel: $('channel'), chLabel: $('channel-label'), chFill: $('channel-fill'),
    abBasic: $('ab-basic'), abQ: $('ab-q'), abE: $('ab-e'), abPot: $('ab-pot'), abWeapon: $('ab-weapon'), abMount: $('ab-mount-label'), abMountBtn: $('ab-mount'),
    coins: $('coins'), loadFill: $('load-fill'), loadText: $('load-text'), saddleRow: $('saddle-row'), saddleFill: $('saddle-fill'), saddleText: $('saddle-text'), loadState: $('load-state'),
    cross: $('crosshair'),
    crossRange: $('cross-range'),
    hitmarker: $('hitmarker'),
    down: $('down'), downText: $('down-text'), downFill: $('down-fill'), vignette: $('vignette'), north: $('cam-north'), corner: $('corner'), miniSize: $('mini-size'),
  };
  on('zone-change', ({ to }) => showZone(to, true));
  on('place-change', () => { els.zplace.textContent = G.anon ? 'Região Turbulenta' : G.player.place; });
  on('player-hurt', () => { els.vignette.classList.add('hit'); setTimeout(() => els.vignette.classList.remove('hit'), 90); });
  on('hitmarker', ({ crit }) => { showHitmarker(crit); sfx('hitmark'); });
  on('book', (e) => { if (e.count === 1 && !e.personal) toast(`O Livro registrou: <b>${e.title}</b>`, 'book', 5200); });
  on('book-title', (t) => toast(`Novo título no Livro: <b>${t.name}</b>`, 'book', 6000));
}

let hmTimer = null;
export function showHitmarker(crit = false) {
  if (!els || !els.hitmarker) return;
  els.hitmarker.classList.add('active');
  els.hitmarker.classList.toggle('crit', !!crit);
  if (hmTimer) clearTimeout(hmTimer);
  hmTimer = setTimeout(() => {
    if (els && els.hitmarker) els.hitmarker.classList.remove('active', 'crit');
    hmTimer = null;
  }, 130);
}

export function showZone(zone, expand) {
  const z = ZONES[zone];
  els.zone.style.setProperty('--zc', z.color);
  els.zmark.textContent = z.mark;
  els.zname.textContent = z.name;
  els.zplace.textContent = G.anon ? 'Região Turbulenta' : G.player.place;
  els.zrule.textContent = z.rule;
  if (expand) { els.zone.classList.add('expanded'); zoneTimer = 7; }
  els.vignette.classList.toggle('turb', zone === 'turbulenta');
}

export function setPrompt(text) {
  if (!text) { els.prompt.hidden = true; return; }
  els.prompt.hidden = false;
  els.prompt.innerHTML = text;
}

function skillSlot(el, id, cd) {
  const span = el.querySelector('span'), cdEl = el.querySelector('.cd');
  if (!id) { span.textContent = '—'; el.classList.add('off'); cdEl.style.height = '0'; return; }
  el.classList.remove('off');
  span.textContent = SKILLS[id].name;
  cdEl.style.height = (cd / SKILLS[id].cd * 100) + '%';
}

export function updateHud(dt) {
  const P = G.player;
  if (!els || !P) return;
  // barras (todo quadro)
  els.hpFill.style.width = (P.hp / P.maxHp * 100) + '%';
  els.vgFill.style.width = (P.vigor / P.maxVigor * 100) + '%';
  const w = weightOf(P.inv), cap = capacity(P);
  els.loadFill.style.width = Math.min(100, w / (cap * 1.3) * 100) + '%';
  if (P.channel) {
    els.channel.hidden = false;
    els.chLabel.textContent = P.channel.label;
    els.chFill.style.width = Math.min(100, P.channel.t / P.channel.dur * 100) + '%';
  } else els.channel.hidden = true;

  // mira: segue o cursor e fica vermelha sobre um inimigo (todo quadro, é só uma transformação)
  const aiming = !!P.aiming && !G.uiOpen;
  if (els.cross.hidden === aiming) els.cross.hidden = !aiming;
  if (aiming) {
    els.cross.style.transform = `translate(${input.mouse.x}px, ${input.mouse.y}px)`;
    const hasTarget = !!(P.aimEnemy || (G.hover && G.hover.kind === 'enemy'));
    els.cross.classList.toggle('on', hasTarget);
    if (els.crossRange) {
      if (hasTarget && P.aimDist) els.crossRange.textContent = `${Math.round(P.aimDist)}m`;
      else els.crossRange.textContent = '';
    }
  }

  const downNow = P.state === 'down';
  els.down.hidden = !downNow;
  if (downNow) {
    const guard = G.enemies.some((e) => e.alive && e.faction === 'guard' && Math.hypot(e.pos.x - P.pos.x, e.pos.z - P.pos.z) < 24);
    els.downText.textContent = guard ? 'Um guarda se aproxima para ajudar.' : 'Sem aliados por perto. A derrota se consolida em instantes.';
    els.downFill.style.width = Math.min(100, P.stateT / (guard ? 2.5 : 4.5) * 100) + '%';
  }
  if (zoneTimer > 0) { zoneTimer -= dt; if (zoneTimer <= 0) els.zone.classList.remove('expanded'); }

  miniT -= dt;
  if (miniT <= 0) {
    miniT = 0.1; drawMinimap(els.mini, miniBig);
    const turned = Math.abs(Math.atan2(Math.sin(G.cam.yaw), Math.cos(G.cam.yaw))) > 0.05 || Math.abs(G.cam.pitch - 0.67) > 0.05;
    els.north.classList.toggle('turned', turned);
    els.north.textContent = turned ? 'N ↺' : 'N';
  }

  textT -= dt;
  if (textT > 0) return;
  textT = 0.12;
  els.name.textContent = G.anon ? 'Identidade oculta' : P.name;
  const title = BOOK.titles.find((t) => t.id === BOOK.shownTitle);
  els.title.textContent = G.anon ? '' : title ? title.name : '';
  els.hpText.textContent = `Vida ${Math.ceil(P.hp)} / ${P.maxHp}`;
  els.vgText.textContent = `Vigor ${Math.floor(P.vigor)}`;
  const chips = [];
  if (G.time - P.lastCombat < 5) chips.push('<span class="chip combat">Em combate</span>');
  if (P.mounted) chips.push('<span class="chip mount">Montado</span>');
  if (G.anon) chips.push('<span class="chip anon">Anônimo</span>');
  const ls = loadState(P);
  if (ls !== 'normal') chips.push(`<span class="chip over">${ls === 'sobrecarga' ? 'Sobrecarga' : 'Carga no limite'}</span>`);
  const html = chips.join('');
  if (els.chips.innerHTML !== html) els.chips.innerHTML = html;

  els.clock.textContent = clock().label;
  // ações
  const wpn = P.equip.weapon;
  const fam = wpn && wpn.cond > 0 ? ITEMS[wpn.id].family : 'punhos';
  els.abBasic.textContent = fam === 'arco' ? 'Disparar' : 'Atacar';
  const m = G.mount;
  els.abMount.textContent = !P.equip.mount ? 'Sem montaria' : P.mounted ? 'Desmontar' : m && m.present && Math.hypot(m.pos.x - P.pos.x, m.pos.z - P.pos.z) < 5 ? 'Montar' : 'Chamar montaria';
  els.abMountBtn.classList.toggle('off', !P.equip.mount);
  skillSlot(els.abQ, WEAPONS[fam].skills[0], P.cd.Q);
  skillSlot(els.abE, WEAPONS[fam].skills[1], P.cd.E);
  els.abPot.querySelector('b').textContent = '×' + count(P.inv, 'pocao');
  els.abPot.querySelector('.cd').style.height = (P.cd.pot / 2 * 100) + '%';
  const wHtml = wpn
    ? `<small>Arma</small><b>${ITEMS[wpn.id].name}</b><div class="cond"><i style="width:${wpn.cond}%;background:${wpn.cond < 35 ? 'var(--bad)' : wpn.cond < 70 ? 'var(--warn)' : 'var(--good)'}"></i></div><small>${Math.round(wpn.cond)}% de condição</small>`
    : '<small>Arma</small><b>Nenhuma</b><small>Kit de recomeço no instrutor</small>';
  if (els.abWeapon.innerHTML !== wHtml) els.abWeapon.innerHTML = wHtml;

  els.coins.textContent = P.coins;
  els.loadText.textContent = `${w.toFixed(1)} / ${cap} kg`;
  const sc = saddleCap(P);
  els.saddleRow.hidden = !sc;
  if (sc) {
    const sw = weightOf(P.saddle);
    els.saddleFill.style.width = Math.min(100, sw / sc * 100) + '%';
    els.saddleText.textContent = `${sw.toFixed(1)} / ${sc} kg`;
  }
  els.loadState.className = ls === 'sobrecarga' ? 'over' : ls === 'limite' ? 'limit' : '';
  els.loadState.textContent = ls === 'normal' ? 'Carga normal' : ls === 'sobrecarga' ? 'Sobrecarga: deslocamento lento, sem esquiva' : 'No limite: nada mais entra na bolsa';

  // widgets de contexto
  const wd = [];
  if (TS.inside) {
    const left = Math.max(0, TS.collapseAt - G.time);
    wd.push(`<div class="widget" style="--wc:var(--z-turb)"><h4>Colapso da região</h4><div class="big">${Math.floor(left / 60)}:${String(Math.floor(left % 60)).padStart(2, '0')}</div><div class="meter"><i style="width:${left / 240 * 100}%"></i></div></div>`);
  } else if (TS.portal) {
    const left = Math.max(0, TS.portal.expires - G.time);
    wd.push(`<div class="widget" style="--wc:var(--z-turb)"><h4>Rasgo violeta</h4>${TS.portal.place} · fecha em ${Math.ceil(left)} s</div>`);
  }
  if (!EVT.done && (P.place === 'Passagem Estreita' || P.place === 'Canteiro da Passagem' || EVT.playerPts > 0)) {
    wd.push(`<div class="widget" style="--wc:var(--z-front)"><h4>Passagem: instável</h4>Abastecimento ${Math.floor(EVT.progress)}%<div class="meter"><i style="width:${EVT.progress}%"></i></div></div>`);
  }
  const wdh = wd.join('');
  if (els.widgets.innerHTML !== wdh) els.widgets.innerHTML = wdh;
}

export function showHud(v) { els.hud.hidden = !v; }
