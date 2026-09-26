// Descobertas por presença: o Livro registra o que a personagem encontrou, mesmo que não seja inédito para todos.
import { G } from '../state.js';
import { LOC, EAST_ROAD } from './layout.js';
import { first } from './book.js';

// As distâncias acompanham a nova escala do mundo (1.024 m contra os 400 m do recorte anterior):
// os raios de reconhecimento subiram junto com o tamanho de cada lugar.
const DISC = [
  ['bosque', LOC.bosque, LOC.bosque.r, 'Bosque das Forjas', 'Encontrou o bosque a oeste do Vale: veios de minério, freixos marcados para corte e erva-lume.'],
  ['canteiro', LOC.canteiro, 24, 'Canteiro da Passagem', 'Chegou ao canteiro montado junto à ponte principal, onde se reúnem materiais para reabrir a rota.'],
  ['passagem', LOC.passagem, 44, 'Passagem Estreita', 'Entrou na garganta entre a serra e o maciço. O caminho mais curto até o Alto — e o mais exposto.'],
  ['vigia', LOC.vigia, 16, 'Ruína de vigia', 'Uma torre antiga, parcialmente caída, ainda enxerga boa parte da Passagem.'],
  ['acampamento', LOC.acampamento, 30, 'Acampamento das Garras', 'Viu o acampamento das Garras num vale lateral da rota segura: tendas, fogueira e patrulhas.'],
  ['observatorio', LOC.observatorio, 32, 'Uma estrutura antiga', 'Encontrou uma estrutura antiga no alto de uma colina a oeste. Nenhum marcador indicava o caminho.'],
  ['ermos', { x: LOC.ermos.x, z: LOC.ermos.z }, LOC.ermos.rx * 0.6, 'Ermos Quebrados', 'Chegou aos Ermos Quebrados: ruínas, cristais celestes e a regra de Full Loot.'],
  ['alto', LOC.alto, LOC.alto.r, 'Entreposto Alto', 'Chegou ao Entreposto Alto, ao norte da serra, onde ferramentas valem mais.'],
  ['mina', LOC.mina, 34, 'Boca da Mina', 'Encontrou a boca da mina, encravada na encosta oeste, com entulho e vigas de sustentação.'],
  ['caverna', LOC.caverna, 30, 'Caverna do Oeste', 'Chegou à caverna no extremo oeste, onde a trilha da mina termina.'],
  ['portal', LOC.portal, 32, 'Portal Instável', 'Viu a estrutura permanente do portal instável, a nordeste, aberta sobre o planalto.'],
  ['ponte', LOC.ponteMenor, 22, 'Ponte Secundária', 'Atravessou a ponte secundária, a leste, por onde passa a rota segura.'],
  ['estrada', { x: EAST_ROAD[Math.floor(EAST_ROAD.length / 2)][0], z: EAST_ROAD[Math.floor(EAST_ROAD.length / 2)][1] }, 40, 'Estrada Longa', 'Percorreu a Estrada Longa, o caminho protegido e demorado que contorna o maciço.'],
];

let t = 0;
export function updateDiscovery(dt) {
  t -= dt; if (t > 0) return; t = 0.5;
  const P = G.player;
  for (const [k, c, r, title, text] of DISC) {
    if (G.flags['disc_' + k]) continue;
    const inside = k === 'ermos' ? P.zone === 'fullloot' : Math.hypot(P.pos.x - c.x, P.pos.z - c.z) < r;
    if (inside) {
      G.flags['disc_' + k] = true;
      first('disc-' + k, 'descobertas', 'exploracao', title, text);
    }
  }
}
