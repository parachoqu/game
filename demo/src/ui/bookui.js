// O Livro (tecla B): memória, expressão e legado.
import { G } from '../state.js';
import { ORIGINS, TRAININGS, ITEMS, SKILLS } from '../config.js';
import { BOOK, chapters, byCat, note } from '../game/book.js';
import { sfx } from '../engine/audio.js';

const el = () => document.getElementById('book');
let tab = 'historia', publicView = false, writing = false;

const TABS = [
  ['historia', 'História', 'Capítulos da trajetória, na ordem em que aconteceram.'],
  ['feitos', 'Feitos', 'Marcos de trabalho, combate, cooperação — e derrotas.'],
  ['descobertas', 'Descobertas', 'Lugares, sinais e observações. Coordenadas não são publicadas.'],
  ['legado', 'Legado', 'Participação em acontecimentos coletivos e títulos reconhecidos.'],
  ['trilhas', 'Trilhas & Estilos', 'Sinergias e orientações do Capítulo 23: você é o que você equipa e pratica.'],
  ['perfil', 'Perfil', 'Origem, conhecimentos, ofícios e o que você escolhe mostrar.'],
];
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' }[c]));

const SUGGESTED_PATHS = [
  {
    id: 'vanguarda',
    name: 'Vanguarda de Ferro',
    focus: 'Linha de Frente · Absorção & Controle',
    desc: 'Combate corpo a corpo pesado, absorção de golpes e quebra de formação adversária.',
    weapons: ['espada', 'martelo_guerra'],
    armors: ['armadura_pesada', 'armadura_ferro'],
    skills: ['Investida', 'Impacto Sísmico', 'Giro Cortante'],
    trainings: ['Combate com Armas', 'Forja de Ferro'],
  },
  {
    id: 'lutador',
    name: 'Lutador Ágil',
    focus: 'Velocidade · Pressão Rápida & Esquiva',
    desc: 'Sequências fulminantes de socos ou estocadas duplas, aproveitando qualquer brecha com mobilidade elevada.',
    weapons: ['manoplas', 'adagas'],
    armors: ['armadura_couro', 'roupa_viajante'],
    skills: ['Sequência Veloz', 'Golpe Triplo', 'Passo Sombrio'],
    trainings: ['Combate com Armas', 'Luta Desarmada', 'Sobrevivência'],
  },
  {
    id: 'sentinela',
    name: 'Sentinela dos Desfiladeiros',
    focus: 'Média Distância · Guarda & Estabilidade',
    desc: 'Mantém adversários a distância segura com estocadas precisas ou esmagamento tático de postura.',
    weapons: ['lanca', 'maca'],
    armors: ['armadura_ferro', 'armadura_couro'],
    skills: ['Estocada Penetrante', 'Arremesso de Lança', 'Esmagar'],
    trainings: ['Combate com Armas', 'Guarda & Defesa'],
  },
  {
    id: 'batedor',
    name: 'Batedor da Fronteira',
    focus: 'Precisão · Distância & Emboscada',
    desc: 'Disparos cirúrgicos à longa distância e recuo rápido em terreno acidentado ou florestas densas.',
    weapons: ['arco_longo', 'besta', 'machado_guerra'],
    armors: ['armadura_couro', 'roupa_viajante'],
    skills: ['Tiro Preciso', 'Chuva de Flechas', 'Disparo Perfurante'],
    trainings: ['Combate com Armas', 'Coleta de Recursos', 'Sobrevivência'],
  },
  {
    id: 'erudito',
    name: 'Erudito do Gelo & Vento',
    focus: 'Magia de Controle · Lentidão & Afastamento',
    desc: 'Congela avanços inimigos com frio cortante e dissipa grupos de ameaças com rajadas de vendaval.',
    weapons: ['cajado_gelo', 'orbe_vento'],
    armors: ['roupa_viajante', 'armadura_couro'],
    skills: ['Seta Gélida', 'Prisão de Gelo', 'Lâmina de Vento', 'Rajada Repulsora'],
    trainings: ['Magia Elemental', 'Herborismo'],
  },
  {
    id: 'piromante',
    name: 'Piromante de Batalha',
    focus: 'Magia Ofensiva · Dano Contínuo & Queima',
    desc: 'Incendeia o campo de batalha com esferas de fogo e pilares abrasadores, forçando o recuo adversário.',
    weapons: ['cajado_fogo', 'foice_combate'],
    armors: ['roupa_viajante', 'armadura_ferro'],
    skills: ['Bola de Fogo', 'Pilar de Chamas', 'Ceifa Ampla'],
    trainings: ['Magia Elemental', 'Combate com Armas'],
  },
  {
    id: 'xama',
    name: 'Xamã Metamorfo',
    focus: 'Híbrido Selvagem · Enraizamento & Forma Bestial',
    desc: 'Invoca raízes e vinhas do solo, regenera vigor vital e transforma-se na forma de um urso implacável.',
    weapons: ['totem_natureza', 'tomo_vital'],
    armors: ['armadura_couro', 'roupa_viajante'],
    skills: ['Raízes Aprisionadoras', 'Regeneração Vital', 'Grito Feroz'],
    trainings: ['Magia Elemental', 'Herborismo', 'Sobrevivência'],
  },
  {
    id: 'guardiao',
    name: 'Guardião Sagrado',
    focus: 'Proteção & Luz · Escudos & Restauração',
    desc: 'Conjura barreiras luminosas que absorvem ferimentos, purifica aliados e dispersa sombras corrompidas.',
    weapons: ['simbolo_sagrado', 'foco_maldicao'],
    armors: ['armadura_pesada', 'armadura_ferro'],
    skills: ['Barreira Radiante', 'Luz Curativa', 'Raio Entrópico'],
    trainings: ['Magia Elemental', 'Combate com Armas'],
  },
];

export function openBook() {
  G.uiOpen = 'book';
  el().hidden = false;
  writing = false;
  render();
  sfx('book');
}
export function closeBook() { el().hidden = true; el().innerHTML = ''; if (G.uiOpen === 'book') G.uiOpen = null; }
export const bookOpen = () => !el().hidden;

function entryHtml(e) {
  if (publicView && !e.public) return '';
  return `<article class="entry${e.personal ? ' personal' : ''}">
    <span class="when">${e.when}${e.updated ? ` · atualizado ${e.updated}` : ''}</span>
    <h4>${esc(e.title)}</h4><p>${esc(e.text)}</p>
    <div class="meta">${e.count > 1 ? `<span class="count">marco agregado · ${e.count}</span>` : ''}
    ${publicView ? '' : e.personal ? '<span class="count">anotação pessoal — não é registro do mundo</span>' : `<button type="button" class="vis${e.public ? ' pub' : ''}" data-bk="vis" data-id="${e.id}">${e.public ? 'Público' : 'Pessoal'}</button>`}</div></article>`;
}

function professions() {
  const s = G.stats, out = [];
  if (s.crafted) out.push(`Artesanato — ${s.crafted} fabricações`);
  const gathered = (s.gather_minerio || 0) + (s.gather_madeira || 0) + (s.gather_erva || 0) + (s.gather_cristal || 0);
  if (gathered) out.push(`Coleta — ${gathered} recursos`);
  const sold = Object.keys(s).filter((k) => k.startsWith('sold_')).reduce((a, k) => a + s[k], 0);
  if (sold) out.push(`Comércio — ${sold} vendas, ${s.earned || 0} moedas`);
  if (s.kills) out.push(`Combate — ${s.kills} confrontos vencidos`);
  if (s.incursoes) out.push(`Incursões — ${s.incursoes} (${s.extracoes || 0} com extração)`);
  return out;
}

function pageFor(t) {
  const [id, name, lede] = TABS.find((x) => x[0] === t);
  const P = G.player;
  const empty = `<p class="empty">${publicView ? 'Nada exibido publicamente nesta parte. Marque registros como “Público” para que apareçam aqui.' : 'Ainda não há registros aqui. O Livro escreve apenas o que realmente aconteceu com esta personagem.'}</p>`;
  let body;
  if (id === 'historia') {
    body = chapters().map((c) => {
      const items = c.list.map(entryHtml).join('');
      return items ? `<div class="chapter">${c.title}</div>${items}` : '';
    }).join('') || empty;
    if (!publicView) {
      body += writing
        ? `<div class="book-note" style="margin-top:18px"><label for="bk-note" class="when">Anotação pessoal (fica marcada como sua, separada dos registros do mundo)</label><textarea id="bk-note" maxlength="400"></textarea><p><button type="button" class="bk-btn" data-bk="save">Guardar anotação</button> <button type="button" class="bk-btn" data-bk="cancel">Cancelar</button></p></div>`
        : '<p style="margin-top:18px"><button type="button" class="bk-btn" data-bk="write">Escrever anotação pessoal</button></p>';
    }
  } else if (id === 'trilhas') {
    const curW = P.equip.weapon;
    const curA = P.equip.armor;
    const curWeaponName = ITEMS[curW]?.name || 'Nenhuma arma empunhada';
    const curArmorName = ITEMS[curA]?.name || 'Traje básico';
    const curSkills = (P.equip.skills || []).filter(Boolean).map((s) => SKILLS[s]?.name || s).join(', ') || 'Nenhuma';

    const cardsHtml = SUGGESTED_PATHS.map((p) => {
      const matchW = p.weapons.includes(curW);
      const matchA = p.armors.includes(curA);
      const isActive = matchW || matchA;
      return `<div class="path-card${isActive ? ' active' : ''}">
        <h4><span>${esc(p.name)}</span> ${isActive ? '<span class="tag">Afinidade</span>' : ''}</h4>
        <p><b>Foco:</b> ${esc(p.focus)}</p>
        <p>${esc(p.desc)}</p>
        <div class="specs">
          <div><b>Armas típicas:</b> ${p.weapons.map((w) => ITEMS[w]?.name || w).join(', ')}</div>
          <div><b>Vestimenta:</b> ${p.armors.map((a) => ITEMS[a]?.name || a).join(', ')}</div>
          <div><b>Técnicas sinérgicas:</b> ${p.skills.join(', ')}</div>
        </div>
      </div>`;
    }).join('');

    body = `
      <div class="paths-intro">
        <strong>Capítulo 23 do Documento-Mestre:</strong> Não há classes estanques. Sua personagem é definida pelo que carrega, forja, veste e aprende no mundo. Trocar de estilo requer apenas preparar novos equipamentos e praticar suas técnicas.
      </div>
      <div class="paths-hero">
        <h4>Configuração Atual da Personagem</h4>
        <p>Arma: <b>${esc(curWeaponName)}</b> · Armadura: <b>${esc(curArmorName)}</b> · Técnicas equipadas: <b>${esc(curSkills)}</b></p>
      </div>
      <div class="paths-grid">${cardsHtml}</div>
    `;
  } else if (id === 'perfil') {
    const o = ORIGINS[P.origin];
    const titles = BOOK.titles;
    const profs = professions();
    const shown = titles.find((x) => x.id === BOOK.shownTitle);
    const titleHtml = !titles.length ? 'Nenhum título ainda.'
      : publicView ? esc(shown ? shown.name : '—')
        : `<div class="titles">${titles.map((x) => `<button type="button" class="titlebtn${BOOK.shownTitle === x.id ? ' on' : ''}" data-bk="title" data-id="${x.id}" title="${esc(x.why)}">${esc(x.name)}</button>`).join('')}</div><p class="empty" style="font-size:13px;margin-top:6px">Títulos são expressivos: não concedem poder.</p>`;
    body = `<div class="profile"><dl>
      <dt>Nome</dt><dd>${esc(P.name)}</dd>
      <dt>Origem</dt><dd>${o.name} — ${o.hint} A origem não define ofício.</dd>
      ${publicView ? '' : `<dt>Conhecimentos</dt><dd>${[...P.trainings].map((t) => TRAININGS[t].name).join(', ')}</dd>`}
      <dt>Ofícios</dt><dd>${profs.length ? profs.join('<br>') : 'Ainda nenhum ofício reconhecido.'}</dd>
      ${publicView ? '' : `<dt>Montaria</dt><dd>${P.equip.mount ? ITEMS.montaria.name : '—'}</dd>`}
      <dt>Título exibido</dt><dd>${titleHtml}</dd>
      ${publicView ? `<dt>Marcos exibidos</dt><dd>${BOOK.entries.filter((e) => e.public).length}</dd>` : ''}
    </dl></div>`;
  } else {
    body = byCat(id).map(entryHtml).join('') || empty;
  }
  return `${publicView ? '<div class="pubbar">Visão pública — o que outra pessoa vê ao inspecionar sua biografia. A inspeção de equipamento é separada.</div>' : ''}<h3>${name}</h3><p class="lede">${lede}</p>${body}`;
}

function render() {
  const P = G.player;
  const n = (c) => byCat(c).filter((e) => !publicView || e.public).length;
  const counts = { historia: BOOK.entries.filter((e) => !publicView || e.public).length, feitos: n('feitos'), descobertas: n('descobertas'), legado: n('legado'), trilhas: '', perfil: '' };
  const title = BOOK.titles.find((t) => t.id === BOOK.shownTitle);
  el().innerHTML = `<div class="bk-left">
      <p class="eyebrow">O Livro de</p>
      <h2>${esc(P.name)}</h2>
      <p class="sub">${ORIGINS[P.origin].name}${title ? ` · ${esc(title.name)}` : ''}</p>
      <div class="band"></div>
      <nav class="tabs">${TABS.map(([id, name]) => `<button type="button" class="tab${tab === id ? ' on' : ''}" data-bk="tab" data-id="${id}">${name}<small>${counts[id]}</small></button>`).join('')}</nav>
      <div class="tools">
        <button type="button" class="bk-btn${publicView ? ' on' : ''}" data-bk="public">${publicView ? 'Voltar ao Livro pessoal' : 'Ver como os outros veem'}</button>
        <button type="button" class="bk-btn" data-bk="close">Fechar o Livro <kbd>Esc</kbd></button>
      </div>
    </div>
    <div class="bk-right">${pageFor(tab)}</div>`;
}

export function initBook() {
  el().addEventListener('click', (ev) => {
    const b = ev.target.closest('[data-bk]');
    if (!b) return;
    const a = b.dataset.bk;
    if (a === 'close') { closeBook(); return; }
    if (a === 'tab') { tab = b.dataset.id; writing = false; sfx('book'); }
    if (a === 'public') publicView = !publicView;
    if (a === 'vis') { const e = BOOK.entries.find((x) => x.id === +b.dataset.id); if (e) e.public = !e.public; }
    if (a === 'title') BOOK.shownTitle = BOOK.shownTitle === b.dataset.id ? null : b.dataset.id;
    if (a === 'write') writing = true;
    if (a === 'cancel') writing = false;
    if (a === 'save') { const t = el().querySelector('#bk-note').value.trim(); if (t) note(t); writing = false; }
    const scroll = el().querySelector('.bk-right')?.scrollTop || 0;
    render();
    const r = el().querySelector('.bk-right'); if (r && a !== 'tab') r.scrollTop = scroll;
    if (a === 'write') el().querySelector('#bk-note')?.focus();
  });
}
