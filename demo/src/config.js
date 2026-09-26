// Dados da demo. Todos os nomes e números são provisórios (o documento-mestre deixa nomes e
// balanceamento em aberto); servem apenas para tornar o recorte jogável.

export const DAY_LENGTH = 420;      // segundos reais por dia de jogo
export const START_HOUR = 9;

// ︎ força apresentação de texto (evita que o glifo vire emoji colorido)
const t = (g) => g + '︎';

export const ITEMS = {
  minerio:     { name: 'Minério de ferro', w: 2, glyph: t('⬢'), color: '#9a7b64', kind: 'material', desc: 'Refina-se em lingotes na forja.' },
  madeira:     { name: 'Madeira de freixo', w: 1.5, glyph: t('▮'), color: '#a57a45', kind: 'material', desc: 'Cabos, arcos e estruturas.' },
  erva:        { name: 'Erva-lume', w: 0.2, glyph: t('✿'), color: '#8cc97a', kind: 'material', desc: 'Base de preparados alquímicos.' },
  pele:        { name: 'Pele de garra', w: 1, glyph: t('◗'), color: '#b48a62', kind: 'material', desc: 'Couro para gibões e empunhaduras.' },
  cristal:     { name: 'Cristal celeste', w: 1, glyph: t('◆'), color: '#8fd6ea', kind: 'material', desc: 'Só brota nos Ermos. Valioso no Alto.' },
  fragmento:   { name: 'Fragmento anômalo', w: 0.5, glyph: t('✦'), color: '#b892ff', kind: 'material', desc: 'Trazido de uma Região Turbulenta.' },
  lingote:     { name: 'Lingote de ferro', w: 1.5, glyph: t('▬'), color: '#c4bfb3', kind: 'material', desc: 'Material de forja e de reparo.' },
  ferramentas: { name: 'Ferramentas', w: 3, glyph: t('⚒'), color: '#dcaa48', kind: 'goods', desc: 'Muito procuradas enquanto a Passagem se recupera.' },
  instrumento: { name: 'Instrumento astronômico', w: 2, glyph: t('⊕'), color: '#e6c774', kind: 'goods', desc: 'Mede desvios no céu.' },
  pocao:       { name: 'Poção de erva-lume', w: 0.3, glyph: t('⚱'), color: '#d8674f', kind: 'consumable', desc: 'Recupera 45 de vida (tecla 1).' },
  // Equipamentos existentes
  espada:   { name: 'Espada de ferro', w: 3, glyph: t('†'), color: '#cfd6da', kind: 'weapon', family: 'espada', req: 'espadachim', desc: 'Técnicas: Investida e Golpe Giratório.' },
  arco:     { name: 'Arco de freixo', w: 1.5, glyph: t('⌒'), color: '#c79a5c', kind: 'weapon', family: 'arco', req: 'arqueiro', desc: 'Técnicas: Tiro Carregado e Recuo com Disparo.' },
  martelo:  { name: 'Martelo de oficina', w: 2.5, glyph: t('⚒'), color: '#b0a18a', kind: 'weapon', family: 'martelo', req: null, desc: 'Ferramenta de trabalho. Técnicas: Golpe Atordoante e Reparo de Campo.' },
  gibao:    { name: 'Gibão de couro', w: 4, glyph: t('▣'), color: '#a47a52', kind: 'armor', hp: 20, speed: 1, desc: '+20 de vida. Leve.' },
  cota:     { name: 'Cota de malha', w: 8, glyph: t('▦'), color: '#a9b0b3', kind: 'armor', hp: 45, speed: 0.93, desc: '+45 de vida. Reduz um pouco o deslocamento.' },
  bolsa:    { name: 'Bolsa de viagem', w: 0.5, glyph: t('◘'), color: '#8d6b4a', kind: 'bag', cap: 15, desc: '+15 kg de capacidade pessoal.' },
  montaria: { name: 'Cavalo de carga', w: 0, glyph: t('♞'), color: '#a8875e', kind: 'mount', cap: 60, desc: 'Item de montaria. Alforjes de 60 kg; mais rápido na estrada.' },

  // Cap. 21 — Famílias de armas complementares
  manoplas: { name: 'Manoplas de ferro', w: 2.2, glyph: t('⚔'), color: '#d2b48c', kind: 'weapon', family: 'manoplas', req: 'lutador', desc: 'Combate corpo a corpo ágil. Técnicas: Rajada Rápida e Avanço de Impacto.' },
  maca:     { name: 'Maça com cravos', w: 3.2, glyph: t('ϙ'), color: '#c49a45', kind: 'weapon', family: 'maca', req: 'concussao_corte', desc: 'Golpes contundentes pesados. Técnicas: Golpe Esmagador e Onda de Choque.' },
  machado:  { name: 'Machado de batalha', w: 3.4, glyph: t('🪓'), color: '#ba604d', kind: 'weapon', family: 'machado', req: 'concussao_corte', desc: 'Cortes agressivos e comprometidos. Técnicas: Machadada Brutal e Dilacerar.' },
  adaga:    { name: 'Adagas de aço', w: 1.2, glyph: t('🗡'), color: '#b0c4de', kind: 'weapon', family: 'adaga', req: 'assassino', desc: 'Curto alcance e janelas de oportunidade. Técnicas: Passo das Sombras e Golpe Preciso.' },
  lanca:    { name: 'Lança de guarda', w: 2.8, glyph: t('⤊'), color: '#deb887', kind: 'weapon', family: 'lanca', req: 'combatente_haste', desc: 'Controle de distância e estocadas. Técnicas: Estocada Penetrante e Varrer Distância.' },
  foice:    { name: 'Foice de guerra', w: 2.9, glyph: t('⟆'), color: '#8b008b', kind: 'weapon', family: 'foice', req: 'combatente_haste', desc: 'Arco cortante amplo com identidade própria. Técnicas: Ceifa Circular e Puxão Ceifador.' },
  besta:    { name: 'Besta de freixo', w: 3.0, glyph: t('⤅'), color: '#cd853f', kind: 'weapon', family: 'besta', req: 'assassino', desc: 'Disparo reto perfurante cadenciado. Técnicas: Virote Perfurante e Disparo de Repulsão.' },

  // Cap. 22 — Escolas e tradições de magia
  cajado_gelo:         { name: 'Cajado do Gelo', w: 2.0, glyph: t('❄'), color: '#70d6ff', kind: 'weapon', family: 'cajado_gelo', req: 'magia_elemental', desc: 'Manipulação de água e frio. Técnicas: Seta Gélida e Prisão de Gelo.' },
  tomo_fogo:           { name: 'Tomo da Chama Viva', w: 1.8, glyph: t('🔥'), color: '#ff6b35', kind: 'weapon', family: 'tomo_fogo', req: 'magia_elemental', desc: 'Calor e combustão intensa. Técnicas: Bola de Fogo e Erupção Ígnea.' },
  cajado_natureza:     { name: 'Cajado das Raízes', w: 2.1, glyph: t('🌿'), color: '#52b788', kind: 'weapon', family: 'cajado_natureza', req: 'magia_natural', desc: 'Ciclo vegetal e da terra. Técnicas: Enraizar e Florescer Vital.' },
  tomo_vento:          { name: 'Tomo dos Ventos', w: 1.6, glyph: t('🌪'), color: '#90e0ef', kind: 'weapon', family: 'tomo_vento', req: 'magia_elemental', desc: 'Deslocamento e pressão eólica. Técnicas: Lufada Repulsiva e Salto dos Ventos.' },
  tomo_maldicao:       { name: 'Tomo das Maldições', w: 1.7, glyph: t('👁'), color: '#9b5de5', kind: 'weapon', family: 'tomo_maldicao', req: 'artes_espirituais', desc: 'Marcas de enfraquecimento e aflição. Técnicas: Marca Corruptora e Dreno Vital.' },
  fetiche_metamorfose: { name: 'Amuleto da Fera', w: 0.8, glyph: t('🐾'), color: '#bc6c25', kind: 'weapon', family: 'metamorfose', req: 'magia_natural', desc: 'Alteração corporal e pacto animal. Técnicas: Forma da Fera e Rugido Intimidador.' },
  cajado_vital:        { name: 'Bastão Vital', w: 1.9, glyph: t('✨'), color: '#a7c957', kind: 'weapon', family: 'cajado_vital', req: 'artes_espirituais', desc: 'Restauração e estabilização física. Técnicas: Toque Vital e Pulso Restaurador.' },
  tomo_sagrado:        { name: 'Tomo da Luz Celeste', w: 1.8, glyph: t('☼'), color: '#ffd166', kind: 'weapon', family: 'tomo_sagrado', req: 'artes_espirituais', desc: 'Purificação e bênçãos protetoras. Técnicas: Escudo Radiante e Clarão Sagrado.' },
};

export const GEAR_KINDS = new Set(['weapon', 'armor', 'bag', 'mount']);

export const TRAININGS = {
  espadachim:        { name: 'Espadachim I', cost: 30, desc: 'Libera espadas e suas técnicas de corte e estocada.' },
  arqueiro:          { name: 'Arqueiro I', cost: 30, desc: 'Libera arcos e suas técnicas de tiro à distância.' },
  artesao:           { name: 'Artesão I', cost: 25, desc: 'Receitas de forja de armas, armaduras e equipamentos.' },
  alquimista:        { name: 'Alquimista I', cost: 20, desc: 'Preparo de poções e compostos alquímicos.' },
  lutador:           { name: 'Lutador I', cost: 30, desc: 'Libera manoplas de batalha e combate de pressão corporal.' },
  combatente_haste:  { name: 'Mestre de Haste I', cost: 35, desc: 'Libera lanças e foices de guerra para controle de área.' },
  concussao_corte:   { name: 'Impacto & Corte I', cost: 35, desc: 'Libera maças contundentes e machados pesados de guerra.' },
  assassino:         { name: 'Batedor Furtivo I', cost: 35, desc: 'Libera adagas ágeis e disparos perfurantes de besta.' },
  magia_elemental:   { name: 'Magia Elemental I', cost: 40, desc: 'Tradições de Água/Gelo, Fogo/Lava e Vento/Ar.' },
  magia_natural:     { name: 'Tradição Natural I', cost: 40, desc: 'Raízes da terra e metamorfose corporal na Forma da Fera.' },
  artes_espirituais: { name: 'Artes Espirituais I', cost: 40, desc: 'Bênçãos sagradas, recuperação vital e marcas de maldição.' },
};

export const RECIPES = [
  // Básicos e materiais
  { out: 'lingote', qty: 1, in: { minerio: 2 }, req: null, note: 'Refino' },
  { out: 'ferramentas', qty: 1, in: { lingote: 1, madeira: 1 }, req: null, note: 'Ofício básico' },
  { out: 'gibao', qty: 1, in: { pele: 3 }, req: null, note: 'Couro' },
  { out: 'cota', qty: 1, in: { lingote: 4 }, req: 'artesao', note: 'Forja' },
  { out: 'instrumento', qty: 1, in: { lingote: 2, cristal: 1 }, req: 'artesao', note: 'Precisão' },
  { out: 'pocao', qty: 2, in: { erva: 3 }, req: 'alquimista', note: 'Alquimia' },

  // Armas de Aço e Haste (Cap. 21)
  { out: 'espada', qty: 1, in: { lingote: 3, madeira: 1 }, req: 'artesao', note: 'Forja' },
  { out: 'arco', qty: 1, in: { madeira: 3, pele: 1 }, req: 'artesao', note: 'Carpintaria' },
  { out: 'martelo', qty: 1, in: { lingote: 2, madeira: 1 }, req: 'artesao', note: 'Forja' },
  { out: 'manoplas', qty: 1, in: { lingote: 2, pele: 2 }, req: 'artesao', note: 'Forja & Couro' },
  { out: 'maca', qty: 1, in: { lingote: 3, madeira: 1 }, req: 'artesao', note: 'Forja' },
  { out: 'machado', qty: 1, in: { lingote: 3, madeira: 2 }, req: 'artesao', note: 'Forja' },
  { out: 'adaga', qty: 1, in: { lingote: 2, pele: 1 }, req: 'artesao', note: 'Forja' },
  { out: 'lanca', qty: 1, in: { lingote: 2, madeira: 3 }, req: 'artesao', note: 'Haste' },
  { out: 'foice', qty: 1, in: { lingote: 3, madeira: 2 }, req: 'artesao', note: 'Forja' },
  { out: 'besta', qty: 1, in: { madeira: 3, lingote: 2, pele: 1 }, req: 'artesao', note: 'Engenho' },

  // Focos Mágicos das 8 Tradições (Cap. 22)
  { out: 'cajado_gelo', qty: 1, in: { madeira: 2, cristal: 1 }, req: 'artesao', note: 'Canalização' },
  { out: 'tomo_fogo', qty: 1, in: { pele: 2, cristal: 1, fragmento: 1 }, req: 'artesao', note: 'Encadernação' },
  { out: 'cajado_natureza', qty: 1, in: { madeira: 3, erva: 4 }, req: 'artesao', note: 'Sintonia' },
  { out: 'tomo_vento', qty: 1, in: { pele: 2, madeira: 1, fragmento: 1 }, req: 'artesao', note: 'Encadernação' },
  { out: 'tomo_maldicao', qty: 1, in: { pele: 2, fragmento: 2 }, req: 'artesao', note: 'Marcas' },
  { out: 'fetiche_metamorfose', qty: 1, in: { pele: 4, madeira: 1 }, req: 'artesao', note: 'Tradição Primeva' },
  { out: 'cajado_vital', qty: 1, in: { madeira: 2, erva: 3, cristal: 1 }, req: 'artesao', note: 'Restauração' },
  { out: 'tomo_sagrado', qty: 1, in: { pele: 2, lingote: 1, cristal: 2 }, req: 'artesao', note: 'Luz Sagrada' },
];

export const MARKETS = {
  vale: {
    name: 'Mercado do Vale',
    buy: {
      minerio: 4, madeira: 3, lingote: 10, pocao: 12, martelo: 12, gibao: 22, espada: 42, arco: 38, bolsa: 35,
      manoplas: 36, maca: 44, machado: 46, adaga: 32, lanca: 40, foice: 45, besta: 48,
      cajado_gelo: 52, tomo_fogo: 54, cajado_natureza: 50, tomo_vento: 50,
      tomo_maldicao: 58, fetiche_metamorfose: 48, cajado_vital: 52, tomo_sagrado: 58,
    },
    sell: {
      minerio: 2, madeira: 1.5, erva: 1, pele: 4, lingote: 6, ferramentas: 11, cristal: 18, fragmento: 20, instrumento: 30,
      manoplas: 18, maca: 22, machado: 23, adaga: 16, lanca: 20, foice: 22, besta: 24,
      cajado_gelo: 26, tomo_fogo: 27, cajado_natureza: 25, tomo_vento: 25,
      tomo_maldicao: 29, fetiche_metamorfose: 24, cajado_vital: 26, tomo_sagrado: 29,
    },
  },
  alto: {
    name: 'Mercado do Alto',
    buy: {
      pele: 9, cristal: 62, pocao: 14, lingote: 14, madeira: 5,
      cajado_gelo: 68, tomo_fogo: 72, tomo_sagrado: 75, tomo_maldicao: 75,
    },
    sell: {
      ferramentas: 28, lingote: 10, pele: 6, cristal: 40, fragmento: 45, instrumento: 55, minerio: 3, erva: 2,
      manoplas: 26, maca: 30, machado: 32, adaga: 24, lanca: 28, foice: 32, besta: 34,
      cajado_gelo: 42, tomo_fogo: 44, cajado_natureza: 38, tomo_vento: 38,
      tomo_maldicao: 46, fetiche_metamorfose: 36, cajado_vital: 42, tomo_sagrado: 46,
    },
  },
};
export const MOUNT_PRICE = 80;
export const RESTART_KIT_PRICE = 8;

export const WEAPONS = {
  punhos:              { name: 'Punhos', basic: { type: 'melee', dmg: 6, range: 1.9, arc: 1.7, windup: 0.08, recover: 0.28 }, skills: [] },
  espada:              { name: 'Espada', basic: { type: 'melee', dmg: 14, range: 2.8, arc: 2.0, windup: 0.1, recover: 0.34 }, skills: ['investida', 'giro'] },
  arco:                { name: 'Arco', basic: { type: 'ranged', dmg: 12, speed: 45, windup: 0.16, recover: 0.42 }, skills: ['tiroCarregado', 'recuo'] },
  martelo:             { name: 'Martelo', basic: { type: 'melee', dmg: 13, range: 2.5, arc: 1.6, windup: 0.2, recover: 0.48 }, skills: ['atordoar', 'reparoCampo'] },
  // Famílias do Cap. 21
  manoplas:            { name: 'Manoplas', basic: { type: 'melee', dmg: 10, range: 2.1, arc: 1.8, windup: 0.06, recover: 0.22 }, skills: ['comboManopla', 'avancoImpacto'] },
  maca:                { name: 'Maça', basic: { type: 'melee', dmg: 15, range: 2.5, arc: 1.7, windup: 0.18, recover: 0.44 }, skills: ['golpeEsmagador', 'ondaChoque'] },
  machado:             { name: 'Machado', basic: { type: 'melee', dmg: 16, range: 2.6, arc: 1.9, windup: 0.17, recover: 0.42 }, skills: ['machadadaPesada', 'dilacerar'] },
  adaga:               { name: 'Adagas', basic: { type: 'melee', dmg: 9, range: 2.2, arc: 1.5, windup: 0.06, recover: 0.2 }, skills: ['passoSombras', 'golpePreciso'] },
  lanca:               { name: 'Lança', basic: { type: 'melee', dmg: 13, range: 3.6, arc: 1.2, windup: 0.12, recover: 0.38 }, skills: ['estocadaLonga', 'varrerDistancia'] },
  foice:               { name: 'Foice', basic: { type: 'melee', dmg: 14, range: 3.0, arc: 2.8, windup: 0.16, recover: 0.4 }, skills: ['ceifaCircular', 'puxaoFoice'] },
  besta:               { name: 'Besta', basic: { type: 'ranged', dmg: 18, speed: 58, windup: 0.24, recover: 0.52 }, skills: ['tiroBesta', 'disparoRepulsao'] },
  // Tradições do Cap. 22
  cajado_gelo:         { name: 'Gelo Perpétuo', basic: { type: 'spell', school: 'ice', dmg: 11, speed: 38, windup: 0.15, recover: 0.38 }, skills: ['setaGelo', 'prisaoGelo'] },
  tomo_fogo:           { name: 'Chama Viva', basic: { type: 'spell', school: 'fire', dmg: 15, speed: 40, windup: 0.18, recover: 0.42 }, skills: ['bolaFogo', 'erupcaoFogo'] },
  cajado_natureza:     { name: 'Natureza & Raízes', basic: { type: 'spell', school: 'nature', dmg: 10, speed: 36, windup: 0.14, recover: 0.36 }, skills: ['enraizar', 'bencaoTerra'] },
  tomo_vento:          { name: 'Vento & Ar', basic: { type: 'spell', school: 'wind', dmg: 11, speed: 46, windup: 0.12, recover: 0.32 }, skills: ['lufadaVento', 'saltoVento'] },
  tomo_maldicao:       { name: 'Maldições Sombrias', basic: { type: 'spell', school: 'curse', dmg: 12, speed: 38, windup: 0.16, recover: 0.4 }, skills: ['marcaCorruptora', 'drenoVital'] },
  metamorfose:         { name: 'Forma da Fera', basic: { type: 'melee', dmg: 17, range: 2.7, arc: 2.2, windup: 0.11, recover: 0.3 }, skills: ['formaFera', 'rugidoFera'] },
  cajado_vital:        { name: 'Vitalidade', basic: { type: 'spell', school: 'vital', dmg: 9, speed: 36, windup: 0.14, recover: 0.35 }, skills: ['curaVital', 'auraRestauracao'] },
  tomo_sagrado:        { name: 'Luz Sagrada', basic: { type: 'spell', school: 'holy', dmg: 13, speed: 42, windup: 0.16, recover: 0.38 }, skills: ['escudoRadiante', 'purificacaoSagrada'] },
};

export const SKILLS = {
  // Técnicas existentes
  investida:          { name: 'Investida', cost: 30, cd: 6, desc: 'Avança 7 m e fere quem estiver no caminho.' },
  giro:               { name: 'Golpe Giratório', cost: 35, cd: 9, desc: 'Breve preparo; atinge tudo ao redor e empurra.' },
  tiroCarregado:      { name: 'Tiro Carregado', cost: 30, cd: 7, desc: 'Prepara 0,6 s e dispara uma flecha perfurante.' },
  recuo:              { name: 'Recuo com Disparo', cost: 28, cd: 8, desc: 'Salta para trás e dispara.' },
  atordoar:           { name: 'Golpe Atordoante', cost: 25, cd: 8, desc: 'Golpe lento que atordoa por 1,5 s.' },
  reparoCampo:        { name: 'Reparo de Campo', cost: 20, cd: 30, desc: 'Recupera 25% da condição da arma e da armadura.' },

  // Cap. 21 — Técnicas das Famílias de Armas
  comboManopla:       { name: 'Rajada Rápida', cost: 22, cd: 5, desc: 'Sequência rápida de 3 socos contundentes em alta velocidade.' },
  avancoImpacto:      { name: 'Avanço de Impacto', cost: 28, cd: 7, desc: 'Projeta o corpo à frente desferindo um soco demolidor que atordoa.' },
  golpeEsmagador:     { name: 'Golpe Esmagador', cost: 30, cd: 7, desc: 'Impacto violento descendente que ignora armadura e reduz a velocidade do alvo.' },
  ondaChoque:         { name: 'Onda de Choque', cost: 35, cd: 9, desc: 'Bate a maça no chão com tremendo impacto, repelindo todos ao redor.' },
  machadadaPesada:    { name: 'Machadada Brutal', cost: 32, cd: 6, desc: 'Golpe comprometido e pesado que causa dano massivo frontal.' },
  dilacerar:          { name: 'Dilacerar', cost: 28, cd: 8, desc: 'Corte semicircular afiado que abre feridas contínuas no inimigo.' },
  passoSombras:       { name: 'Passo das Sombras', cost: 24, cd: 6, desc: 'Desliza velozmente e golpeia um ponto fraco do oponente.' },
  golpePreciso:       { name: 'Golpe Preciso', cost: 30, cd: 8, desc: 'Estocada cirúrgica de curto alcance com altíssimo dano crítico.' },
  estocadaLonga:      { name: 'Estocada Penetrante', cost: 26, cd: 6, desc: 'Golpe longo e direto de até 4,5 m que mantém o alvo à distância.' },
  varrerDistancia:    { name: 'Varrer Distância', cost: 30, cd: 8, desc: 'Varredura circular ampla com a haste que repele atacantes próximos.' },
  ceifaCircular:      { name: 'Ceifa Circular', cost: 32, cd: 7, desc: 'Giro ceifador com a foice que corta múltiplos alvos ao redor.' },
  puxaoFoice:         { name: 'Puxão Ceifador', cost: 26, cd: 8, desc: 'Engata a lâmina curva no oponente e puxa-o em sua direção.' },
  tiroBesta:          { name: 'Virote Perfurante', cost: 28, cd: 7, desc: 'Disparo potente em linha reta que atravessa múltiplos inimigos.' },
  disparoRepulsao:    { name: 'Tiro de Repulsão', cost: 26, cd: 8, desc: 'Disparo de altíssima pressão que arremessa o alvo para trás.' },

  // Cap. 22 — Tradições e Expressões Mágicas
  setaGelo:           { name: 'Seta Gélida', cost: 25, cd: 6, desc: 'Dispara um projétil de água congelada que causa lentidão ao acertar.' },
  prisaoGelo:         { name: 'Prisão de Gelo', cost: 38, cd: 10, desc: 'Forma um anel gélido no solo que congela e danifica inimigos.' },
  bolaFogo:           { name: 'Bola de Fogo', cost: 32, cd: 7, desc: 'Esfera ígnea de combustão intensa que explode no impacto.' },
  erupcaoFogo:        { name: 'Erupção Ígnea', cost: 40, cd: 11, desc: 'Detona o chão sob os adversários em uma explosão de labaredas.' },
  enraizar:           { name: 'Enraizamento', cost: 28, cd: 8, desc: 'Raízes brotam do chão imobilizando o adversário por 2 s.' },
  bencaoTerra:        { name: 'Bênção da Terra', cost: 25, cd: 14, desc: 'Sintonia vital que restaura vigor e concede proteção física temporária.' },
  lufadaVento:        { name: 'Lufada Repulsiva', cost: 24, cd: 6, desc: 'Rajada de pressão eólica em cone que afasta inimigos e desvia projéteis.' },
  saltoVento:         { name: 'Salto dos Ventos', cost: 22, cd: 7, desc: 'Impulso de vento veloz que projeta o conjurador na direção desejada.' },
  marcaCorruptora:    { name: 'Marca Corruptora', cost: 28, cd: 8, desc: 'Marca o alvo com maldição que causa dano persistente com leitura clara.' },
  drenoVital:         { name: 'Dreno Vital', cost: 32, cd: 10, desc: 'Canaliza energia corrupta, drenando pontos de vida do oponente.' },
  formaFera:          { name: 'Forma da Fera', cost: 35, cd: 16, desc: 'Metamorfoseia o corpo em forma bestial (urso), ganhando vigor e patadas ferozes.' },
  rugidoFera:         { name: 'Rugido Intimidador', cost: 25, cd: 9, desc: 'Rugido estarrecedor que desestabiliza e atordoa oponentes em área.' },
  curaVital:          { name: 'Toque Vital', cost: 30, cd: 9, desc: 'Concentra o ciclo vital para recuperar 50 pontos de vida do corpo.' },
  auraRestauracao:    { name: 'Aura Restauradora', cost: 38, cd: 15, desc: 'Cria uma área de florescência no solo com regeneração de vida contínua.' },
  escudoRadiante:     { name: 'Escudo Radiante', cost: 30, cd: 12, desc: 'Purificação celeste que envolve o conjurador em uma barreira luminosa protetora.' },
  purificacaoSagrada: { name: 'Clarão Sagrado', cost: 35, cd: 10, desc: 'Emanação sagrada resplandecente que afasta sombras e queima inimigos próximos.' },
};

export const ZONES = {
  protegida: {
    name: 'Protegida', color: '#62b98e', mark: '◈',
    rule: 'Sem ataque livre entre jogadores. Morrer: retorno ao abrigo e desgaste, sem saque.',
    death: 'Retorno ao abrigo, desgaste do equipamento. Nada é saqueado.',
  },
  fronteira: {
    name: 'Fronteira', color: '#e3b24c', mark: '◭',
    rule: 'Carga transportada (bolsa e alforjes) pode ser saqueada. Conjunto equipado é preservado.',
    death: 'A carga transportada fica no local. Equipamento, montaria, moedas e conhecimentos permanecem.',
  },
  fullloot: {
    name: 'Full Loot', color: '#d85a43', mark: '✕',
    rule: 'Tudo o que você leva — carga, equipamento e montaria — pode ser saqueado ou destruído.',
    death: 'Carga, equipamento e item de montaria ficam no local; uma parte é destruída.',
  },
  turbulenta: {
    name: 'Turbulenta', color: '#a07cf2', mark: '◌',
    rule: 'Full Loot. Identidades ocultas. Saia por uma extração antes do colapso.',
    death: 'Tudo o que você leva fica na região.',
  },
};

export const ENEMIES = {
  garra:         { name: 'Garra', faction: 'beast', hp: 40, speed: 6.4, dmg: 10, range: 2.3, arc: 1.4, windup: 0.62, cd: 1.5, aggro: 17, drop: { pele: [1, 1] } },
  garraLider:    { name: 'Garra-líder', faction: 'beast', hp: 120, speed: 6, dmg: 18, range: 3.4, shape: 'circle', windup: 0.9, cd: 2.2, aggro: 20, drop: { pele: [2, 3] }, scale: 1.45 },
  garraPalida:   { name: 'Garra pálida', faction: 'beast', hp: 60, speed: 6.8, dmg: 13, range: 2.4, arc: 1.4, windup: 0.55, cd: 1.4, aggro: 19, drop: { pele: [1, 2] }, scale: 1.15 },
  saqueador:     { name: 'Saqueador', faction: 'bandit', hp: 55, speed: 6.1, dmg: 12, range: 2.6, arc: 1.7, windup: 0.55, cd: 1.35, aggro: 19, weapon: 'espada', coins: [4, 9] },
  saqueadorArco: { name: 'Saqueador arqueiro', faction: 'bandit', hp: 42, speed: 5.8, dmg: 10, range: 17, ranged: true, windup: 0.85, cd: 1.9, aggro: 22, weapon: 'arco', coins: [4, 8] },
  saqueadorLanca:{ name: 'Saqueador lanceiro', faction: 'bandit', hp: 58, speed: 6.0, dmg: 13, range: 3.4, arc: 1.4, windup: 0.58, cd: 1.45, aggro: 20, weapon: 'lanca', coins: [5, 10] },
  sombraEspada:  { name: 'Figura desconhecida', faction: 'shadow', hp: 75, speed: 6.3, dmg: 14, range: 2.8, arc: 1.8, windup: 0.6, cd: 1.5, aggro: 22, weapon: 'espada', drop: { fragmento: [1, 1] } },
  sombraArco:    { name: 'Figura desconhecida', faction: 'shadow', hp: 60, speed: 6, dmg: 11, range: 18, ranged: true, windup: 0.8, cd: 1.8, aggro: 24, weapon: 'arco', drop: { fragmento: [1, 1] } },
  sombraMagica:  { name: 'Figura desconhecida', faction: 'shadow', hp: 70, speed: 6.1, dmg: 15, range: 18, ranged: true, windup: 0.75, cd: 1.7, aggro: 23, weapon: 'cajado_gelo', drop: { fragmento: [1, 2] } },
};

export const ORIGINS = {
  humano: { name: 'Humano', skin: '#c99a78', cloth: '#3f6f8f', trim: '#c9a25a', hint: 'Povos das estradas e dos canais.' },
  elfo:   { name: 'Elfo', skin: '#e2c2a4', cloth: '#476b4a', trim: '#d6c28a', hint: 'Herdeiros de antigas estruturas do oeste.' },
  anao:   { name: 'Anão', skin: '#c08a6a', cloth: '#7a4a36', trim: '#d9a441', hint: 'Cidades escalonadas nas encostas.' },
  orc:    { name: 'Orc', skin: '#7f9470', cloth: '#5b4a6e', trim: '#c7b07a', hint: 'Clãs de caravaneiros e pastores.' },
};

// Cap. 23 — Arquétipos de entrada sugeridos (sem aprisionamento em classe permanente)
export const STARTS = {
  espadachim: { label: 'Espadachim I', weapon: 'espada', desc: 'Começa com uma espada de ferro e suas técnicas versáteis de corte e estocada.' },
  arqueiro:   { label: 'Arqueiro I', weapon: 'arco', desc: 'Começa com um arco de freixo e suas técnicas de precisão à distância.' },
  artesao:    { label: 'Artesão I', weapon: 'martelo', desc: 'Começa com martelo de oficina e receitas de forja e fabricação.' },
  lutador:    { label: 'Lutador I', weapon: 'manoplas', desc: 'Começa com manoplas de ferro e combate de pressão corporal em curta distância.' },
  lanceiro:   { label: 'Lanceiro I', weapon: 'lanca', desc: 'Começa com lança de guarda e técnicas de controle de distância e penetração.' },
  mago_gelo:  { label: 'Mago do Gelo I', weapon: 'cajado_gelo', desc: 'Começa com cajado gélido e manipulação de água, fluxo e contenção.' },
  piromante:  { label: 'Piromante I', weapon: 'tomo_fogo', desc: 'Começa com tomo de chamas vivas e combustão explosiva intensa.' },
  metamorfo:  { label: 'Metamorfo I', weapon: 'fetiche_metamorfose', desc: 'Começa com amuleto primevo e a tradição de assumir a Forma da Fera.' },
};
