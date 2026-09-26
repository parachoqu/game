// Desaparecimento das estrelas (cap. 14): observação, interpretação e segredo de autoria separados.
// A demo mostra só observações; a causa permanece em aberto, como no documento.
import { G, emit, clock } from '../state.js';
import { CONSTELLATIONS } from '../engine/sky.js';
import { sfx } from '../engine/audio.js';
import { first, log, grantTitle } from './book.js';
import { toast } from '../ui/hud.js';

export const SKY = { vanished: [], next: 50, observed: new Set(), instrumentGiven: false };

export function phaseOf(n = SKY.vanished.length) {
  if (n <= 3) return { name: 'Indício', desc: 'Astrônomos percebem ausências isoladas.' };
  if (n <= 8) return { name: 'Repercussão', desc: 'Referências de navegação deixam de coincidir.' };
  return { name: 'Ruptura', desc: 'Fenômenos incomuns aparecem com mais frequência.' };
}

function starLabel(o) {
  if (o.constellation < 0) return 'um ponto isolado do céu';
  return `a constelação da ${CONSTELLATIONS[o.constellation].name}`;
}
// com preposição contraída: "na constelação", "num ponto"
function starWhere(o) {
  if (o.constellation < 0) return 'num ponto isolado do céu';
  return `na constelação da ${CONSTELLATIONS[o.constellation].name}`;
}

export function updateStars(dt, night) {
  if (G.time < SKY.next || !G.sky) return;
  const o = G.sky.order[SKY.vanished.length];
  if (!o) { SKY.next = Infinity; return; }
  SKY.vanished.push({ ...o, time: G.time, when: clock().label });
  G.sky.vanish(o.index);
  SKY.next = G.time + 85 + Math.random() * 40;
  if (night > 0.5 && G.player.zone !== 'turbulenta') {
    sfx('star');
    toast('Um ponto de luz no céu tremeu e não voltou.', 'sky');
  }
  emit('star-vanished', o);
}

// No observatório: enquadra a ausência mais recente (ou a Lanterna, se nada sumiu ainda)
export function observe(night) {
  const last = SKY.vanished[SKY.vanished.length - 1];
  const target = last || { index: G.sky.order[1].index, constellation: 0 };
  const dir = G.sky.starDir(target.index);
  let caption;
  if (!last) {
    caption = { title: 'A Lanterna, ainda inteira', text: 'O instrumento registra um leve desvio de brilho na Lanterna (nome provisório). Nenhum ponto sumiu — por enquanto.' };
    first('obs-inicial', 'descobertas', 'ceu', 'Primeira leitura do céu', 'Usou o instrumento do Observatório Antigo. A Lanterna apresentava um desvio de brilho, sem ausências.');
  } else if (night < 0.45) {
    caption = { title: 'Céu claro demais', text: `À luz do dia, o anel de bronze ainda aponta para ${starLabel(last)}: o instrumento marca um vazio onde havia um ponto.` };
    if (!SKY.observed.has(last.index)) {
      SKY.observed.add(last.index);
      log('descobertas', 'ceu', 'Um desvio nos instrumentos', `No Observatório Antigo, o instrumento registrou a falta de um ponto ${starWhere(last)} (${last.when}).`, 'obs' + last.index);
    }
  } else {
    caption = { title: 'Uma ausência', text: `${cap(starWhere(last))} falta um ponto que os mapas antigos registram. A causa não é conhecida.` };
    if (!SKY.observed.has(last.index)) {
      SKY.observed.add(last.index);
      log('descobertas', 'ceu', `Uma ausência ${last.constellation < 0 ? 'num ponto isolado' : 'na ' + CONSTELLATIONS[last.constellation].name}`, `Do Observatório Antigo, observou que ${starLabel(last)} (nome provisório) perdeu um ponto de luz. Registro feito no ${last.when}.`, 'obs' + last.index);
    }
    grantTitle('olhar-observatorio', 'Olhar do Observatório', 'Registrou uma ausência no céu com os próprios olhos.');
  }
  G.cam.mode = 'sky'; G.cam.skyDir = dir;
  G.sky.markGap(target.index, !!last);
  G.sky.lineMat.opacity = 0.35;
  G.cinematic = true;
  document.body.classList.add('cine');
  emit('observe', caption);
}
const cap = (t) => t.charAt(0).toUpperCase() + t.slice(1);

export function endObserve() {
  G.cam.mode = 'follow'; G.cinematic = false;
  document.body.classList.remove('cine');
  G.sky.markGap(0, false); G.sky.lineMat.opacity = 0;
}

export function giveInstrument(P) {
  SKY.instrumentGiven = true;
  P.coins += 60;
  log('legado', 'ceu', 'Um instrumento para o Observatório', 'Forneceu um instrumento astronômico ao Observatório Antigo. As medições seguintes usam uma peça fabricada por esta personagem.', 'instrumento');
  toast('A astrônoma pagou 60 moedas pelo instrumento.', 'coin');
  sfx('event');
}
