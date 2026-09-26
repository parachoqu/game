// Evento regional (cap. 13/16): "A Passagem Estreita deixou de ser confiável".
// Contribuições civis e de combate somam; outros participantes (simulados) também contribuem.
// Ao concluir, muda uma condição concreta — guardas e contratos —, sem alterar a regra de perda da zona.
import { G, on, emit, stat } from '../state.js';
import { ITEMS, MARKETS } from '../config.js';
import { ROUTES, distToPolyline, GUARD_POSTS } from './layout.js';
import { count, removeFrom } from './inventory.js';
import { spawnEnemy } from './enemies.js';
import { first, log, grantTitle } from './book.js';
import { sfx } from '../engine/audio.js';
import { toast } from '../ui/hud.js';

export const EVT = {
  progress: 0, done: false, doneAt: null, othersPts: 0, playerPts: 0, othersT: 0,
  contrib: { ferramentas: 0, lingotes: 0, reconhecimento: 0, saqueadores: 0, acampamento: 0 },
  delivered: { ferramentas: 0, lingote: 0 },
};
const LABEL = { ferramentas: 'Ferramentas entregues', lingotes: 'Lingotes entregues', reconhecimento: 'Reconhecimento da passagem', saqueadores: 'Saqueadores afastados', acampamento: 'Pressão sobre o acampamento' };
const PAY = { ferramentas: 16, lingote: 6 };
const PTS = { ferramentas: 8, lingote: 3 };
const OTHERS_CAP = 55;

export function contribute(kind, pts) {
  if (EVT.done) return;
  EVT.contrib[kind] += pts; EVT.playerPts += pts;
  addProgress(pts);
  emit('event-progress');
}
function addProgress(p) {
  EVT.progress = Math.min(100, EVT.progress + p);
  if (EVT.progress >= 100 && !EVT.done) complete();
}

export function deliver(P, id) {
  const n = count(P.inv, id);
  if (!n) { toast(`Nenhum(a) ${ITEMS[id].name.toLowerCase()} na bolsa.`, 'warn'); sfx('deny'); return; }
  removeFrom(P.inv, id, n);
  const pay = n * (EVT.done ? Math.round(PAY[id] * 0.6) : PAY[id]);
  P.coins += pay; sfx('coin');
  EVT.delivered[id] += n;
  stat('delivered_' + id, n);
  toast(`Entregou ${n}× ${ITEMS[id].name} ao canteiro: +${pay} moedas.`, 'coin');
  if (!EVT.done) contribute(id === 'ferramentas' ? 'ferramentas' : 'lingotes', n * PTS[id]);
}

export function recon() {
  if (EVT.contrib.reconhecimento > 0) return;
  contribute('reconhecimento', 6);
  first('reconhecimento', 'descobertas', 'exploracao', 'Movimento na Passagem', 'Da ruína de vigia, registrou rotas de saqueadores e trilhas de Garras dentro da Passagem Estreita. O registro foi levado ao canteiro.');
  toast('Reconhecimento registrado e enviado ao canteiro.', 'event');
}

export function initEvent() {
  on('enemy-killed', ({ e, byPlayer }) => {
    if (!byPlayer || EVT.done || e.faction !== 'bandit') return;
    // conta apenas quem cai na faixa da Passagem, medida pela própria rota curta
    if (distToPolyline(e.pos.x, e.pos.z, ROUTES.short_gorge.pts) < 40 && EVT.contrib.saqueadores < 15) contribute('saqueadores', 3);
  });
}

export function updateEvent(dt) {
  if (EVT.done) return;
  EVT.othersT += dt;
  if (EVT.othersT > 7 && EVT.othersPts < OTHERS_CAP) {
    EVT.othersT = 0;
    EVT.othersPts += 1.5;
    addProgress(1.5);
    emit('event-progress');
  }
}

export function share() {
  const total = EVT.playerPts + EVT.othersPts;
  return total ? EVT.playerPts / total : 0;
}

export function fourQuestions() {
  const pct = Math.round(share() * 100);
  const parts = Object.entries(EVT.contrib).filter(([, v]) => v > 0).map(([k, v]) => ({ label: LABEL[k], pts: Math.round(v * 10) / 10 }));
  return {
    title: 'A Passagem Estreita foi reaberta',
    changed: 'Guardas passaram a vigiar o canteiro e o meio da Passagem. Saqueadores reaparecem com menos frequência. O Entreposto Alto voltou a receber ferramentas e paga menos por elas; o Vale abriu novas encomendas.',
    contributed: { you: pct, others: 100 - pct, parts, othersNote: 'Outros participantes forneceram materiais, fizeram escoltas e combateram.' },
    affected: 'Comerciantes do Entreposto Alto, transportadores da rota, o canteiro e quem cruza a Passagem.',
    responses: 'A Passagem continua sendo Fronteira: a regra de perda não mudou. O Acampamento das Garras pode se recuperar e voltar a patrulhar. Os preços se ajustam com as próximas entregas.',
  };
}

// condição concreta: guardas e contratos (também usada ao restaurar um jogo salvo)
export function applyReopenEffects() {
  // postos ao longo da Passagem: canteiro e três pontos da rota curta (posições em `layout.js`)
  for (const p of GUARD_POSTS.map((g) => ({ ...g }))) { const g = spawnEnemy('guarda', p.x, p.z, { post: p }); g.guardPost = true; }
  MARKETS.alto.sell.ferramentas = 19;
  MARKETS.vale.sell.ferramentas = 14;
}

function complete() {
  EVT.done = true; EVT.doneAt = G.time; EVT.progress = 100;
  sfx('event');
  applyReopenEffects();
  const pct = Math.round(share() * 100);
  const parts = [];
  if (EVT.delivered.ferramentas) parts.push(`entregou ${EVT.delivered.ferramentas} ferramenta${EVT.delivered.ferramentas > 1 ? 's' : ''}`);
  if (EVT.delivered.lingote) parts.push(`${EVT.delivered.lingote} lingote${EVT.delivered.lingote > 1 ? 's' : ''}`);
  if (EVT.contrib.reconhecimento) parts.push('registrou o movimento da passagem');
  if (EVT.contrib.saqueadores) parts.push('afastou saqueadores');
  if (EVT.contrib.acampamento) parts.push('participou da pressão sobre o acampamento');
  if (EVT.playerPts > 0) {
    log('legado', 'comunidade', 'A reabertura da Passagem Estreita',
      `Participou do abastecimento que reabriu a Passagem Estreita. ${parts.length ? cap(parts.join(', ')) + '.' : ''} Sua parte corresponde a cerca de ${pct}% das contribuições registradas; outros participantes responderam pelo restante.`, 'passagem');
    grantTitle('mao-passagem', 'Mão da Passagem', 'Contribuiu para a reabertura da Passagem Estreita.');
  } else {
    log('historia', 'comunidade', 'A Passagem foi reaberta', 'Outros participantes reabriram a Passagem Estreita enquanto seguia outros caminhos.', 'passagem');
  }
  emit('event-done', fourQuestions());
}
const cap = (s) => s.charAt(0).toUpperCase() + s.slice(1);
