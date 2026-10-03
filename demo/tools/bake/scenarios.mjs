// Roteiros dos testes de paridade da simulação (rodados pelo harness no bake, reproduzidos pelo C++).
//
// Cada roteiro: semente do Math.random, perfil, ajustes do jogador, inimigos, e a entrada quadro a
// quadro (teclas seguradas, toques, giro da câmera, mira, ações de painel). O mesmo JSON vai para
// cpp/tests/parity/fixtures/sim-parity.json junto com o rastro que a demo produziu.

// Compila partes em quadros: {from, to, keys, cam, aim, right, ui} valem num intervalo [from, to);
// {at, press, fire, calls} valem num quadro.
export function frames(n, parts) {
  const out = Array.from({ length: n }, () => ({}));
  for (const p of parts) {
    if (p.at != null) {
      const f = out[p.at];
      if (!f) throw new Error(`roteiro: quadro ${p.at} fora de 0..${n - 1}`);
      if (p.press) f.press = [...(f.press || []), ...p.press];
      if (p.fire) f.fire = true;
      if (p.calls) f.calls = [...(f.calls || []), ...p.calls];
    }
    if (p.from != null) {
      for (let i = p.from; i < Math.min(n, p.to); i++) {
        const f = out[i];
        if (p.keys) f.keys = [...(f.keys || []), ...p.keys];
        if (p.cam != null) f.cam = p.cam;
        if (p.aim) f.aim = p.aim;
        if (p.right) f.right = true;
        if (p.ui) f.ui = true;
      }
    }
  }
  return out;
}

const every = (from, to, step, make) => {
  const out = [];
  for (let i = from; i < to; i += step) out.push(make(i));
  return out;
};

const FOCUSED = ['mount', 'enemies', 'projectiles', 'camp', 'event', 'stars', 'turbulent', 'markets', 'lootbags', 'interact', 'discovery'];

// `places`: LOC da demo; `layout`: gameplay-layout.json; `bridges`, `riverZ(x)`: relevo.
export function buildScenarios({ places, layout, bridges, riverZ }) {
  const P = places;
  const shelter = layout.shelter;
  const pass = { x: P.passagem.x, z: P.passagem.z };
  const T = layout.turbulent.center;
  const profile = (start, extra = {}) => ({ name: 'Teste', origin: 'humano', start, model: 'paladina', ...extra });
  const list = [];

  // ---------------------------------------------------------------- integração: o boot inteiro
  list.push({
    name: 'boot', boot: true, profile: profile('espadachim', { name: 'Maren' }),
    frames: frames(540, [
      { from: 60, to: 240, keys: ['KeyW'], cam: 0 },
      { from: 240, to: 330, keys: ['KeyW', 'ShiftLeft'], cam: 0.5 },
      { at: 330, press: ['Space'] },
      { from: 360, to: 361, keys: ['ShiftLeft'] }, { at: 360, press: ['Space'] },
      { from: 380, to: 440, keys: ['KeyS', 'KeyD'], cam: -0.8 },
      { at: 450, press: ['KeyC'] },
      { from: 460, to: 540, keys: ['KeyW'], cam: Math.PI },
    ]),
  });

  // ---------------------------------------------------------------- motor
  list.push({
    name: 'motor', seed: 101, profile: profile('espadachim'), systems: FOCUSED,
    player: { x: shelter.x, z: shelter.z - 20 },
    frames: frames(430, [
      { from: 0, to: 60, keys: ['KeyW'], cam: 0.3 },
      { from: 60, to: 120, keys: ['KeyW', 'ShiftLeft'], cam: 0.3 },
      { from: 120, to: 160, keys: ['KeyW', 'ControlLeft'], cam: -0.4 },
      { from: 160, to: 200, keys: ['KeyA'], right: true, cam: 1.1 },
      { at: 205, press: ['Space'] },
      { from: 230, to: 250, keys: ['KeyW', 'ShiftLeft'], cam: 0 }, { at: 240, press: ['Space'] },
      { from: 260, to: 300, keys: ['KeyD'], cam: 2 }, { at: 270, press: ['KeyC'] },
      { at: 320, press: ['KeyC'] },
      { from: 340, to: 400, keys: ['KeyS', 'KeyA'], cam: -2.5 },
      { at: 405, press: ['Digit1'] },
      { from: 410, to: 430, keys: ['KeyW'], cam: 3.0 },
    ]),
  });

  // rio: atravessa a lâmina d'água longe das pontes, depois a ponte principal no sentido do tabuleiro
  const rx = P.canteiro.x - 90;
  const bridge = bridges.reduce((a, b) => (Math.hypot(b.x - P.canteiro.x, b.z - P.canteiro.z) < Math.hypot(a.x - P.canteiro.x, a.z - P.canteiro.z) ? b : a));
  const len = bridge.hd + bridge.apron;
  const ax = -Math.sin(bridge.rot), az = Math.cos(bridge.rot);
  list.push({
    name: 'rio', seed: 102, profile: profile('espadachim'), systems: FOCUSED,
    player: { x: rx, z: riverZ(rx) - 22 },
    frames: frames(560, [
      { from: 0, to: 300, keys: ['KeyW'], cam: Math.PI },
      { at: 310, calls: [['tp', bridge.x - ax * len * 0.95, bridge.z - az * len * 0.95]] },
      { from: 311, to: 560, keys: ['KeyW'], cam: Math.PI - bridge.rot },
    ]),
  });

  // montaria: montado, desmonta com F, monta de novo com R
  list.push({
    name: 'montaria', seed: 103, profile: profile('espadachim'), systems: FOCUSED,
    player: { x: pass.x, z: pass.z + 40, equip: [['mount', 'montaria']], mounted: true },
    frames: frames(420, [
      { from: 0, to: 120, keys: ['KeyW'], cam: 0.2 },
      { from: 120, to: 180, keys: ['KeyW', 'ShiftLeft'], cam: -0.6 },
      { at: 185, press: ['KeyQ'] },
      { at: 200, press: ['KeyF'] },
      { at: 260, press: ['KeyR'] },
      { from: 320, to: 420, keys: ['KeyD'], cam: 0 },
    ]),
  });

  // carga: no limite, normal com bolsa, cota de malha
  list.push({
    name: 'carga', seed: 104, profile: profile('espadachim'), systems: FOCUSED,
    player: { x: pass.x + 10, z: pass.z, inv: [['minerio', 20]] },
    frames: frames(360, [
      { from: 0, to: 100, keys: ['KeyW'], cam: 1 },
      { at: 40, press: ['Space'] }, { at: 60, press: ['KeyC'] },
      { at: 100, calls: [['equip', 'bag', 'bolsa']] },
      { from: 100, to: 200, keys: ['KeyW'], cam: -1 }, { at: 150, press: ['Space'] },
      { at: 200, calls: [['equip', 'armor', 'cota'], ['give', 'minerio', 3]] },
      { from: 200, to: 360, keys: ['KeyW', 'ShiftLeft'], cam: 2 }, { at: 260, press: ['KeyC'] },
    ]),
  });

  // ---------------------------------------------------------------- combate
  const packAt = (x, z) => [
    { type: 'garra', x, z: z - 9 },
    { type: 'saqueador', x: x + 3, z: z - 10 },
    { type: 'garraPalida', x: x - 4, z: z - 12 },
  ];
  list.push({
    name: 'espada', seed: 201, profile: profile('espadachim'), systems: FOCUSED,
    player: { x: pass.x, z: pass.z }, enemies: packAt(pass.x, pass.z),
    frames: frames(450, [
      { at: 2, calls: [['clickAttack', 0]] },
      { from: 80, to: 100, aim: { enemy: 1 } }, { at: 90, press: ['KeyQ'] },
      { at: 150, press: ['KeyE'] },
      { at: 200, calls: [['clickAttack', 1]] },
      { at: 300, press: ['Digit1'] },
      { at: 330, press: ['KeyC'] },
      { at: 360, calls: [['clickAttack', 2]] },
    ]),
  });

  list.push({
    name: 'arco', seed: 202, profile: profile('arqueiro'), systems: FOCUSED,
    player: { x: pass.x, z: pass.z },
    enemies: [{ type: 'saqueadorArco', x: pass.x, z: pass.z - 15 }, { type: 'saqueador', x: pass.x + 6, z: pass.z - 20 }],
    frames: frames(450, [
      { from: 10, to: 300, right: true, aim: { enemy: 0 } },
      { from: 300, to: 440, right: true, aim: { enemy: 1 } },
      ...every(20, 440, 25, (i) => ({ at: i, fire: true })),
      { at: 120, press: ['KeyQ'] }, { at: 240, press: ['KeyE'] },
    ]),
  });

  // técnicas de todas as famílias, com recarga zerada entre uma arma e outra
  const skillTour = (weapons, n) => {
    const parts = [];
    const step = Math.floor(n / weapons.length);
    weapons.forEach((w, k) => {
      const t0 = k * step + 2;
      parts.push({ at: t0, calls: [['wield', w], ['refresh'], ['spawn', { type: 'garra', x: pass.x + 2, z: pass.z - 7 }]] });
      parts.push({ from: t0, to: t0 + step - 2, aim: { enemy: k } });
      parts.push({ at: t0 + 5, press: ['KeyQ'] });
      parts.push({ at: t0 + Math.floor(step / 2), calls: [['refresh']], press: ['KeyE'] });
      parts.push({ from: t0 + step - 20, to: t0 + step - 8, right: true });
      parts.push({ at: t0 + step - 12, fire: true });
    });
    return frames(n, parts);
  };
  list.push({
    name: 'magias-a', seed: 203, profile: profile('mago_gelo'), systems: FOCUSED,
    player: { x: pass.x, z: pass.z }, enemies: [],
    frames: skillTour(['cajado_gelo', 'tomo_fogo', 'cajado_natureza', 'tomo_vento'], 480),
  });
  list.push({
    name: 'magias-b', seed: 204, profile: profile('metamorfo'), systems: FOCUSED,
    player: { x: pass.x, z: pass.z }, enemies: [],
    frames: skillTour(['tomo_maldicao', 'fetiche_metamorfose', 'cajado_vital', 'tomo_sagrado'], 480),
  });
  list.push({
    name: 'armas-a', seed: 205, profile: profile('lutador'), systems: FOCUSED,
    player: { x: pass.x, z: pass.z }, enemies: [],
    frames: skillTour(['martelo', 'manoplas', 'maca', 'machado'], 480),
  });
  list.push({
    name: 'armas-b', seed: 206, profile: profile('lanceiro'), systems: FOCUSED,
    player: { x: pass.x, z: pass.z }, enemies: [],
    frames: skillTour(['adaga', 'lanca', 'foice', 'besta'], 480),
  });

  // ---------------------------------------------------------------- derrotas
  list.push({
    name: 'derrota-fronteira', seed: 301, profile: profile('espadachim'), systems: FOCUSED,
    player: { x: pass.x, z: pass.z + 30, hp: 25, inv: [['pele', 3], ['madeira', 2]] },
    enemies: [{ type: 'saqueador', x: pass.x + 3, z: pass.z + 26 }, { type: 'saqueador', x: pass.x - 3, z: pass.z + 25 }],
    frames: frames(1100, [
      { at: 600, calls: [['respawn']] },
      { at: 1000, calls: [['kill', 0], ['kill', 1]] },
    ]),
  });
  const cg = layout.canteiroGuard;
  list.push({
    name: 'resgate', seed: 302, profile: profile('espadachim'), systems: FOCUSED,
    player: { x: cg.x + 4, z: cg.z + 4, hp: 12 },
    enemies: [{ type: 'guarda', x: cg.x, z: cg.z, post: { x: cg.x, z: cg.z, yaw: cg.yaw } }, { type: 'garra', x: cg.x + 7, z: cg.z + 6 }],
    frames: frames(420, []),
  });
  const E = P.ermos;
  list.push({
    name: 'fullloot', seed: 303, profile: profile('espadachim'), systems: FOCUSED,
    player: {
      x: E.x, z: E.z, hp: 15,
      inv: [['pele', 4], ['cristal', 2], ['arco', 1, 64], ['pocao', 1]],
      equip: [['armor', 'gibao'], ['bag', 'bolsa']],
    },
    enemies: [{ type: 'garraPalida', x: E.x + 2, z: E.z + 2 }, { type: 'saqueador', x: E.x - 2, z: E.z + 3 }],
    frames: frames(420, []),
  });

  // ---------------------------------------------------------------- economia
  list.push({
    name: 'economia', seed: 401, profile: profile('espadachim'), systems: FOCUSED,
    player: { x: P.mercado.x, z: P.mercado.z },
    frames: frames(320, [
      { at: 1, calls: [['give', 'minerio', 10], ['give', 'madeira', 6], ['give', 'pele', 5], ['give', 'erva', 6], ['give', 'cristal', 2]] },
      { at: 5, calls: [['sell', 'vale', 'minerio', 3], ['sell', 'vale', 'pele', 5]] },
      { at: 10, calls: [['buy', 'vale', 'pocao', 2], ['buy', 'vale', 'gibao', 1]] },
      { at: 20, calls: [['coins', 200], ['learn', 'artesao'], ['learn', 'alquimista']] },
      { at: 30, calls: [['craft', 0, true], ['craft', 0, true], ['craft', 0, true], ['craft', 1, true], ['craft', 5, true]] },
      { at: 40, calls: [['craft', 6, true], ['craft', 4, true]] },
      { at: 50, calls: [['repair', 'weapon'], ['wield', 'espada', 30], ['repair', 'weapon']] },
      { at: 60, calls: [['give', 'minerio', 4], ['craft', 0, true], ['wield', 'espada', 30], ['repair', 'weapon']] },
      { at: 70, calls: [['kit'], ['buyMount']] },
      { at: 80, calls: [['deliver', 'ferramentas'], ['give', 'lingote', 2], ['deliver', 'lingote']] },
      { at: 90, calls: [['sell', 'alto', 'cristal', 1], ['sell', 'vale', 'instrumento', 1]] },
      { at: 200, calls: [['sell', 'vale', 'madeira', 2]] },
    ]),
  });

  // ---------------------------------------------------------------- acampamento
  const C = layout.camp.center;
  list.push({
    name: 'acampamento', seed: 402, profile: profile('espadachim'), systems: FOCUSED, camp: true,
    player: { x: C.x + 30, z: C.z + 30 },
    frames: frames(700, [
      { at: 20, calls: [['killCamp', 0]] }, { at: 40, calls: [['killCamp', 1]] }, { at: 60, calls: [['killCamp', 2]] },
      { at: 80, calls: [['refresh'], ['tp', C.x + 200, C.z + 200]] },
      { at: 300, calls: [['killCamp', 0], ['killCamp', 1], ['killCamp', 2], ['killCamp', 3]] },
      { at: 400, calls: [['time', 111]] },
      { at: 500, calls: [['time', 30]] }, { at: 600, calls: [['time', 30]] },
    ]),
  });

  // ---------------------------------------------------------------- Turbulenta
  const sh = layout.turbulent.shrines[0], ex = layout.turbulent.exits[0];
  list.push({
    name: 'turbulenta', seed: 403, profile: profile('espadachim'), systems: FOCUSED,
    player: { x: pass.x, z: pass.z + 20, inv: [['fragmento', 1]] },
    frames: frames(460, [
      { at: 5, calls: [['portal']] },
      { at: 10, calls: [['enter']] },
      { at: 40, calls: [['refresh'], ['tp', T.x + sh[0] + 1.5, T.z + sh[1] + 1.5]] },
      { at: 45, press: ['KeyF'] },
      { at: 150, calls: [['refresh'], ['tp', T.x + ex[0] + 1.5, T.z + ex[1] + 1.5]] },
      { at: 155, press: ['KeyF'] },
    ]),
  });

  // ---------------------------------------------------------------- céu
  list.push({
    name: 'estrelas', seed: 404, profile: profile('espadachim'), systems: FOCUSED,
    player: { x: P.observatorio.x, z: P.observatorio.z + 20 },
    frames: frames(240, [
      { at: 1, calls: [['time', 49]] },
      { at: 60, calls: [['observe']] }, { at: 70, calls: [['endObserve']] },
      { at: 80, calls: [['time', 300]] },
      { at: 120, calls: [['observe']] }, { at: 130, calls: [['endObserve']] },
      { at: 150, calls: [['time', 130]] },
      { at: 200, calls: [['observe']] },
    ]),
  });

  // ---------------------------------------------------------------- coleta
  const node = layout.nodes[0];
  list.push({
    name: 'coleta', seed: 405, profile: profile('artesao'), systems: FOCUSED,
    player: { x: node.x + 1.2, z: node.z },
    frames: frames(260, [
      { at: 5, press: ['KeyF'] }, { at: 60, press: ['KeyF'] }, { at: 110, press: ['KeyF'] }, { at: 160, press: ['KeyF'] },
      { at: 210, calls: [['time', 56]] }, { at: 215, press: ['KeyF'] },
    ]),
  });

  return list;
}
