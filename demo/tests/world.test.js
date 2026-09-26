// Testes dos contratos críticos da integração do mundo.
// Rodam pelo mesmo caminho do build (esbuild com os loaders de .bin/.json/.glb):
//   node tools/run-world-tests.mjs
import { WORLD, REGION, TURBULENT, anchor, route, placement, assertRuntimeSchema, PLACEMENTS } from '../src/world/runtime-manifest.js';
import {
  worldPlanToThree, threeToWorldPlan, regionAnchorToThree, turbulentAnchorToThree,
  routeToThree, PLAN_Z_SIGN, TURB_RUNTIME_OFFSET, TURB_GATE_X, isTurbulentSpace,
} from '../src/world/coordinates.js';
import {
  REGION_FIELD, TURB_FIELD, fieldAt, regionHeightAt, turbulentHeightAt,
  biomeAt, isWaterCell, waterLevelAt, WATER_LEVEL, REGION_LIMITS, TURB_LIMITS,
  RIVER, riverCenterAt, riverHalfWidthAt, wetnessAt,
} from '../src/world/heightfield.js';
import { initRouteField, routeDistance, REGION_ROUTE_LINES, distToPolyline, nearestRouteInfo } from '../src/world/world-routes.js';
import { HALF, TURB, LOC, MAIN_ROAD, EAST_ROAD, WEST_PATH, RAVINE_PATH, PORTAL_SPOTS, zoneAt, placeAt, gorgeX, riverZ, WORLD_LAYOUT_VERSION } from '../src/game/layout.js';
import { buildWorldColliders, WORLD_BRIDGES, WORLD_COLLIDERS } from '../src/world/world-colliders.js';
import { migrateSave, isValidPosition, safeSpawn, SAVE_VERSION } from '../src/game/save.js';
import { groundHeight, terrainHeight, WATER_Y, bridges, initTerrainData } from '../src/engine/terrain.js';
import { moveEntity, addCircle } from '../src/game/collide.js';
import { KIT_MANIFEST } from '../src/world/nature-kit.js';
import kitBin from '../assets/nature-kit/nature-kit.glb';
import { planRegionInstances, TARGET_RANGES, buildRegionInstances, setSecondaryDensity } from '../src/world/world-instances.js';
import { FUNCTIONAL, solidGapAt } from '../src/world/functional-areas.js';
import { generateCell, CELL } from '../src/world/ground-cover.js';
import { LOD_FIELDS, updateLODFields, activeCount, setLODBands, LOD_BANDS } from '../src/engine/lod-field.js';
import { ENCOUNTERS, CANTEIRO, PASSAGE_PROPS, GUARD_POSTS } from '../src/game/layout.js';
import { SHELTER } from '../src/game/zones.js';
import { EXTRA_SPOTS, EXTRA_DEFS } from '../src/world/extra-spots.js';
import { REPLACEMENTS, replacedVolume } from '../src/world/extra-props.js';
import EXTRA_MANIFEST from '../assets/extra-props/extra-props-manifest.json';


const results = [];
function test(name, fn) {
  try { fn(); results.push({ name, ok: true }); } catch (e) { results.push({ name, ok: false, error: e.message }); }
}
function assert(cond, msg) { if (!cond) throw new Error(msg || 'falhou'); }
function close(a, b, tol, msg) {
  if (Math.abs(a - b) > tol) throw new Error(`${msg || 'valor'}: ${a} ≠ ${b} (tolerância ${tol})`);
}

// ---------------------------------------------------------------- manifesto
test('manifesto: schema e limites', () => {
  assertRuntimeSchema();
  assert(WORLD.runtime_schema.startsWith('1.'), 'runtime_schema inesperado');
  assert(WORLD_LAYOUT_VERSION === WORLD.world_layout_version, 'layout divergente');
  assert(JSON.stringify(REGION.bounds_m) === JSON.stringify([-512, -512, 512, 512]), 'limites da região');
  assert(JSON.stringify(TURBULENT.bounds_m) === JSON.stringify([-128, -128, 128, 128]), 'limites da Turbulenta');
  assert(REGION.unit_meters === 1 && TURBULENT.unit_meters === 1, 'escala métrica');
  assert(REGION.chunks.length === 16, '16 blocos');
});

test('manifesto: âncoras obrigatórias presentes', () => {
  for (const id of ['bridge_main', 'bridge_minor', 'camp', 'cave_west', 'gorge_center', 'market',
    'mine', 'observatory', 'outpost_north', 'outpost_south', 'portal_turbulent', 'wastes']) {
    const a = anchor(id);
    assert(Array.isArray(a) && a.length === 3, `âncora ${id} incompleta`);
  }
  for (const id of ['entry', 'portal_core', 'objective_ruins', 'exit_east', 'exit_west']) {
    const a = anchor(id, 'turbulent');
    assert(Array.isArray(a) && a.length === 3, `âncora turbulenta ${id} incompleta`);
  }
});

test('manifesto: quatro rotas da região com geometria', () => {
  for (const id of ['short_gorge', 'safe_east', 'mine_spur', 'observatory_trail']) {
    const r = route(id);
    assert(r.points.length > 8, `rota ${id} com poucos pontos`);
    assert(r.width_m > 0, `rota ${id} sem largura`);
  }
  close(route('short_gorge').width_m, 4, 0.001, 'largura short_gorge');
  close(route('safe_east').width_m, 5, 0.001, 'largura safe_east');
  close(route('mine_spur').width_m, 2.5, 0.001, 'largura mine_spur');
  close(route('observatory_trail').width_m, 2.5, 0.001, 'largura observatory_trail');
});

// ---------------------------------------------------------------- coordenadas
test('coordenadas: plano ↔ cena são inversas', () => {
  assert(PLAN_Z_SIGN === -1, 'o exportador inverte o eixo horizontal');
  for (const [x, y] of [[0, 0], [123.5, -456.25], [-512, 512], [40, -375]]) {
    const t = worldPlanToThree(x, y);
    const back = threeToWorldPlan(t.x, t.z);
    close(back.x, x, 1e-9, 'x de volta');
    close(back.y, y, 1e-9, 'y de volta');
  }
});

test('coordenadas: âncoras conhecidas batem com a geometria exportada', () => {
  // Bridge_Main está em z = +145 no glTF e a âncora do plano é (0, -145)
  const bm = regionAnchorToThree(anchor('bridge_main'));
  close(bm.x, 0, 1e-6, 'x da ponte principal');
  close(bm.z, 145, 1e-6, 'z da ponte principal');
  const obs = regionAnchorToThree(anchor('observatory'));
  close(obs.x, -260, 1e-6, 'x do observatório');
  close(obs.z, -205, 1e-6, 'z do observatório');
  const mk = regionAnchorToThree(anchor('market'));
  close(mk.x, 40, 1e-6, 'x do mercado');
  close(mk.z, 375, 1e-6, 'z do mercado');
  // o volume medido no GLB tem que cair sobre a âncora
  const p = placement('Bridge_Main');
  close(p.plan[0], 0, 0.5, 'plano x da ponte');
  close(p.plan[1], -145, 0.5, 'plano y da ponte');
});

test('coordenadas: deslocamento técnico da Turbulenta', () => {
  assert(TURB_RUNTIME_OFFSET.x >= 1000, 'deslocamento insuficiente');
  assert(TURB_RUNTIME_OFFSET.x - 128 > 512 + 200, 'Turbulenta perto demais da região');
  const entry = turbulentAnchorToThree(anchor('entry', 'turbulent'));
  close(entry.x, TURB_RUNTIME_OFFSET.x, 1e-6, 'x da entrada');
  close(entry.z, 112, 1e-6, 'z da entrada');
  assert(isTurbulentSpace(entry.x), 'entrada deveria estar no espaço da Turbulenta');
  assert(!isTurbulentSpace(511), 'borda leste da região não é Turbulenta');
  assert(TURB.half === 128, 'meia-extensão da Turbulenta');
  // toda a área deslocada fica além do portão técnico
  assert(TURB_RUNTIME_OFFSET.x - TURB.half > TURB_GATE_X, 'a área deslocada cruza o portão');
});

test('coordenadas: rotas convertidas mantêm o sinal', () => {
  const pts = routeToThree(route('safe_east').points);
  const raw = route('safe_east').points;
  close(pts[0][0], raw[0][0], 1e-9, 'x do primeiro ponto');
  close(pts[0][1], -raw[0][1], 1e-9, 'z do primeiro ponto');
});

// ---------------------------------------------------------------- heightfield
test('heightfield: dimensões e fórmula declaradas', () => {
  assert(REGION_FIELD.w === 1025 && REGION_FIELD.h === 1025, 'amostras da região');
  assert(TURB_FIELD.w === 513 && TURB_FIELD.h === 513, 'amostras da Turbulenta');
  close(REGION_FIELD.step, 1, 1e-9, 'passo da região');
  close(TURB_FIELD.step, 0.5, 1e-9, 'passo da Turbulenta');
  assert(REGION.height.encoding === 'linear_uint16_le', 'codificação declarada');
});

test('heightfield: nós da grade batem com a amostragem bilinear', () => {
  for (const [i, j] of [[0, 0], [512, 512], [1024, 1024], [40, 900], [777, 13]]) {
    const x = -512 + i, y = -512 + j;
    const exact = fieldAt(REGION_FIELD, i, j);
    const sampled = regionHeightAt(x, PLAN_Z_SIGN * y);
    close(sampled, exact, 1e-4, `nó (${i},${j})`);
  }
});

test('heightfield: interpolação bilinear entre dois nós', () => {
  const j = 400;
  for (const i of [100, 333, 901]) {
    const a = fieldAt(REGION_FIELD, i, j), b = fieldAt(REGION_FIELD, i + 1, j);
    const mid = regionHeightAt(-512 + i + 0.5, PLAN_Z_SIGN * (-512 + j));
    assert(mid >= Math.min(a, b) - 1e-4 && mid <= Math.max(a, b) + 1e-4, `meio de (${i},${j}) fora do intervalo`);
  }
});

test('heightfield: bordas e fora dos limites seguram o valor da borda', () => {
  const edge = regionHeightAt(-512, 512);            // canto do plano (x=-512, y=-512)
  close(regionHeightAt(-600, 900), edge, 1e-6, 'fora dos limites a oeste/sul');
  const e2 = regionHeightAt(512, -512);
  close(regionHeightAt(4000, -4000), e2, 1e-6, 'fora dos limites a leste/norte');
  assert(Number.isFinite(regionHeightAt(1e9, -1e9)), 'valor finito bem longe');
});

test('heightfield: sem saltos nas bordas dos 16 blocos', () => {
  // as emendas ficam em múltiplos de 256 m; a altura tem de ser contínua
  let worst = 0;
  for (const edge of [-256, 0, 256]) {
    for (let v = -500; v <= 500; v += 4) {
      worst = Math.max(worst, Math.abs(regionHeightAt(edge - 0.01, v) - regionHeightAt(edge + 0.01, v)));
      worst = Math.max(worst, Math.abs(regionHeightAt(v, edge - 0.01) - regionHeightAt(v, edge + 0.01)));
    }
  }
  assert(worst < 0.05, `salto de ${worst.toFixed(3)} m em uma borda de bloco`);
});

test('heightfield: alturas dentro da faixa declarada', () => {
  let lo = Infinity, hi = -Infinity;
  for (let z = -512; z <= 512; z += 8) for (let x = -512; x <= 512; x += 8) {
    const h = regionHeightAt(x, z);
    if (h < lo) lo = h; if (h > hi) hi = h;
  }
  assert(lo >= REGION.height.min - 1e-3, `mínimo ${lo} abaixo do declarado`);
  assert(hi <= REGION.height.max + 1e-3, `máximo ${hi} acima do declarado`);
});

test('heightfield: Turbulenta amostra no espaço deslocado', () => {
  const core = turbulentAnchorToThree(anchor('portal_core', 'turbulent'));
  const h = turbulentHeightAt(core.x, core.z);
  close(h, anchor('portal_core', 'turbulent')[2], 0.3, 'altura do portal central');
  assert(TURB_LIMITS.x0 > REGION_LIMITS.x1, 'as duas áreas se sobrepõem');
});

test('heightfield: máscara de água e biomas', () => {
  const values = new Set();
  let water = 0;
  for (let z = -512; z <= 512; z += 2) for (let x = -512; x <= 512; x += 2) {
    values.add(biomeAt(x, z));
    if (isWaterCell(x, z)) water++;
  }
  assert(water > 500, `poucas células de água (${water})`);
  for (const v of values) assert(v >= 0 && v <= 5, `bioma fora da faixa: ${v}`);
  assert(values.has(5), 'bioma de água ausente');
});

test('heightfield: perfil do rio cobre a região e é razoável', () => {
  assert(Number.isFinite(WATER_Y) && WATER_Y > 5 && WATER_Y < 30, `WATER_Y fora do esperado: ${WATER_Y}`);
  for (const x of [-512, -256, 0, 256, 512]) {
    const w = waterLevelAt(x);
    assert(Number.isFinite(w), `nível indefinido em x=${x}`);
    assert(w > REGION.height.min && w < REGION.height.max, `nível fora da faixa em x=${x}`);
  }
});

// ---------------------------------------------------------------- rotas e layout
test('rotas: campo de distância concorda com a polilinha', () => {
  initRouteField();
  const pts = REGION_ROUTE_LINES.safe_east.pts;
  for (const t of [0.15, 0.4, 0.75]) {
    const p = pts[Math.round(t * (pts.length - 1))];
    assert(routeDistance(p[0], p[1]) < 2.5, `ponto da rota com distância ${routeDistance(p[0], p[1])}`);
  }
  // longe de tudo o campo satura
  assert(routeDistance(-500, 500) >= 25, 'campo não satura longe das rotas');
});

// ---------------------------------------------------------------- rio refeito no vale
test('rio: o curso serpenteia dentro do vale e a largura varia', () => {
  const st = RIVER.stats;
  assert(st.bends >= 6, `poucas curvas: ${st.bends}`);
  assert(st.maxOffset >= 5 && st.maxOffset <= 14, `desvio máximo do eixo antigo fora de 5–14 m: ${st.maxOffset}`);
  assert(st.minHalf <= 4.5 && st.maxHalf >= 8, `largura sem variação: meia-largura de ${st.minHalf} a ${st.maxHalf} m`);
  // o eixo não salta: curvas suaves
  let worst = 0;
  for (let x = -511; x < 511; x++) worst = Math.max(worst, Math.abs(riverCenterAt(x + 1) - riverCenterAt(x)));
  assert(worst < 0.8, `eixo com degrau de ${worst.toFixed(2)} m por metro`);
});

test('rio: leito abaixo da lâmina e margens acima dela', () => {
  initTerrainData();
  let sections = 0, lowBank = 0, dryAxis = 0, jump = 0;
  for (let x = -510; x <= 510; x += 2) {
    const L = waterLevelAt(x), c = riverCenterAt(x);
    if (regionHeightAt(x, c) > L - 0.4) dryAxis++;
    if (Math.abs(waterLevelAt(x + 1) - L) > 0.08) jump++;
    const zc = Math.round(c);
    if (!isWaterCell(x, zc)) continue;
    const nearBridge = WORLD_BRIDGES.some((b) => Math.abs(x - b.x) < b.hw + 6);
    for (const dir of [-1, 1]) {
      let z = zc;
      while (isWaterCell(x, z) && Math.abs(z - zc) < 30) z += dir;
      sections++;
      if (!nearBridge && regionHeightAt(x, z + dir * 1.5) < L + 0.1) lowBank++;
    }
  }
  assert(dryAxis <= 6, `${dryAxis} colunas com o eixo raso demais (leito > lâmina − 0,4 m)`);
  assert(jump === 0, `nível do rio com saltos em ${jump} colunas`);
  assert(lowBank / sections <= 0.03, `margem baixa demais em ${lowBank} de ${sections} lados (${(lowBank / sections * 100).toFixed(1)} %)`);
});

test('rio: a água não chega às construções nem às estradas fora das pontes', () => {
  initTerrainData();
  for (const p of PLACEMENTS) {
    if (p.bridge || p.kind === 'field') continue;
    const px = p.plan[0], pz = PLAN_Z_SIGN * p.plan[1];
    for (let z = pz - p.half[1] - 2; z <= pz + p.half[1] + 2; z += 1) for (let x = px - p.half[0] - 2; x <= px + p.half[0] + 2; x += 1) {
      assert(!isWaterCell(x, z), `água a menos de 2 m de ${p.name} em (${x.toFixed(0)}, ${z.toFixed(0)})`);
    }
  }
  const onBridge = (x, z) => WORLD_BRIDGES.some((b) => Math.abs(x - b.x) <= b.hw + 4 && Math.abs(z - b.z) <= b.hd + 3);
  for (const line of Object.values(REGION_ROUTE_LINES)) {
    for (let i = 0; i < line.pts.length - 1; i++) {
      const [ax, az] = line.pts[i], [bx, bz] = line.pts[i + 1];
      const n = Math.ceil(Math.hypot(bx - ax, bz - az));
      for (let k = 0; k <= n; k++) {
        const x = ax + (bx - ax) * k / n, z = az + (bz - az) * k / n;
        if (onBridge(x, z)) continue;
        assert(!isWaterCell(x, z), `${line.id} passa pela água fora da ponte em (${x.toFixed(1)}, ${z.toFixed(1)})`);
      }
    }
  }
});

test('rio: umidade só junto da água', () => {
  assert(wetnessAt(100, riverCenterAt(100)) > 0.9, 'o leito deveria estar encharcado');
  assert(wetnessAt(LOC.alto.x, LOC.alto.z) === 0, 'umidade longe do rio');
});

test('layout: âncoras principais viraram lugares', () => {
  close(HALF, 512, 1e-9, 'meia-extensão');
  const pairs = [
    ['vale', 'outpost_south'], ['alto', 'outpost_north'], ['mercado', 'market'],
    ['acampamento', 'camp'], ['observatorio', 'observatory'], ['mina', 'mine'],
    ['caverna', 'cave_west'], ['portal', 'portal_turbulent'], ['ponteMenor', 'bridge_minor'],
  ];
  for (const [key, id] of pairs) {
    const a = regionAnchorToThree(anchor(id));
    close(LOC[key].x, a.x, 1e-6, `${key}.x`);
    close(LOC[key].z, a.z, 1e-6, `${key}.z`);
  }
  assert(LOC.ermos.rx > 100 && LOC.ermos.rz > 100, 'Ermos pequenos demais para a nova escala');
});

test('layout: rotas antigas apontam para as novas', () => {
  assert(MAIN_ROAD.length > 8 && EAST_ROAD.length > 8, 'rotas principais vazias');
  assert(WEST_PATH.length > 4 && RAVINE_PATH.length > 4, 'trilhas vazias');
  // a rota curta liga os dois entrepostos
  const first = MAIN_ROAD[0], last = MAIN_ROAD[MAIN_ROAD.length - 1];
  const south = LOC.vale, north = LOC.alto;
  assert(Math.hypot(first[0] - south.x, first[1] - south.z) < 30, 'rota curta não começa no entreposto sul');
  assert(Math.hypot(last[0] - north.x, last[1] - north.z) < 30, 'rota curta não termina no entreposto norte');
});

test('layout: gorgeX e riverZ continuam respondendo', () => {
  const x = gorgeX(0);
  assert(Number.isFinite(x) && Math.abs(x) <= 512, `gorgeX fora do mundo: ${x}`);
  for (const px of [-512, 0, 275, 512]) {
    const z = riverZ(px);
    assert(Number.isFinite(z) && Math.abs(z) <= 512, `riverZ fora do mundo em ${px}`);
  }
  // nas travessias do manifesto o eixo do rio passa pelo vão da ponte
  for (const p of PLACEMENTS.filter((q) => q.bridge)) {
    const bx = p.plan[0], bz = PLAN_Z_SIGN * p.plan[1];
    const room = p.half[1] - riverHalfWidthAt(bx);
    assert(Math.abs(riverZ(bx) - bz) <= room, `${p.name}: eixo do rio a ${Math.abs(riverZ(bx) - bz).toFixed(2)} m do centro, fora do vão`);
  }
});

test('layout: zonas e lugares cobrem o mundo', () => {
  const seen = new Set();
  for (let z = -500; z <= 500; z += 20) for (let x = -500; x <= 500; x += 20) seen.add(zoneAt(x, z));
  assert(seen.has('protegida') && seen.has('fronteira') && seen.has('fullloot'), `zonas encontradas: ${[...seen]}`);
  assert(zoneAt(TURB.x, TURB.z) === 'turbulenta', 'a área deslocada deveria ser turbulenta');
  assert(placeAt(LOC.mercado.x, LOC.mercado.z) === LOC.mercado.name, 'mercado não reconhecido');
  assert(placeAt(TURB.x, TURB.z) === 'Região Turbulenta', 'lugar da Turbulenta');
});

test('layout: pontos de portal ficam fora das zonas protegidas', () => {
  assert(PORTAL_SPOTS.length >= 4, `poucos pontos de portal: ${PORTAL_SPOTS.length}`);
  for (const [x, z] of PORTAL_SPOTS) {
    const zone = zoneAt(x, z);
    assert(zone === 'fronteira' || zone === 'fullloot', `ponto de portal em zona ${zone}`);
    assert(Math.abs(x) < HALF - 10 && Math.abs(z) < HALF - 10, 'ponto de portal fora do mundo');
  }
});

// ---------------------------------------------------------------- terreno e colisores
test('terreno: pontes registram piso elevado acima do leito', () => {
  initTerrainData();
  assert(bridges.length === 2, `pontes registradas: ${bridges.length}`);
  for (const b of bridges) {
    const below = terrainHeight(b.x, b.z);
    assert(b.y > below, `tabuleiro de ${b.name} abaixo do leito`);
    close(groundHeight(b.x, b.z), b.y, 1e-6, `groundHeight sobre ${b.name}`);
    // fora da ponte o chão volta a ser o terreno
    const off = groundHeight(b.x + b.hw + 6, b.z);
    close(off, terrainHeight(b.x + b.hw + 6, b.z), 1e-6, 'fora da ponte');
  }
});

test('terreno: as duas pontes cruzam o rio', () => {
  for (const b of bridges) {
    let crossed = false;
    for (let d = -b.hd; d <= b.hd; d += 1) if (isWaterCell(b.x, b.z + d)) crossed = true;
    assert(crossed, `${b.name} não passa sobre o rio`);
  }
});

test('colisores: gerados e fora das estradas', () => {
  const built = buildWorldColliders();
  assert(built.colliders.length > 15, `poucos colisores: ${built.colliders.length}`);
  assert(built.bridges.length === 2, 'pontes');
  for (const c of built.colliders) {
    if (c.x > TURB_GATE_X) continue;
    // barreiras de corredor são propositalmente paralelas à estrada: nelas vale a espessura
    const reach = c.r != null ? c.r : (c.rot ? c.hd : Math.max(c.hw, c.hd));
    assert(routeDistance(c.x, c.z) >= reach - 0.01, `colisor ${c.name} invade uma estrada`);
  }
});

test('colisores: nada bloqueia o eixo das rotas principais', () => {
  for (const id of ['short_gorge', 'safe_east', 'mine_spur', 'observatory_trail']) {
    const line = REGION_ROUTE_LINES[id];
    for (const [x, z] of line.pts) {
      for (const c of WORLD_COLLIDERS) {
        if (c.x > TURB_GATE_X) continue;
        if (c.r != null) {
          assert(Math.hypot(c.x - x, c.z - z) > c.r, `${c.name} bloqueia ${id}`);
        } else {
          const co = Math.cos(c.rot || 0), si = Math.sin(c.rot || 0);
          const ex = x - c.x, ez = z - c.z;
          const lx = ex * co + ez * si, lz = -ex * si + ez * co;
          assert(Math.abs(lx) > c.hw || Math.abs(lz) > c.hd, `${c.name} bloqueia ${id}`);
        }
      }
    }
  }
});

// ---------------------------------------------------------------- migração de saves
test('save: migra o layout anterior preservando o progresso', () => {
  const antigo = {
    v: 1, time: 812, stats: { incursoes: 2 }, flags: { disc_bosque: true },
    profile: { name: 'Maren', origin: 'humano', start: 'espadachim' },
    P: { coins: 173, trainings: ['espadachim'], inv: [{ id: 'pocao', n: 2, uid: 7 }], saddle: [], storage: [], equip: {}, stableBags: [] },
    book: { entries: [1, 2] }, evt: { progress: 40 }, camp: { pop: 6, state: 'pressionado' },
    sky: { vanished: [], observed: [], instrumentGiven: false },
    mkt: {}, marketBase: { alto: 19, vale: 14 },
  };
  const d = migrateSave(JSON.parse(JSON.stringify(antigo)));
  assert(d.v === SAVE_VERSION, 'versão do save não atualizada');
  assert(d.world.layout === WORLD_LAYOUT_VERSION, 'layout não migrado');
  assert(d.P.coins === 173, 'moedas perdidas');
  assert(d.P.inv.length === 1 && d.P.inv[0].uid === 7, 'inventário perdido');
  assert(d.camp.state === 'pressionado', 'estado do acampamento perdido');
  assert(d.flags.disc_bosque === true, 'marcos perdidos');
  assert(d.evt.progress === 40, 'progresso do evento perdido');
  assert(d.migrated.notes.length > 0, 'migração silenciosa');
  assert(isValidPosition(d.world.pos[0], d.world.pos[1]), 'posição de destino inválida');
});

test('save: posição fora do mundo novo cai na âncora segura', () => {
  const d = migrateSave({
    v: 2, world: { layout: WORLD_LAYOUT_VERSION, pos: [9999, -9999], yaw: 0 },
    time: 0, stats: {}, flags: {}, profile: {},
    P: { coins: 0, trainings: [], inv: [], saddle: [], storage: [], equip: {}, stableBags: [] },
    book: {}, evt: {}, camp: { pop: 0, state: 'estabelecido' },
    sky: { vanished: [], observed: [], instrumentGiven: false }, mkt: {}, marketBase: { alto: 0, vale: 0 },
  });
  const safe = safeSpawn();
  close(d.world.pos[0], safe.x, 1e-6, 'x de destino');
  close(d.world.pos[1], safe.z, 1e-6, 'z de destino');
  assert(d.world.reposicionado === true, 'não marcou o reposicionamento');
});

test('save: posição válida já migrada é preservada', () => {
  const safe = safeSpawn();
  const x = safe.x + 6, z = safe.z + 6;
  assert(isValidPosition(x, z), 'ponto de teste inválido');
  const d = migrateSave({
    v: 2, world: { layout: WORLD_LAYOUT_VERSION, pos: [x, z], yaw: 1.2 },
    time: 10, stats: {}, flags: {}, profile: {},
    P: { coins: 5, trainings: [], inv: [], saddle: [], storage: [], equip: {}, stableBags: [] },
    book: {}, evt: {}, camp: { pop: 0, state: 'estabelecido' },
    sky: { vanished: [], observed: [], instrumentGiven: false }, mkt: {}, marketBase: { alto: 0, vale: 0 },
  });
  close(d.world.pos[0], x, 1e-6, 'x preservado');
  close(d.world.pos[1], z, 1e-6, 'z preservado');
  assert(!d.world.reposicionado, 'reposicionou um save já migrado');
  assert(d.migrated.notes.length === 0, 'anotou migração desnecessária');
});

test('save: a âncora segura é caminhável', () => {
  const s = safeSpawn();
  assert(isValidPosition(s.x, s.z), 'âncora segura inválida');
  assert(zoneAt(s.x, s.z) === 'protegida', 'âncora segura fora de zona protegida');
});

test('save: posição dentro da Turbulenta é recusada', () => {
  assert(!isValidPosition(TURB.x, TURB.z), 'aceitou posição na Turbulenta');
});

// ---------------------------------------------------------------- navegação
test('navegação: os marcos principais são alcançáveis a pé', () => {
  // caminhada em linha reta pela rota, verificando que nenhum passo exige degrau impossível
  const MAX_STEP = 1.25;
  for (const id of ['short_gorge', 'safe_east', 'mine_spur', 'observatory_trail']) {
    const pts = REGION_ROUTE_LINES[id].pts;
    let worst = 0;
    for (let i = 0; i < pts.length - 1; i++) {
      const [ax, az] = pts[i], [bx, bz] = pts[i + 1];
      const len = Math.hypot(bx - ax, bz - az);
      const steps = Math.max(1, Math.ceil(len));
      let prev = groundHeight(ax, az);
      for (let s = 1; s <= steps; s++) {
        const t = s / steps;
        const h = groundHeight(ax + (bx - ax) * t, az + (bz - az) * t);
        worst = Math.max(worst, Math.abs(h - prev) / Math.max(0.05, len / steps));
        prev = h;
      }
    }
    assert(worst <= MAX_STEP, `rota ${id} exige inclinação ${worst.toFixed(2)} (limite ${MAX_STEP})`);
  }
});

test('navegação: marcos ficam dentro do mundo e sobre o chão', () => {
  for (const key of ['vale', 'alto', 'mercado', 'canteiro', 'passagem', 'acampamento',
    'observatorio', 'mina', 'caverna', 'portal', 'ermos', 'bosque']) {
    const l = LOC[key];
    assert(Math.abs(l.x) < HALF && Math.abs(l.z) < HALF, `${key} fora do mundo`);
    const h = groundHeight(l.x, l.z);
    assert(Number.isFinite(h), `${key} sem chão`);
  }
});

test('navegação: Turbulenta tem entrada, objetivo e duas saídas separados', () => {
  const p = (id) => turbulentAnchorToThree(anchor(id, 'turbulent'));
  const entry = p('entry'), core = p('portal_core'), obj = p('objective_ruins');
  const east = p('exit_east'), west = p('exit_west');
  assert(Math.hypot(east.x - west.x, east.z - west.z) > 120, 'as duas saídas estão perto demais');
  assert(Math.hypot(entry.x - core.x, entry.z - core.z) > 60, 'entrada colada no portal');
  for (const q of [entry, core, obj, east, west]) {
    const lx = q.x - TURB.x, lz = q.z - TURB.z;
    assert(Math.abs(lx) <= 128 && Math.abs(lz) <= 128, 'âncora fora da Turbulenta');
    assert(Number.isFinite(turbulentHeightAt(q.x, q.z)), 'sem chão na Turbulenta');
  }
});

// ---------------------------------------------------------------- kit de natureza
test('kit: manifesto com todos os tipos que o mundo distribui', () => {
  assert(KIT_MANIFEST.kit_schema.startsWith('1.'), `kit_schema inesperado: ${KIT_MANIFEST.kit_schema}`);
  assert(KIT_MANIFEST.source.unit_scale === 1, 'o FBX2glTF já normaliza a escala do pacote');
  for (const t of ['pine', 'broad', 'dead', 'bush', 'grass', 'flower', 'mushroom',
    'rock', 'cliff', 'pebble', 'log', 'stump', 'branch', 'mountain']) {
    const info = KIT_MANIFEST.types[t];
    assert(info, `tipo ausente no kit: ${t}`);
    assert(info.variants >= 1, `${t} sem variantes`);
    assert(info.items.length === info.variants, `${t}: contagem de variantes inconsistente`);
  }
});

test('kit: alturas na escala métrica do mundo', () => {
  const faixa = {
    pine: [11, 14], broad: [4, 6.5], dead: [11, 14], bush: [1, 2.2], grass: [0.3, 1],
    flower: [0.5, 1.2], mushroom: [0.2, 0.7], rock: [0.8, 5], cliff: [4, 14],
    pebble: [0.1, 0.8], log: [0.3, 1.2], stump: [0.5, 1.6], branch: [0.02, 0.6], mountain: [30, 60],
  };
  for (const [t, [lo, hi]] of Object.entries(faixa)) {
    for (const item of KIT_MANIFEST.types[t].items) {
      assert(item.height >= lo && item.height <= hi,
        `${t}/${item.source}: altura ${item.height} m fora de [${lo}, ${hi}]`);
    }
  }
});

test('kit: três níveis por tipo, sempre decrescentes', () => {
  for (const [t, info] of Object.entries(KIT_MANIFEST.types)) {
    for (const item of info.items) {
      assert(item.levels.length === 3, `${t}/${item.source}: ${item.levels.length} níveis`);
      for (let i = 1; i < item.levels.length; i++) {
        assert(item.levels[i].triangles <= item.levels[i - 1].triangles,
          `${t}: nível ${i} (${item.levels[i].triangles}) não é mais barato que o anterior`);
      }
      assert(item.levels[0].triangles > 0, `${t}: nível de perto vazio`);
    }
  }
});

// ---------------------------------------------------------------- kit e população enriquecida
function parseGLBJson(buf) {
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  const magic = dv.getUint32(0, true);
  if (magic !== 0x46546C67) throw new Error('Arquivo não é um GLB válido');
  const jsonLen = dv.getUint32(12, true);
  const jsonType = dv.getUint32(16, true);
  if (jsonType !== 0x4E4F534A) throw new Error('Primeiro bloco do GLB não é JSON');
  const jsonBytes = new Uint8Array(buf.buffer, buf.byteOffset + 20, jsonLen);
  return JSON.parse(new TextDecoder('utf-8').decode(jsonBytes));
}

test('kit: decodificação do GLB e sobrevivência dos 6 materiais', () => {
  const gltf = parseGLBJson(kitBin);
  assert(gltf.meshes && gltf.meshes.length > 0, 'nenhuma malha encontrada no GLB');
  const matNames = gltf.materials.map((m) => m.name);
  for (const req of ['palette', 'branch', 'leaves', 'grass', 'flower', 'flowerLeaf']) {
    assert(matNames.includes(req), `material obrigatório ausente no GLB: ${req}`);
  }
  let totalPrims = 0;
  const matUsage = {};
  for (const mesh of gltf.meshes) {
    for (const prim of mesh.primitives) {
      totalPrims++;
      const matId = prim.material !== undefined ? gltf.materials[prim.material].name : 'none';
      matUsage[matId] = (matUsage[matId] || 0) + 1;
    }
  }
  assert(totalPrims >= 88, `esperadas pelo menos 88 primitivas, encontradas ${totalPrims}`);
  assert(matUsage.branch < totalPrims, 'todas as primitivas viraram branch (defeito crítico do pipeline anterior)');
  assert(matUsage.palette > 0 && matUsage.leaves > 0 && matUsage.grass > 0, 'materiais não foram preservados nas primitivas');
});

test('kit: proveniência dos LODs autorais, triângulos e conservação de altura (< 2%)', () => {
  const pine = KIT_MANIFEST.types.pine.items[0];
  const broad = KIT_MANIFEST.types.broad.items[0];
  // Abeto grande: LOD0 autoral próximo tem 9.729 triângulos
  assert(pine.levels[0].triangles === 9729, `abeto LOD0 próximo com ${pine.levels[0].triangles} triângulos (esperado 9729)`);
  assert(pine.levels[2].triangles <= 3000, `abeto LOD longe com ${pine.levels[2].triangles} triângulos`);
  // Segunda conífera: LOD0 autoral tem 5.113 triângulos
  assert(broad.levels[0].triangles === 5113, `conífera pequena LOD0 com ${broad.levels[0].triangles} triângulos (esperado 5113)`);
  assert(broad.levels[2].triangles <= 1600, `conífera pequena LOD longe com ${broad.levels[2].triangles} triângulos`);

  // Conservação de altura entre níveis (< 2% de diferença)
  for (const [type, info] of Object.entries(KIT_MANIFEST.types)) {
    for (const item of info.items) {
      for (const l of item.levels) {
        assert(Math.abs(l.height_diff_pct) < 2.0,
          `tipo ${type} (${item.source}) nível ${l.level}: variação de altura de ${l.height_diff_pct}% excede 2%`);
      }
    }
  }
});

test('kit: distinção de tint entre coníferas', () => {
  assert(KIT_MANIFEST.types.broad.tint === '#8fae63', 'conífera pequena broad deve ter tint verde quente (#8fae63)');
  assert(KIT_MANIFEST.types.broad.role === 'small_conifer', 'broad deve ser documentada como conífera pequena');
});

test('kit: colisor mede o tronco, não a copa', () => {
  const pine = KIT_MANIFEST.types.pine.items[0];
  assert(pine.collider > 0.2 && pine.collider < 1.2,
    `colisor do abeto em ${pine.collider} m (deveria ser o tronco, não os galhos)`);
  assert(pine.collider < pine.radius * 0.5, 'o colisor está pegando a copa inteira');
  for (const t of ['bush', 'grass']) {
    assert(KIT_MANIFEST.types[t].items[0].collider === 0, `${t} deveria ser atravessável`);
  }
  assert(KIT_MANIFEST.types.log.items[0].collider < 0.8, 'tronco caído colidindo pelo comprimento');
});

test('kit: materiais e texturas declarados', () => {
  for (const id of ['palette', 'branch', 'leaves', 'grass', 'flower', 'flowerLeaf']) {
    const m = KIT_MANIFEST.materials[id];
    assert(m, `material ausente: ${id}`);
    assert(m.texture && KIT_MANIFEST.textures[m.texture], `${id} sem textura preparada`);
    assert(/^#[0-9a-f]{6}$/.test(m.color), `${id} com cor inválida: ${m.color}`);
  }
  assert(KIT_MANIFEST.materials.grass.alphaTest > 0, 'a grama precisa de recorte no alfa');
  assert(KIT_MANIFEST.materials.palette.alphaTest === 0, 'a paleta é opaca');
});

test('kit: envmap do HDRI reduzido e com cores dominantes', () => {
  const sky = KIT_MANIFEST.sky;
  assert(sky, 'envmap do céu ausente');
  assert(sky.width === 256 && sky.height === 128, `envmap em ${sky.width}×${sky.height}`);
  assert(sky.source_resolution[0] >= 8192, 'a fonte deveria ser o HDRI grande do pacote');
  assert(sky.bytes < 200 * 1024, `envmap com ${sky.bytes} bytes`);
  for (const k of ['zenith', 'horizon', 'ground']) {
    assert(/^#[0-9a-f]{6}$/.test(sky.colors[k]), `cor ${k} inválida`);
  }
});

// ---------------------------------------------------------------- população e planejamento
test('população: planejamento lógico determinístico e separação de renderização', () => {
  const p1 = planRegionInstances({ seed: 713337, fresh: true });
  const p2 = planRegionInstances({ seed: 713337, fresh: true });
  assert(p1.report.total === p2.report.total, 'população determinística deve gerar mesmo total');
  // 1.710 árvores e rochas autorais (o resto dos 2.610 são tufos de grama, que viram sementes das manchas)
  assert(p1.report.authored >= 1710 * 0.95, `poucas árvores e rochas autorais preservadas: ${p1.report.authored} de 1710`);
  assert(p1.report.grassSeeds >= 900 * 0.95, `poucos tufos autorais viraram sementes: ${p1.report.grassSeeds}`);

  for (let i = 0; i < 50; i++) {
    close(p1.lists.pine[i].x, p2.lists.pine[i].x, 1e-6, `x da instância ${i}`);
    close(p1.lists.pine[i].z, p2.lists.pine[i].z, 1e-6, `z da instância ${i}`);
  }
});

test('população: metas de densidade por estrato e bioma respeitadas', () => {
  const plan = planRegionInstances({ seed: 713337 });
  for (let b = 0; b <= 5; b++) {
    const d = plan.densityByBiome[b];
    const r = TARGET_RANGES[b];
    assert(d.canopyPerHa >= r.canopy[0] && d.canopyPerHa <= r.canopy[1],
      `bioma ${b} copa fora da meta: ${d.canopyPerHa.toFixed(1)}/ha (meta: ${r.canopy[0]}-${r.canopy[1]})`);
    assert(d.understoryPerHa >= r.understory[0] && d.understoryPerHa <= r.understory[1],
      `bioma ${b} sub-bosque fora da meta: ${d.understoryPerHa.toFixed(1)}/ha (meta: ${r.understory[0]}-${r.understory[1]})`);
  }
});

test('população: montanhas direcionadas em altitude e fora de rotas/POIs', () => {
  const plan = planRegionInstances({ seed: 713337 });
  assert(plan.mountains.length >= 3 && plan.mountains.length <= 8,
    `esperadas 3 a 8 montanhas, obtidas ${plan.mountains.length}`);
  for (const m of plan.mountains) {
    assert(biomeAt(m.x, m.z) === 2, `montanha em (${m.x}, ${m.z}) não está no bioma 2 (altitude)`);
    assert(regionHeightAt(m.x, m.z) > 60, `montanha em cota baixa: ${regionHeightAt(m.x, m.z)} m`);
    assert(routeDistance(m.x, m.z) >= 20, `montanha perto demais de rota: ${routeDistance(m.x, m.z)} m`);
    for (const p of PLACEMENTS) {
      const d = Math.hypot(m.x - p.plan[0], m.z - PLAN_Z_SIGN * p.plan[1]);
      assert(d >= 40, `montanha colidindo com POI ${p.name}: ${d} m`);
    }
  }
});

test('população: zero instâncias terrestres na água', () => {
  const plan = planRegionInstances({ seed: 713337 });
  for (const [k, arr] of Object.entries(plan.lists)) {
    for (const it of arr) {
      assert(!isWaterCell(it.x, it.z), `instância ${k} em (${it.x.toFixed(1)}, ${it.z.toFixed(1)}) dentro da água`);
      const wl = waterLevelAt(it.x);
      assert(it.y >= wl - 0.05, `instância ${k} abaixo do nível do rio (${it.y.toFixed(2)} < ${wl.toFixed(2)})`);
    }
  }
});

test('população: zero objetos sobre pontes, rampas e aproximações (+2m livres)', () => {
  const plan = planRegionInstances({ seed: 713337 });
  for (const b of WORLD_BRIDGES) {
    for (const [k, arr] of Object.entries(plan.lists)) {
      for (const it of arr) {
        const inBridge = Math.abs(it.x - b.x) < b.hw + 2.0 && Math.abs(it.z - b.z) < b.hd + b.apron + 2.0;
        assert(!inBridge, `instância ${k} invadindo tabuleiro/aproximação da ponte ${b.name}`);
      }
    }
  }
});

test('população: folga das rotas respeitando larguras reais', () => {
  const plan = planRegionInstances({ seed: 713337 });
  // Todas as árvores de copa e rochas sólidas respeitam largura/2 + 3m (ou 2m em trilhas)
  for (const it of plan.byStratum.canopy) {
    const rInfo = nearestRouteInfo(it.x, it.z);
    if (!rInfo || rInfo.dist > 15) continue;
    const isTrail = rInfo.width <= 3.0;
    const req = rInfo.width / 2 + (isTrail ? 1.9 : 2.9);
    assert(rInfo.dist >= req, `copa em (${it.x.toFixed(1)}, ${it.z.toFixed(1)}) invade rota ${rInfo.id}: ${rInfo.dist.toFixed(2)} < ${req}`);
  }
});

test('população: nós de coleta e abrigo livres de obstáculos sólidos', () => {
  const plan = planRegionInstances({ seed: 713337 });
  // Abrigo / spawn do jogador (5m livres)
  for (const s of plan.solids) {
    const dSpawn = Math.hypot(s.x - 40, s.z - 375);
    assert(dSpawn >= 4.8, `sólido a ${dSpawn.toFixed(1)} m do spawn do jogador`);
  }
});

test('população: floresta em aglomerados, não em grade uniforme', () => {
  const plan = planRegionInstances({ seed: 713337 });
  const trees = plan.byStratum.canopy;
  assert(trees.length >= 15000, `floresta rala demais: ${trees.length} árvores`);
  // razão variância/média das árvores por célula de 32 m: 1 seria aleatório uniforme
  const counts = new Map();
  for (const t of trees) {
    const k = `${Math.floor((t.x + 512) / 32)},${Math.floor((t.z + 512) / 32)}`;
    counts.set(k, (counts.get(k) || 0) + 1);
  }
  const all = [];
  for (let j = 0; j < 32; j++) for (let i = 0; i < 32; i++) all.push(counts.get(`${i},${j}`) || 0);
  const mean = all.reduce((a, b) => a + b, 0) / all.length;
  const vmr = all.reduce((a, b) => a + (b - mean) ** 2, 0) / all.length / mean;
  assert(vmr > 2, `distribuição uniforme demais (variância/média = ${vmr.toFixed(2)})`);
  // células vazias (clareiras e campos) e células cheias (núcleos) convivem
  assert(all.filter((n) => n === 0).length > 60 && all.filter((n) => n > mean * 1.8).length > 60, 'faltam clareiras ou núcleos densos');
});

test('população: escala, altura, inclinação e tom variam de árvore para árvore', () => {
  const plan = planRegionInstances({ seed: 713337 });
  const trees = plan.byStratum.canopy.filter((t) => !t.authored);
  const sd = (f) => { const v = trees.map(f); const m = v.reduce((a, b) => a + b, 0) / v.length; return Math.sqrt(v.reduce((a, b) => a + (b - m) ** 2, 0) / v.length); };
  assert(sd((t) => t.s) > 0.1, `escala quase constante (desvio ${sd((t) => t.s).toFixed(3)})`);
  assert(sd((t) => t.sy / t.s) > 0.05, 'altura proporcional à escala em todas');
  assert(sd((t) => t.tilt || 0) > 0.01, 'nenhuma árvore inclinada');
  assert(sd((t) => t.fol[0] / t.fol[1]) > 0.02, 'tom da copa sem variação');
});

test('população: áreas funcionais livres de sólidos', () => {
  const plan = planRegionInstances({ seed: 713337 });
  const hit = (x, z, r) => plan.solids.find((s) => Math.hypot(s.x - x, s.z - z) < r + s.r);
  const check = (nome, x, z, r) => { const s = hit(x, z, r); assert(!s, `${nome}: sólido em (${s?.x.toFixed(1)}, ${s?.z.toFixed(1)})`); };
  for (const e of ENCOUNTERS) check(`encontro ${e.id}`, e.x, e.z, 10);
  for (const n of FUNCTIONAL.nodes) check(`nó de coleta ${n.kind}`, n.x, n.z, 3.8);
  check('canteiro', CANTEIRO.x + 1, CANTEIRO.z + 2, 11);
  check('acampamento', LOC.acampamento.x, LOC.acampamento.z, 19);
  check('abrigo', SHELTER.x, SHELTER.z, 4.5);
  check('vigia', LOC.vigia.x, LOC.vigia.z, 4);
  for (const b of PASSAGE_PROPS.banners) check('estandarte da Passagem', b.x, b.z, 2);
  for (const g of GUARD_POSTS) check('posto de guarda', g.x, g.z, 2.5);
  for (const p of PLACEMENTS) {
    if (p.bridge || p.kind === 'field') continue;
    const x = p.plan[0], z = PLAN_Z_SIGN * p.plan[1];
    for (const s of plan.solids) {
      if (s.r >= 10) continue;               // montanhas: conferidas no teste próprio
      const dx = Math.max(0, Math.abs(s.x - x) - p.half[0]), dz = Math.max(0, Math.abs(s.z - z) - p.half[1]);
      assert(Math.hypot(dx, dz) >= s.r + 2.5, `sólido encostado em ${p.name} (${Math.hypot(dx, dz).toFixed(2)} m)`);
    }
  }
});

test('cobertura: determinística, fora da água, da pista e das construções', () => {
  planRegionInstances({ seed: 713337 });
  const cells = [[16, 20], [15, 20], [20, 12], [9, 7], [18, 27], [25, 9], [6, 14], [12, 19]];
  let grass = 0, total = 0, lonely = 0;
  for (const [i, j] of cells) {
    const a = generateCell(i, j), b = generateCell(i, j);
    assert(a.grass.length === b.grass.length && (a.grass[0]?.x ?? 0) === (b.grass[0]?.x ?? 0), `célula ${i},${j} não é determinística`);
    for (const k of ['grass', 'flower', 'mushroom', 'pebble', 'branch']) {
      for (const it of a[k]) {
        total++;
        assert(!isWaterCell(it.x, it.z), `${k} na água em (${it.x.toFixed(1)}, ${it.z.toFixed(1)})`);
        assert(routeDistance(it.x, it.z) > 0.5 || k === 'pebble', `${k} sobre a pista em (${it.x.toFixed(1)}, ${it.z.toFixed(1)})`);
        for (const b of WORLD_BRIDGES) {
          assert(!(Math.abs(it.x - b.x) < b.hw + 2 && Math.abs(it.z - b.z) < b.hd + b.apron + 2), `${k} na ponte ${b.name}`);
        }
      }
    }
    // nenhum tufo solto: cada um tem ao menos dois vizinhos a 1,5 m
    const g = a.grass;
    grass += g.length;
    for (const t of g) {
      let n = 0;
      for (const o of g) if (o !== t && Math.hypot(o.x - t.x, o.z - t.z) < 1.5) { n++; if (n >= 2) break; }
      if (n < 2) lonely++;
    }
  }
  assert(grass > cells.length * 150, `grama rala demais: ${grass} tufos em ${cells.length} células`);
  assert(lonely / grass < 0.1, `${(lonely / grass * 100).toFixed(1)} % de tufos isolados`);
  for (const p of PLACEMENTS) {
    if (p.bridge || p.kind === 'field') continue;
    const ci = Math.floor((p.plan[0] + 512) / CELL), cj = Math.floor((PLAN_Z_SIGN * p.plan[1] + 512) / CELL);
    const c = generateCell(ci, cj);
    const x = p.plan[0], z = PLAN_Z_SIGN * p.plan[1];
    for (const it of c.grass) {
      assert(!(Math.abs(it.x - x) < p.half[0] && Math.abs(it.z - z) < p.half[1]), `grama dentro de ${p.name}`);
    }
  }
});

test('população: nível de detalhe por instância e troca de qualidade reversível', () => {
  const fakeScene = { add: () => {} };
  buildRegionInstances(fakeScene, { density: 1 });
  assert(LOD_FIELDS.length > 5, `poucos campos de LOD: ${LOD_FIELDS.length}`);
  const cam = { x: LOC.bosque.x, z: LOC.bosque.z };
  updateLODFields(cam, true);
  const pine = LOD_FIELDS.find((f) => f.name.startsWith('pine#'));
  // cada instância ativa está no nível da própria distância (ou na histerese de uma fronteira)
  const bands = LOD_BANDS.tree;
  for (const c of pine.chunks) for (let i = 0; i < c.n; i++) {
    const l = c.level[i];
    const d = Math.hypot(c.px[i] - cam.x, c.pz[i] - cam.z);
    let want = 255;
    for (let k = 0; k < bands.length; k++) if (d < bands[k]) { want = k; break; }
    if (l === want) continue;
    const edge = Math.min(...bands.filter((b) => b > 0).map((b) => Math.abs(d - b)));
    assert(edge <= 3.01, `instância a ${d.toFixed(1)} m no nível ${l}, esperado ${want}`);
  }
  // as árvores nunca rareiam: todas as que estão até o corte ficam ativas
  const inReach = pine.chunks.reduce((n, c) => { for (let i = 0; i < c.n; i++) if (Math.hypot(c.px[i] - cam.x, c.pz[i] - cam.z) < bands[bands.length - 1] - 3) n++; return n; }, 0);
  assert(activeCount(pine) >= inReach, `árvores sumiram: ${activeCount(pine)} ativas de ${inReach} no alcance`);

  const counts = () => LOD_FIELDS.map((f) => activeCount(f));
  const alta = counts();
  setSecondaryDensity(0.65);
  updateLODFields(cam, true);
  const baixa = counts();
  assert(baixa.some((n, i) => n < alta[i]), 'a qualidade baixa deve rarear os itens secundários');
  const pineIdx = LOD_FIELDS.indexOf(pine);
  assert(baixa[pineIdx] === alta[pineIdx], 'as árvores não podem rarear com a qualidade');
  setSecondaryDensity(1);
  updateLODFields(cam, true);
  const volta = counts();
  for (let i = 0; i < alta.length; i++) assert(volta[i] === alta[i], `${LOD_FIELDS[i].name}: ${volta[i]} ≠ ${alta[i]} ao voltar para Alta`);

  // Baixa: sem L0/L1, então nada perto fica no nível mais caro
  setLODBands({ tree: [0, 0, 70, 380] });
  updateLODFields(cam, true);
  for (const c of pine.chunks) assert(c.slots[0].count <= 0 && c.slots[1].count <= 0, 'Baixa ainda desenha L0/L1');
  setLODBands({ tree: [28, 65, 150, 700] });
  updateLODFields(cam, true);
});

test('recursos: 82 pontos de coleta preservados (22 minério, 24 madeira, 24 erva, 12 cristal)', () => {
  planRegionInstances({ seed: 713337 });
  const nodes = FUNCTIONAL.nodes;
  const got = { minerio: 0, madeira: 0, erva: 0, cristal: 0 };
  for (const n of nodes) got[n.kind]++;
  assert(got.minerio === 22, `esperados 22 minérios, obtidos ${got.minerio}`);
  assert(got.madeira === 24, `esperadas 24 madeiras, obtidas ${got.madeira}`);
  assert(got.erva === 24, `esperadas 24 ervas, obtidas ${got.erva}`);
  assert(got.cristal === 12, `esperados 12 cristais, obtidos ${got.cristal}`);
  assert(nodes.length === 82, `esperados 82 nós no total, obtidos ${nodes.length}`);
  for (const n of nodes) {
    assert(!isWaterCell(n.x, n.z), `nó na água em (${n.x.toFixed(1)}, ${n.z.toFixed(1)})`);
    assert(routeDistance(n.x, n.z) >= 5, `nó em cima da estrada em (${n.x.toFixed(1)}, ${n.z.toFixed(1)})`);
  }
});

test('população: registro prévio de colliders da natureza', () => {
  // Registra os colliders da natureza no motor de colisão antes do teste de caminhada
  const plan = planRegionInstances({ seed: 713337 });
  assert(plan.solids.length > 2000, `esperados mais de 2000 sólidos de natureza, obtidos ${plan.solids.length}`);
  for (const s of plan.solids) {
    addCircle(s.x, s.z, s.r);
  }
});

// ---------------------------------------------------------------- caminhada real
// Simula o mesmo `moveEntity` do jogo (inclinação máxima, colisores e limites do mundo) ao longo
// das rotas e entre os marcos: é o teste que prova que dá para chegar a pé.
function walk(from, to, { r = 0.45, step = 0.35, maxSteps = 20000, arrive = 3 } = {}) {
  const ent = { pos: { x: from.x, y: groundHeight(from.x, from.z), z: from.z } };
  let stuck = 0, last = Infinity;
  for (let i = 0; i < maxSteps; i++) {
    const dx = to.x - ent.pos.x, dz = to.z - ent.pos.z;
    const d = Math.hypot(dx, dz);
    if (d < arrive) return { ok: true, steps: i, pos: ent.pos };
    const k = step / d;
    moveEntity(ent, dx * k, dz * k, r);
    ent.pos.y = groundHeight(ent.pos.x, ent.pos.z);
    const nd = Math.hypot(to.x - ent.pos.x, to.z - ent.pos.z);
    // contorno simples: quando o avanço direto trava, tenta deslizar para o lado
    if (nd > last - 1e-3) {
      stuck++;
      const side = stuck % 2 ? 1 : -1;
      moveEntity(ent, -dz * k * side, dx * k * side, r);
      ent.pos.y = groundHeight(ent.pos.x, ent.pos.z);
      if (stuck > 400) return { ok: false, steps: i, pos: ent.pos, dist: nd };
    } else stuck = 0;
    last = nd;
  }
  return { ok: false, steps: maxSteps, pos: ent.pos, dist: Math.hypot(to.x - ent.pos.x, to.z - ent.pos.z) };
}

test('caminhada: as quatro rotas são percorríveis ponto a ponto', () => {
  for (const id of ['short_gorge', 'safe_east', 'mine_spur', 'observatory_trail']) {
    const pts = REGION_ROUTE_LINES[id].pts;
    let cur = { x: pts[0][0], z: pts[0][1] };
    for (let i = 1; i < pts.length; i++) {
      const to = { x: pts[i][0], z: pts[i][1] };
      const res = walk(cur, to);
      assert(res.ok, `${id}: travou a caminho do ponto ${i} (faltavam ${res.dist?.toFixed(1)} m)`);
      cur = { x: res.pos.x, z: res.pos.z };
    }
  }
});

test('caminhada: o jogador atravessa as duas pontes', () => {
  for (const b of bridges) {
    const from = { x: b.x, z: b.z - (b.hd + b.apron + 6) };
    const to = { x: b.x, z: b.z + (b.hd + b.apron + 6) };
    const res = walk(from, to);
    assert(res.ok, `${b.name}: travessia bloqueada (faltavam ${res.dist?.toFixed(1)} m)`);
    // o caminho passou mesmo por cima do tabuleiro
    assert(Math.abs(groundHeight(b.x, b.z) - b.y) < 1e-6, `${b.name}: piso não é o tabuleiro`);
  }
});

test('caminhada: dos entrepostos aos marcos principais', () => {
  const sul = { x: LOC.mercado.x, z: LOC.mercado.z };
  const trechos = [
    ['mercado → ponte principal', sul, { x: LOC.ponte.x, z: LOC.ponte.z, r: LOC.ponte.r }],
    ['ponte principal → garganta', { x: LOC.ponte.x, z: LOC.ponte.z, r: LOC.ponte.r }, { x: LOC.passagem.x, z: LOC.passagem.z, r: LOC.passagem.r }],
    ['garganta → entreposto norte', { x: LOC.passagem.x, z: LOC.passagem.z, r: LOC.passagem.r }, { x: LOC.alto.x, z: LOC.alto.z, r: LOC.alto.r }],
    ['ponte principal → mina', { x: LOC.ponte.x, z: LOC.ponte.z, r: LOC.ponte.r }, { x: LOC.mina.x, z: LOC.mina.z, r: LOC.mina.r }],
    ['mina → caverna', { x: LOC.mina.x, z: LOC.mina.z, r: LOC.mina.r }, { x: LOC.caverna.x, z: LOC.caverna.z, r: LOC.caverna.r }],
    ['garganta → observatório', { x: LOC.passagem.x, z: LOC.passagem.z, r: LOC.passagem.r }, { x: LOC.observatorio.x, z: LOC.observatorio.z, r: LOC.observatorio.r }],
    ['observatório → Ermos', { x: LOC.observatorio.x, z: LOC.observatorio.z, r: LOC.observatorio.r }, { x: LOC.ermos.x, z: LOC.ermos.z, r: LOC.ermos.r }],
    ['mercado → ponte secundária', sul, { x: LOC.ponteMenor.x, z: LOC.ponteMenor.z, r: LOC.ponteMenor.r }],
    ['ponte secundária → acampamento', { x: LOC.ponteMenor.x, z: LOC.ponteMenor.z, r: LOC.ponteMenor.r }, { x: LOC.acampamento.x, z: LOC.acampamento.z, r: LOC.acampamento.r }],
    ['acampamento → portal', { x: LOC.acampamento.x, z: LOC.acampamento.z, r: LOC.acampamento.r }, { x: LOC.portal.x, z: LOC.portal.z, r: LOC.portal.r }],
    ['portal → entreposto norte', { x: LOC.portal.x, z: LOC.portal.z, r: LOC.portal.r }, { x: LOC.alto.x, z: LOC.alto.z, r: LOC.alto.r }],
  ];
  for (const [nome, a, b] of trechos) {
    // chegar ao marco é encostar nele: prédios como o observatório ocupam o centro da âncora
    const arrive = Math.max(3, (b.r || 0) * 0.9);
    const res = walk(a, b, { maxSteps: 40000, arrive });
    assert(res.ok, `${nome}: não chegou (faltavam ${res.dist?.toFixed(1)} m)`);
  }
});

test('caminhada: na Turbulenta, da entrada ao objetivo e às duas saídas', () => {
  const p = (id) => { const a = turbulentAnchorToThree(anchor(id, 'turbulent')); return { x: a.x, z: a.z }; };
  const entry = p('entry'), core = p('portal_core'), obj = p('objective_ruins');
  for (const [nome, a, b] of [
    ['entrada → portal', entry, core],
    ['portal → ruínas objetivo', core, obj],
    ['ruínas → saída leste', obj, p('exit_east')],
    ['ruínas → saída oeste', obj, p('exit_west')],
  ]) {
    const res = walk(a, b, { maxSteps: 30000 });
    assert(res.ok, `Turbulenta, ${nome}: não chegou (faltavam ${res.dist?.toFixed(1)} m)`);
  }
});

// ---------------------------------------------------------------- assets extras
test('extras: os seis acréscimos em chão livre, fora das rotas, dos volumes e da coleta', () => {
  planRegionInstances({ seed: 713337 });
  assert(EXTRA_SPOTS.length === EXTRA_DEFS.length, `${EXTRA_SPOTS.length} de ${EXTRA_DEFS.length} acréscimos posicionados`);
  for (const s of EXTRA_SPOTS) {
    const d = EXTRA_DEFS.find((q) => q.id === s.id);
    assert(!isWaterCell(s.x, s.z), `${s.id} na água`);
    assert(routeDistance(s.x, s.z) >= d.road + d.foot - 0.01, `${s.id} a ${routeDistance(s.x, s.z).toFixed(1)} m da estrada`);
    for (const p of PLACEMENTS) {
      const gx = Math.abs(s.x - p.plan[0]) - p.half[0], gz = Math.abs(s.z - PLAN_Z_SIGN * p.plan[1]) - p.half[1];
      assert(Math.max(gx, gz) >= d.foot + 3 - 0.01, `${s.id} encostado em ${p.name}`);
    }
    for (const n of FUNCTIONAL.nodes) assert(Math.hypot(n.x - s.x, n.z - s.z) >= d.r + 4 - 0.01, `${s.id} sobre um ponto de coleta`);
    for (const o of EXTRA_SPOTS) if (o !== s) assert(Math.hypot(o.x - s.x, o.z - s.z) > o.r + s.r, `${s.id} encostado em ${o.id}`);
    assert(WORLD_COLLIDERS.some((c) => c.name === s.id), `${s.id} sem colisor`);
  }
  // o sorteio da coleta não mudou: continua depois dos pontos de jogo e antes dos acréscimos
  assert(FUNCTIONAL.nodes.length === 82, `${FUNCTIONAL.nodes.length} pontos de coleta`);
});

test('extras: trocas só visuais — colisores e tabuleiro das pontes iguais ao manifesto', () => {
  for (const r of REPLACEMENTS) {
    const p = placement(r.volume);
    assert(p, `volume ${r.volume} fora do manifesto`);
    assert(replacedVolume(`${r.volume}_Roof`) === r.volume || replacedVolume(r.volume) === r.volume, `${r.volume} não reconhecido`);
    const x = p.plan[0], z = PLAN_Z_SIGN * p.plan[1];
    if (p.bridge) {
      const b = WORLD_BRIDGES.find((q) => q.name === r.volume);
      assert(b && Math.abs(b.y - (p.deck_final ?? p.deck)) < 1e-6 && Math.abs(b.x - x) < 1e-6 && Math.abs(b.z - z) < 1e-6, `${r.volume}: tabuleiro mudou`);
      continue;
    }
    const c = WORLD_COLLIDERS.find((q) => q.name === r.volume);
    if (p.collider === false) { assert(!c, `${r.volume}: ganhou colisor`); continue; }   // a doca não tem colisor
    if (!c) {
      // volume cortado pelo corredor da estrada: colisores redondos só na parte fora da pista, dentro da caixa
      const parts = WORLD_COLLIDERS.filter((q) => q.name.startsWith(`${r.volume}#`));
      assert(parts.length && parts.every((q) => Math.abs(q.x - x) <= p.half[0] + 1e-6 && Math.abs(q.z - z) <= p.half[1] + 1e-6), `${r.volume}: colisor mudou`);
      continue;
    }
    assert(c && Math.abs(c.x - x) < 1e-6 && Math.abs(c.z - z) < 1e-6, `${r.volume}: colisor mudou de lugar`);
    if (c.r != null) assert(Math.abs(c.r - Math.max(0.35, Math.min(p.half[0], p.half[1]) * 0.92)) < 1e-6, `${r.volume}: raio mudou`);
    else assert(Math.abs(c.hw - p.half[0]) < 1e-6 && Math.abs(c.hd - p.half[1]) < 1e-6, `${r.volume}: caixa mudou`);
  }
});

test('extras: nenhuma construção antiga à vista — todo prédio, ruína, ponte, torre e doca tem troca', () => {
  const kinds = new Set(['building', 'ruin', 'bridge', 'tower', 'dock']);
  const old = PLACEMENTS.filter((p) => kinds.has(p.kind) && !REPLACEMENTS.some((r) => r.volume === p.name)).map((p) => p.name);
  assert(old.length === 0, `sem troca: ${old.join(', ')}`);
  assert(replacedVolume('POI_RegionalObservatory') === null && replacedVolume('Mine_Rock_00') === null, 'POI ou rocha trocado');
  assert(new Set(REPLACEMENTS.map((r) => r.volume)).size === REPLACEMENTS.length, 'volume trocado duas vezes');
});

test('extras: manifesto de origem completo, com licença e hash em cada derivado', () => {
  const ids = EXTRA_MANIFEST.assets.map((a) => a.id);
  assert(new Set(ids).size === ids.length && ids.length === 30, `${ids.length} derivados`);
  for (const a of EXTRA_MANIFEST.assets) {
    assert(/^[0-9a-f]{64}$/.test(a.source.sha256) && a.source.file, `${a.id}: origem incompleta`);
    assert(a.license && a.license.length > 5, `${a.id}: sem licença`);
    assert(a.triangles > 0 && a.size.every((v) => v > 0), `${a.id}: derivado vazio`);
    if (/Pigcraft/.test(a.author || '')) assert(/CC-BY-4\.0/.test(a.license) && a.url, `${a.id}: atribuição CC-BY incompleta`);
    if (/kenney/.test(a.source.file)) assert(/CC0/.test(a.license) && /Kenney/.test(a.author), `${a.id}: crédito Kenney incompleto`);
  }
  for (const r of REPLACEMENTS) assert(ids.includes(r.asset), `troca sem derivado: ${r.asset}`);
  for (const d of EXTRA_DEFS) assert(ids.includes(d.asset), `acréscimo sem derivado: ${d.asset}`);
});

// ---------------------------------------------------------------- relatório
const failed = results.filter((r) => !r.ok);
for (const r of results) console.log(`${r.ok ? '  ok  ' : ' FALHA'} ${r.name}${r.ok ? '' : ` — ${r.error}`}`);
console.log(`\n${results.length - failed.length}/${results.length} testes aprovados`);
if (failed.length) {
  console.log('FALHAS:');
  for (const r of failed) console.log(` - ${r.name}: ${r.error}`);
}
globalThis.__testExit = failed.length ? 1 : 0;
