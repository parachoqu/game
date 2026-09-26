import { biomeAt, isWaterCell, wetnessAt, regionHeightAt } from '../src/world/heightfield.js';
import { initRouteField, routeDistance } from '../src/world/world-routes.js';
import { initTerrainData } from '../src/engine/terrain.js';
import { planRegionInstances } from '../src/world/world-instances.js';
import { generateCell, CELL } from '../src/world/ground-cover.js';
import { forestDensity, inWastes } from '../src/world/vegetation-fields.js';
import { lowGapExact } from '../src/world/functional-areas.js';
initTerrainData();
const plan = planRegionInstances({ seed: 713337 });
const N = 1024, cov = new Uint8Array(N * N);
const stamp = (x, z, r) => {
  const i0 = Math.max(0, Math.floor(x + 512 - r)), i1 = Math.min(N - 1, Math.ceil(x + 512 + r));
  const j0 = Math.max(0, Math.floor(z + 512 - r)), j1 = Math.min(N - 1, Math.ceil(z + 512 + r));
  for (let j = j0; j <= j1; j++) for (let i = i0; i <= i1; i++) if (Math.hypot(i + 0.5 - 512 - x, j + 0.5 - 512 - z) < r) cov[j * N + i] = 1;
};
let ng = 0;
for (let cj = 0; cj < 32; cj++) for (let ci = 0; ci < 32; ci++) {
  const c = generateCell(ci, cj);
  for (const g of c.grass) { stamp(g.x, g.z, 0.75 * g.s); ng++; }
}
// bushes / trees from plan
const lists = plan?.lists || plan?.byKind || plan;
let keys = lists ? Object.keys(lists).slice(0, 30) : [];
console.log('plan keys', keys.join(','));
const out = new Uint8Array(256 * 256);
let green = 0, bare = 0;
for (let j = 0; j < N; j++) for (let i = 0; i < N; i++) {
  const x = i + 0.5 - 512, z = j + 0.5 - 512;
  if ((i & 1) || (j & 1)) continue;
  const b = biomeAt(x, z);
  const isGreen = (b === 0 || b === 1) && !isWaterCell(x, z) && wetnessAt(x, z) < 0.3 && routeDistance(x, z) > 6 && !inWastes(x, z) && forestDensity(x, z) < 0.55 && lowGapExact(x, z) > 0.3;
  if (!isGreen) continue;
  green++;
  const bareHere = !cov[j * N + i];
  if (bareHere) { bare++; out[(j >> 2) * 256 + (i >> 2)]++; }
}
console.log(JSON.stringify({ grassTufts: ng, green, bare, bareFrac: +(bare / green).toFixed(3) }));
// top bare 16x16 m blocks
const blocks = [];
for (let bj = 0; bj < 64; bj++) for (let bi = 0; bi < 64; bi++) {
  let s = 0; for (let j = 0; j < 4; j++) for (let i = 0; i < 4; i++) s += out[(bj * 4 + j) * 256 + bi * 4 + i];
  blocks.push([s, -512 + bi * 16 + 8, -512 + bj * 16 + 8]);
}
blocks.sort((a, b) => b[0] - a[0]);
console.log('topBare', JSON.stringify(blocks.slice(0, 25).map(([s, x, z]) => [s, x, z, +regionHeightAt(x, z).toFixed(1)])));
console.log('MAP' + Buffer.from(out).toString('base64'));
