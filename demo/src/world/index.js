// Sequência de carregamento do mundo preparado, na ordem em que o boot precisa dela:
// manifesto → heightfields e colisores → terreno visual → água e estradas → arquitetura →
// vegetação instanciada → Região Turbulenta.
//
// Tudo aqui é offline: os binários e os GLB chegam como bytes embutidos no próprio HTML.
import { assertRuntimeSchema, WORLD, REGION, TURBULENT } from './runtime-manifest.js';
import { initTerrainData, buildTerrain } from '../engine/terrain.js';
import { loadRegionProps, loadTurbulentProps, buildDebugOverlay, WORLD_SCENE } from './scene-world.js';
import { buildRegionInstances, buildTurbulentInstances, INSTANCE_REPORT } from './world-instances.js';
import { WORLD_COLLIDERS, WORLD_BRIDGES, COLLIDER_REPORT } from './world-colliders.js';
import { decoderReady } from './runtime-loader.js';
import { installNatureKit, KIT_STATUS } from './nature-kit.js';
import { bakeImpostors, IMPOSTOR_STATUS } from './impostors.js';
import { RIVER } from './heightfield.js';

export const WORLD_STATUS = {
  loaded: false, layoutVersion: WORLD.world_layout_version,
  colliders: 0, bridges: 0, instances: null, kit: null, timings: {},
};

const tick = () => new Promise((r) => setTimeout(r, 0));

// `progress(etapa)` recebe o nome legível de cada sub-etapa, para a barra de carregamento.
export async function loadWorldScene(scene, progress, { density = 1, debug = false, tier = 'alta', renderer = null } = {}) {
  const mark = (k, t0) => { WORLD_STATUS.timings[k] = Math.round(performance.now() - t0); };

  progress?.('manifesto do mundo');
  assertRuntimeSchema();
  await decoderReady();

  let t = performance.now();
  progress?.('relevo e colisores');
  initTerrainData();
  mark('heightfield', t);
  await tick();

  t = performance.now();
  let done = 0;
  buildTerrain(scene, (id) => { done++; progress?.(`terreno ${done}/${REGION.chunks.length}`); });
  mark('terrain', t);
  await tick();

  t = performance.now();
  progress?.('construções e pontos de interesse');
  await loadRegionProps(scene);
  mark('props', t);
  await tick();

  t = performance.now();
  progress?.('kit de natureza');
  await installNatureKit({ tier });
  mark('kit', t);
  await tick();

  t = performance.now();
  progress?.('silhuetas da floresta distante');
  bakeImpostors(renderer);
  mark('impostors', t);
  await tick();

  t = performance.now();
  progress?.('vegetação instanciada');
  buildRegionInstances(scene, { density });
  mark('instances', t);
  await tick();

  t = performance.now();
  progress?.('Região Turbulenta');
  await loadTurbulentProps(scene);
  buildTurbulentInstances(scene);
  mark('turbulent', t);
  await tick();

  if (debug) buildDebugOverlay(scene, { colliders: WORLD_COLLIDERS, bridges: WORLD_BRIDGES });

  WORLD_STATUS.loaded = true;
  WORLD_STATUS.colliders = WORLD_COLLIDERS.length;
  WORLD_STATUS.bridges = WORLD_BRIDGES.length;
  WORLD_STATUS.instances = { ...INSTANCE_REPORT };
  WORLD_STATUS.kit = { ...KIT_STATUS };
  WORLD_STATUS.impostors = { ...IMPOSTOR_STATUS };
  WORLD_STATUS.river = { ...RIVER.stats };
  WORLD_STATUS.colliderReport = { ...COLLIDER_REPORT, skippedOnRoute: [...COLLIDER_REPORT.skippedOnRoute] };
  return WORLD_STATUS;
}

export { WORLD, REGION, TURBULENT, WORLD_SCENE };
