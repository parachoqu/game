// Menu único (Tab): Inventário · Livro · Mapa · Intenções, com uma faixa de abas acima do painel.
import { G } from '../state.js';
import { closePanel, panelOpen, inventoryPanel, mapPanel, intentsPanel } from './panels.js';
import { openBook, closeBook, bookOpen } from './bookui.js';

export const MENU_TABS = [['inventory', 'Inventário'], ['book', 'Livro'], ['map', 'Mapa'], ['intents', 'Intenções']];
const OPEN = { inventory: inventoryPanel, book: openBook, map: mapPanel, intents: () => intentsPanel(false) };
let last = 'inventory';
let bar = null;

export function menuTab() {
  if (bookOpen()) return 'book';
  const p = panelOpen();
  return p && OPEN[p] ? p : null;
}
export function closeMenu() { if (bookOpen()) closeBook(); else if (menuTab()) closePanel(); }

// abre uma aba; troca de aba se o menu já estiver aberto; não sobrepõe outros painéis (mercado, portal…)
export function openMenu(tab = last) {
  if (G.uiOpen && !menuTab()) return;
  if (bookOpen()) closeBook(); else if (menuTab()) closePanel();
  last = tab;
  OPEN[tab]();
}
export function toggleMenu(tab) {
  const cur = menuTab();
  if (cur && (!tab || cur === tab)) closeMenu();
  else openMenu(tab || last);
}

export function initMenu() {
  bar = document.getElementById('menubar');
  bar.innerHTML = MENU_TABS.map(([id, name]) => `<button type="button" data-tab="${id}">${name}</button>`).join('') +
    '<span class="hint"><kbd>Tab</kbd> abre e fecha · <kbd>Esc</kbd> fecha</span>';
  bar.addEventListener('click', (e) => {
    const b = e.target.closest('[data-tab]');
    if (b) openMenu(b.dataset.tab);
  });
}

// mantém a faixa sincronizada com o que está aberto
export function updateMenu() {
  const cur = menuTab();
  const show = !!cur;
  if (bar.hidden === show) bar.hidden = !show;
  document.body.classList.toggle('with-menubar', show);
  if (show) for (const b of bar.querySelectorAll('[data-tab]')) b.classList.toggle('on', b.dataset.tab === cur);
}
