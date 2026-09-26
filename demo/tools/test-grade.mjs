// Test creating the 10 terrain layers with real PBR detail
import { readFile } from 'node:fs/promises';
import { join } from 'node:path';

function grade(alb, nrm, [tr, tg, tb], roughnessScale = 1.0) {
  const outAlb = new Uint8Array(alb.length);
  const outNrm = new Uint8Array(nrm.length);
  
  let sumLum = 0;
  for (let q = 0; q < alb.length; q += 4) {
    sumLum += alb[q] * 0.299 + alb[q + 1] * 0.587 + alb[q + 2] * 0.114;
  }
  const avgLum = Math.max(1, sumLum / (alb.length / 4));

  for (let q = 0; q < alb.length; q += 4) {
    const lum = alb[q] * 0.299 + alb[q + 1] * 0.587 + alb[q + 2] * 0.114;
    const factor = (lum / avgLum);
    // Soft contrast curve
    const adj = 0.5 + 0.5 * factor;
    outAlb[q] = Math.min(255, Math.round(tr * adj));
    outAlb[q + 1] = Math.min(255, Math.round(tg * adj));
    outAlb[q + 2] = Math.min(255, Math.round(tb * adj));
    outAlb[q + 3] = alb[q + 3]; // Height in alpha

    outNrm[q] = nrm[q];
    outNrm[q + 1] = nrm[q + 1];
    outNrm[q + 2] = nrm[q + 2];
    outNrm[q + 3] = Math.min(255, Math.round(nrm[q + 3] * roughnessScale)); // Roughness in alpha
  }
  return { alb: outAlb, nrm: outNrm };
}

console.log('grade function defined successfully');
