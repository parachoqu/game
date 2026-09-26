// Acampamento reativo (cap. 12): estados observáveis em vez de simulação irrestrita.
import { G, on, emit } from '../state.js';
import { LOC, CAMP_DISPLACED, CAMP_PATROL } from './layout.js';
import { spawnEnemy, removeEnemy } from './enemies.js';
import { contribute } from './event.js';
import { tally, first } from './book.js';
import { sfx } from '../engine/audio.js';
import { toast } from '../ui/hud.js';

export const CAMP = { pop: 9, state: 'estabelecido', since: 0, pending: true, members: [], playerKills: 0, regenT: 0, extraPalisade: null };
export const CAMP_LABEL = {
  estabelecido: { name: 'Estabelecido', sign: 'Patrulhas percorrem a Passagem; fogueiras acesas.' },
  pressionado: { name: 'Pressionado', sign: 'Patrulhas recolhidas; defesa reforçada na entrada.' },
  deslocado: { name: 'Deslocado', sign: 'Acampamento vazio; trilhas novas rumo às Colinas do Oeste.' },
  recuperacao: { name: 'Em recuperação', sign: 'Pequenos grupos retornam e reconstroem.' },
};
const C = LOC.acampamento;
// Deslocamento: encosta a oeste do acampamento, sobre a rota segura. Patrulha: trecho da rota
// curta que passa pela garganta. As posições ficam em `layout.js`, junto das clareiras.
const DISPLACED = CAMP_DISPLACED;
const gorgePatrol = CAMP_PATROL;

function compose(state) {
  // [tipo, x, z, opções]
  if (state === 'estabelecido') return [
    ['garraLider', C.x + 2, C.z], ['garra', C.x - 4, C.z + 3], ['garra', C.x - 3, C.z - 4], ['garra', C.x + 5, C.z + 4],
    ['garra', gorgePatrol[0][0], gorgePatrol[0][1], { patrol: gorgePatrol }],
    ['garra', gorgePatrol[2][0], gorgePatrol[2][1], { patrol: [...gorgePatrol].reverse() }],
  ];
  if (state === 'pressionado') return [
    ['garraLider', C.x - 6, C.z], ['garra', C.x - 9, C.z + 2], ['garra', C.x - 9, C.z - 2], ['garra', C.x - 5, C.z + 4],
  ];
  if (state === 'deslocado') return [
    ['garra', DISPLACED.x, DISPLACED.z], ['garra', DISPLACED.x + 3, DISPLACED.z + 2], ['garra', DISPLACED.x - 2, DISPLACED.z + 3],
  ];
  return [['garra', C.x, C.z + 2], ['garra', C.x + 3, C.z - 2]];
}
function center(state) { return state === 'deslocado' ? DISPLACED : C; }

function applyComposition() {
  for (const e of CAMP.members) if (e.alive) removeEnemy(e);
  CAMP.members = compose(CAMP.state).map(([type, x, z, o]) => {
    const e = spawnEnemy(type, x, z, { leash: 38, ...(o || {}) });
    e.camp = true; return e;
  });
  CAMP.pending = false;
}

function setState(s) {
  if (CAMP.state === s) return;
  const prev = CAMP.state;
  CAMP.state = s; CAMP.since = G.time; CAMP.pending = true;
  emit('camp-state', { from: prev, to: s });
  if (s === 'pressionado') {
    contribute('acampamento', 10);
    sfx('horn');
    toast('O Acampamento das Garras recolheu as patrulhas da Passagem.', 'event');
    if (CAMP.playerKills >= 2) first('camp-pressionado', 'feitos', 'combate', 'Pressão sobre o acampamento', 'Participou dos combates que fizeram o Acampamento das Garras recolher as patrulhas da Passagem Estreita.');
  }
  if (s === 'deslocado') {
    contribute('acampamento', 6);
    toast('As Garras abandonaram o acampamento. Trilhas novas seguem para as Colinas do Oeste.', 'event');
    if (CAMP.playerKills >= 4) first('camp-deslocado', 'feitos', 'combate', 'O acampamento se deslocou', 'Esteve entre os que pressionaram as Garras até que deixassem o acampamento. Parte delas foi vista a oeste.');
  }
  if (s === 'recuperacao') toast('Pequenos grupos de Garras voltaram ao acampamento.', 'info');
  if (s === 'estabelecido' && prev === 'recuperacao') toast('O Acampamento das Garras voltou a se estabelecer.', 'info');
  CAMP.playerKills = 0;
}

export function initCamp() {
  on('enemy-killed', ({ e, byPlayer }) => {
    if (!e.camp) return;
    CAMP.pop -= e.type === 'garraLider' ? 2 : 1;
    if (byPlayer) {
      CAMP.playerKills++;
      tally('garras', 'feitos', 'combate', (n) => ({ title: 'Enfrentou as Garras', text: `${n} Garra${n > 1 ? 's' : ''} abatida${n > 1 ? 's' : ''} nos arredores do acampamento e da Passagem.` }));
    }
    if (CAMP.state === 'estabelecido' && CAMP.pop <= 5) setState('pressionado');
    else if ((CAMP.state === 'pressionado' || CAMP.state === 'recuperacao') && CAMP.pop <= 2) setState('deslocado');
  });
  applyComposition();
}

export function updateCamp(dt) {
  const P = G.player;
  const t = G.time - CAMP.since;
  if (CAMP.state === 'deslocado' && t > 110) { CAMP.pop = 2; setState('recuperacao'); }
  if (CAMP.state === 'recuperacao') {
    CAMP.regenT += dt;
    if (CAMP.regenT > 25) { CAMP.regenT = 0; CAMP.pop++; if (CAMP.pop >= 6) setState('estabelecido'); }
  }
  if (CAMP.state === 'estabelecido' && CAMP.pop < 9) { CAMP.regenT += dt; if (CAMP.regenT > 40) { CAMP.regenT = 0; CAMP.pop++; } }
  if (CAMP.pending) {
    const c = center(CAMP.state);
    const busy = CAMP.members.some((e) => e.alive && e.state !== 'idle');
    if (!busy && Math.hypot(P.pos.x - c.x, P.pos.z - c.z) > 45 && Math.hypot(P.pos.x - C.x, P.pos.z - C.z) > 45) applyComposition();
  }
  if (CAMP.extraPalisade) CAMP.extraPalisade.visible = CAMP.state === 'pressionado';
}
