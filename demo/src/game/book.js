// O Livro do personagem (cap. 10): curadoria de acontecimentos verificáveis.
// Regras: só registra eventos confirmados, com textos de modelos controlados; repetições viram um marco
// agregado; tudo nasce pessoal e o jogador escolhe o que exibir.
import { G, clock, emit } from '../state.js';

export const BOOK = { entries: [], titles: [], shownTitle: null, seq: 1 };

// domínio de cada entrada define o nome do capítulo do dia
const CHAPTER_NAMES = {
  trabalho: 'Dias de oficina', comercio: 'Carga e estrada', exploracao: 'Caminhos novos',
  combate: 'Aço e poeira', risco: 'O que se arrisca', ceu: 'Sob um céu diferente', comunidade: 'Trabalho em conjunto',
};
const ROMAN = ['I', 'II', 'III', 'IV', 'V', 'VI', 'VII', 'VIII', 'IX', 'X', 'XI', 'XII'];

function push(e) {
  const c = clock();
  const entry = { id: BOOK.seq++, time: G.time, when: c.label, day: c.day, count: 1, public: false, ...e };
  BOOK.entries.push(entry);
  emit('book', entry);
  return entry;
}

// registro único (só na primeira vez que a chave acontece)
export function first(key, cat, domain, title, text) {
  if (BOOK.entries.some((e) => e.key === key)) return null;
  return push({ key, cat, domain, title, text });
}

// marco agregado: uma temporada de entregas, não uma página por pacote
export function tally(key, cat, domain, fmt, inc = 1, data) {
  let e = BOOK.entries.find((x) => x.key === key);
  if (!e) {
    const f = fmt(inc, data);
    e = push({ key, cat, domain, title: f.title, text: f.text, count: inc, data });
  } else {
    e.count += inc;
    e.data = data ?? e.data;
    const f = fmt(e.count, e.data);
    e.title = f.title; e.text = f.text;
    e.updated = clock().label;
    emit('book', e);
  }
  return e;
}

// registro de acontecimento individual (mortes, incursões)
export function log(cat, domain, title, text, key) { return push({ key: key || 'ev' + BOOK.seq, cat, domain, title, text }); }

export function note(text) {
  return push({ key: 'nota' + BOOK.seq, cat: 'historia', domain: null, title: 'Anotação pessoal', text, personal: true });
}

export function grantTitle(id, name, why) {
  if (BOOK.titles.some((t) => t.id === id)) return;
  BOOK.titles.push({ id, name, why });
  emit('book-title', { id, name });
}

export function chapters() {
  const days = new Map();
  for (const e of BOOK.entries) {
    if (!days.has(e.day)) days.set(e.day, []);
    days.get(e.day).push(e);
  }
  return [...days.entries()].map(([day, list]) => {
    const tally = {};
    for (const e of list) if (e.domain) tally[e.domain] = (tally[e.domain] || 0) + e.count;
    const top = Object.entries(tally).sort((a, b) => b[1] - a[1])[0];
    return { day, title: `Capítulo ${ROMAN[day - 1] || day} — ${top ? CHAPTER_NAMES[top[0]] : 'Chegada'}`, list };
  });
}

export function byCat(cat) { return BOOK.entries.filter((e) => e.cat === cat); }

export function resetBook() { BOOK.entries = []; BOOK.titles = []; BOOK.shownTitle = null; BOOK.seq = 1; }
