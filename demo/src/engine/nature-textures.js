// Materiais fotográficos locais, incorporados ao HTML pelo build.
// Cor+altura e normal+rugosidade mantêm o contrato do terreno e da projeção triplanar.
import * as THREE from 'three';
import { NATURE } from '../../assets/nature/textures.js';
import terrainGrassUrl from '../../assets/nature-kit/tex/terrainGrass.webp';
import terrainDirtUrl from '../../assets/nature-kit/tex/terrainDirt.webp';

// Mipmaps calculados aqui, em vez de pela GPU: em alguns drivers o generateMipmap falha para
// texturas sRGB com alfa e os níveis distantes saem vazios — a folhagem some e o chão fica lavado.
// `coverage` mantém a proporção de texels acima do corte em todos os níveis (recorte das folhas).
function mipChain(data, size, layers = 1, coverage = 0) {
  const levels = [];
  let src = data, w = size, h = size;
  const alphaAbove = (buf, cut) => { let n = 0; for (let i = 3; i < buf.length; i += 4) if (buf[i] >= cut) n++; return n / (buf.length / 4); };
  const target = coverage ? alphaAbove(data, coverage * 255) : 0;
  while (w > 1 || h > 1) {
    const nw = Math.max(1, w >> 1), nh = Math.max(1, h >> 1);
    const dst = new Uint8Array(nw * nh * 4 * layers);
    for (let l = 0; l < layers; l++) {
      const so = l * w * h * 4, doff = l * nw * nh * 4;
      for (let y = 0; y < nh; y++) for (let x = 0; x < nw; x++) {
        const x0 = Math.min(w - 1, x * 2), x1 = Math.min(w - 1, x * 2 + 1);
        const y0 = Math.min(h - 1, y * 2), y1 = Math.min(h - 1, y * 2 + 1);
        for (let c = 0; c < 4; c++) {
          dst[doff + (y * nw + x) * 4 + c] = (
            src[so + (y0 * w + x0) * 4 + c] + src[so + (y0 * w + x1) * 4 + c] +
            src[so + (y1 * w + x0) * 4 + c] + src[so + (y1 * w + x1) * 4 + c] + 2) >> 2;
        }
      }
    }
    if (coverage) {
      // reescala o alfa até o nível cobrir a mesma área do original
      let lo = 0.4, hi = 6;
      for (let it = 0; it < 12; it++) {
        const k = (lo + hi) / 2;
        let n = 0;
        for (let i = 3; i < dst.length; i += 4) if (Math.min(255, dst[i] * k) >= coverage * 255) n++;
        if (n / (dst.length / 4) < target) lo = k; else hi = k;
      }
      const k = (lo + hi) / 2;
      for (let i = 3; i < dst.length; i += 4) dst[i] = Math.min(255, dst[i] * k);
    }
    levels.push({ data: dst, width: nw, height: nh });
    src = dst; w = nw; h = nh;
  }
  return levels;
}

export function texture(data, size, color = false, coverage = 0) {
  const t = new THREE.DataTexture(data, size, size, THREE.RGBAFormat);
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  if (coverage) {
    // recorte de folha: os níveis precisam manter a área coberta, senão a copa some ao longe
    t.mipmaps = [{ data, width: size, height: size }, ...mipChain(data, size, 1, coverage)];
    t.generateMipmaps = false;
  } else t.generateMipmaps = true;
  t.minFilter = THREE.LinearMipmapLinearFilter;
  t.magFilter = THREE.LinearFilter;
  t.anisotropy = 8;
  if (color) t.colorSpace = THREE.SRGBColorSpace;
  t.needsUpdate = true;
  return t;
}

export async function pixels(url, size, flip = true) {
  const img = new Image();
  await new Promise((resolve, reject) => {
    img.onload = resolve;
    img.onerror = () => reject(new Error('Não foi possível abrir uma textura de natureza incorporada.'));
    img.src = url;
  });
  const canvas = document.createElement('canvas');
  canvas.width = canvas.height = size;
  const ctx = canvas.getContext('2d', { willReadFrequently: true });
  // DataTexture não inverte as linhas: prepara a orientação OpenGL explicitamente. As UV vindas de
  // glTF já contam a origem no topo, então essas texturas pedem `flip = false`.
  if (flip) { ctx.translate(0, size); ctx.scale(1, -1); }
  ctx.drawImage(img, 0, 0, size, size);
  return new Uint8Array(ctx.getImageData(0, 0, size, size).data);
}

// Contrato do shader: altura no alfa do albedo, rugosidade no alfa da normal. Os derivados trazem
// as duas num mapa de dados (R = altura, G = rugosidade), então aqui é só recompor.
async function surface(id, size) {
  const src = NATURE[id];
  const [alb, nrm, data] = await Promise.all([src.col, src.nrm, src.hr].map((url) => pixels(url, size)));
  for (let q = 0; q < alb.length; q += 4) {
    alb[q + 3] = data[q];
    nrm[q + 3] = data[q + 1];
  }
  return { alb, nrm, size, scale: 1 / src.metres };
}

function resizeLegacy(data, from, to) {
  const result = new Uint8Array(to * to * 4);
  for (let y = 0; y < to; y++) for (let x = 0; x < to; x++) {
    const at = (Math.floor(y * from / to) * from + Math.floor(x * from / to)) * 4;
    result.set(data.subarray(at, at + 4), (y * to + x) * 4);
  }
  return result;
}

function terrainArray(layers, size, color) {
  const data = new Uint8Array(size * size * 4 * layers.length);
  layers.forEach((layer, i) => data.set(layer, i * size * size * 4));
  const t = new THREE.DataArrayTexture(data, size, size, layers.length);
  t.format = THREE.RGBAFormat;
  t.wrapS = t.wrapT = THREE.RepeatWrapping;
  // DataArrayTexture: o three ignora mipmaps fornecidos (só envia o nível 0), então aqui a GPU gera
  t.generateMipmaps = true; t.minFilter = THREE.LinearMipmapLinearFilter;
  t.magFilter = THREE.LinearFilter; t.anisotropy = 8;
  if (color) t.colorSpace = THREE.SRGBColorSpace;
  t.needsUpdate = true;
  return t;
}

const region = (x0, y0, x1, y1, rotation) => ({
  u0: x0, v0: 1 - y1, u1: x1, v1: 1 - y0,
  aspect: rotation ? (y1 - y0) / (x1 - x0) : (x1 - x0) / (y1 - y0),
  ...(rotation ? { rotation } : {}),
});

// ---------------------------------------------------------------- camadas do kit de natureza
// Combina os materiais PBR CC0 existentes com a paleta cromática do Ultimate Nature - Starter.
// O albedo preserva o microdetalhe e altura no alfa; a normal preserva relevo e rugosidade no alfa.

function gradeSurface(srcSurface, targetPalette, { gains = [1, 1, 1], mix = 0.55, gain = 1.0, roughnessScale = 1.0, roughnessOffset = 0 } = {}) {
  const n = srcSurface.alb.length;
  const alb = new Uint8Array(n);
  const nrm = new Uint8Array(n);
  for (let q = 0; q < n; q += 4) {
    const r = srcSurface.alb[q], g = srcSurface.alb[q + 1], b = srcSurface.alb[q + 2];
    const lum = (r * 0.299 + g * 0.587 + b * 0.114) / 128.0;
    const tr = targetPalette ? targetPalette[q] : 128;
    const tg = targetPalette ? targetPalette[q + 1] : 128;
    const tb = targetPalette ? targetPalette[q + 2] : 128;

    const br = r * (1 - mix) + (tr * lum) * mix;
    const bg = g * (1 - mix) + (tg * lum) * mix;
    const bb = b * (1 - mix) + (tb * lum) * mix;

    alb[q] = Math.min(255, Math.max(0, Math.round(br * gains[0] * gain)));
    alb[q + 1] = Math.min(255, Math.max(0, Math.round(bg * gains[1] * gain)));
    alb[q + 2] = Math.min(255, Math.max(0, Math.round(bb * gains[2] * gain)));
    alb[q + 3] = srcSurface.alb[q + 3];   // altura autêntica para mistura de splatting

    nrm[q] = srcSurface.nrm[q];
    nrm[q + 1] = srcSurface.nrm[q + 1];
    nrm[q + 2] = srcSurface.nrm[q + 2];
    const rgh = srcSurface.nrm[q + 3];
    nrm[q + 3] = Math.min(255, Math.max(0, Math.round(rgh * roughnessScale + roughnessOffset * 255)));
  }
  return { alb, nrm };
}

const KIT_METRES = 2;

export async function applyNatureTextures(TEX, progress) {
  const size = 512, loaded = {};
  for (const id of ['sparse_grass', 'rocky_trail_02', 'forrest_ground_01', 'rock_3', 'dark_rock', 'river_small_rocks']) {
    progress?.(`natureza: ${id.replaceAll('_', ' ')}`);
    loaded[id] = await surface(id, size);
  }

  progress?.('camadas do terreno');
  const kitSrc = {
    grass: await pixels(terrainGrassUrl, size, false),
    dirt: await pixels(terrainDirtUrl, size, false),
  };

  // Mapeamento dos 10 estratos do shader:
  // 0 campo: paleta de grama UNS + microdetalhe de sparse_grass
  // 1 grama seca: paleta UNS dourada + sparse_grass
  // 2 rocha e encostas: rock_3 com triplanar
  // 3 trilhas e terra: paleta UNS dirt + rocky_trail_02
  // 4 areia e margens: river_small_rocks / terra fina + paleta UNS areia
  // 5 rocha de altitude: dark_rock
  // 6 cinza dos Ermos: forrest_ground / rock desaturado e cinza queimado
  // 7 calçamento / praça mercado: rocky_trail_02 calçado
  // 8 solo de bosque: cor UNS verde musgo + forrest_ground_01
  // 9 seixos do leito do rio: river_small_rocks úmido (rugosidade baixa)
  const layerDefs = [
    { src: 'sparse_grass', pal: kitSrc.grass, gains: [1.02, 1.05, 0.95], mix: 0.60, gain: 1.0 },
    { src: 'sparse_grass', pal: kitSrc.grass, gains: [1.20, 1.10, 0.70], mix: 0.65, gain: 1.05 },
    { src: 'rock_3', pal: null, gains: [1.00, 1.00, 1.00], mix: 0.00, gain: 0.95 },
    { src: 'rocky_trail_02', pal: kitSrc.dirt, gains: [1.00, 0.98, 0.95], mix: 0.58, gain: 0.92 },
    { src: 'river_small_rocks', pal: kitSrc.dirt, gains: [1.14, 1.08, 0.92], mix: 0.55, gain: 0.95 },
    { src: 'dark_rock', pal: null, gains: [0.95, 0.95, 1.00], mix: 0.00, gain: 0.92 },
    { src: 'forrest_ground_01', pal: null, gains: [0.85, 0.85, 0.88], mix: 0.75, gain: 0.78, roughnessScale: 1.1 },
    { src: 'rocky_trail_02', pal: kitSrc.dirt, gains: [1.02, 1.00, 0.98], mix: 0.45, gain: 0.88 },
    { src: 'forrest_ground_01', pal: kitSrc.grass, gains: [0.85, 0.92, 0.78], mix: 0.52, gain: 0.82 },
    { src: 'river_small_rocks', pal: null, gains: [0.92, 0.95, 1.00], mix: 0.20, gain: 0.90, roughnessScale: 0.35, roughnessOffset: 0.05 },
  ];

  const albedos = [], normals = [];
  for (const def of layerDefs) {
    const s = gradeSurface(loaded[def.src], def.pal, def);
    albedos.push(s.alb);
    normals.push(s.nrm);
  }

  TEX.terrainAlb.dispose(); TEX.terrainNrm.dispose();
  TEX.terrainAlb = terrainArray(albedos, size, true);
  TEX.terrainNrm = terrainArray(normals, size, false);
  TEX.terrainScales = [
    1 / 2.0, 1 / 2.2, 1 / 2.5, 1 / 2.0, 1 / 2.0,
    1 / 2.5, 1 / 2.5, 1 / 2.0, 1 / 2.5, 1 / 2.0,
  ];
  TEX.kitTerrain = { grass: kitSrc.grass, dirt: kitSrc.dirt, size, metres: KIT_METRES };

  for (const [name, id] of [['cascaNatural', 'bark_brown_02'], ['cascaPinheiro', 'pine_bark'], ['rochaNatural', 'rock_3'], ['rochaEscura', 'dark_rock']]) {
    progress?.(`superfícies naturais: ${name}`);
    const s = await surface(id, 1024);
    TEX.surf[name] = { alb: texture(s.alb, s.size, true), nrm: texture(s.nrm, s.size), scale: s.scale };
  }

  // Estradas e campos do mundo do Blender: usam texturas PBR autênticas com cores graduadas na paleta UNS
  const sTrilha = gradeSurface(loaded.rocky_trail_02, kitSrc.dirt, { gains: [1.02, 0.99, 0.95], mix: 0.45, gain: 0.88 });
  TEX.surf.trilha = { alb: texture(sTrilha.alb, size, true), nrm: texture(sTrilha.nrm, size), scale: 1 / KIT_METRES };

  const sGrama = gradeSurface(loaded.sparse_grass, kitSrc.grass, { gains: [1.08, 1.04, 0.82], mix: 0.55, gain: 0.96 });
  TEX.surf.gramaCampo = { alb: texture(sGrama.alb, size, true), nrm: texture(sGrama.nrm, size), scale: 1 / KIT_METRES };

  for (const key of ['leaves', 'needles']) {
    progress?.(key === 'leaves' ? 'folhas reais' : 'ramos de pinheiro');
    const src = NATURE[key], size = src.size;
    const [alb, nrm, rough] = await Promise.all([src.col, src.nrm, src.rgh].map((url) => pixels(url, size)));
    TEX[key].dispose();
    TEX[key] = texture(alb, size, true, 0.45);
    TEX[key].wrapS = TEX[key].wrapT = THREE.ClampToEdgeWrapping;
    TEX[key].userData.normalMap = texture(nrm, size);
    TEX[key].userData.roughnessMap = texture(rough, size);
  }
  // Regiões inspecionadas dos atlas: somente folhas/ramos, sem troncos, pinhas ou resíduos.
  TEX.foliageRegions = {
    leaves: [region(.015, .015, .150, .515), region(.163, .016, .340, .401), region(.357, .035, .488, .371), region(.513, .048, .655, .369), region(.690, .015, .835, .423)],
  };
}


