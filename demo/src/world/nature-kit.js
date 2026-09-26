// Kit de natureza "Ultimate Nature – Starter" (Innerverse Interactive), preparado por
// `tools/build-nature-kit.mjs` e embutido no HTML.
//
// O kit não reescreve nada: ele preenche o provedor de `engine/props.js`, e todo o resto —
// `instanced()`, os dois níveis de detalhe por bloco, o vento, a sombra por distância, o registro
// de qualidade e a poda por distância — segue funcionando como sempre. Se o carregamento falhar,
// `props.js` volta sozinho aos protótipos procedurais e o jogo continua jogável.
import * as THREE from 'three';
import MANIFEST from '../../assets/nature-kit/kit-manifest.json';
import kitBin from '../../assets/nature-kit/nature-kit.glb';
import paletteUrl from '../../assets/nature-kit/tex/palette.webp';
import branchUrl from '../../assets/nature-kit/tex/branch.webp';
import leavesUrl from '../../assets/nature-kit/tex/leaves.webp';
import grassUrl from '../../assets/nature-kit/tex/grass.webp';
import flowerUrl from '../../assets/nature-kit/tex/flower.webp';
import flowerLeafUrl from '../../assets/nature-kit/tex/flowerLeaf.webp';
import skyEnvUrl from '../../assets/nature-kit/sky-env.webp';
import { parseSceneGLB } from './runtime-loader.js';
import { KIT } from '../engine/props.js';
import { patchMaterial, foliageMat, foliageDepth } from '../engine/materials.js';
import { pixels, texture } from '../engine/nature-textures.js';

export const KIT_MANIFEST = MANIFEST;
export const KIT_STATUS = { loaded: false, types: 0, error: null, tier: null, levels: null };

const TEX_URL = {
  palette: paletteUrl, branch: branchUrl, leaves: leavesUrl,
  grass: grassUrl, flower: flowerUrl, flowerLeaf: flowerLeafUrl,
};

// Nível do kit usado em cada faixa de distância, por qualidade. O kit traz três; a cena desenha
// dois por bloco (perto e longe), como o resto da vegetação sempre fez.
//
// Os volumes grandes têm tabela própria: o abeto custa 2.600 triângulos no nível de perto e, numa
// mata fechada, sozinho responde por mais da metade da geometria do quadro. Em Alta ele já entra
// no nível intermediário, que a essa distância é indistinguível.
const TIER_LEVELS = {
  alta: [0, 2],
  media: [1, 2],
  baixa: [2, 2],
};
const BIG_TIER_LEVELS = {
  alta: [0, 2],
  media: [1, 2],
  baixa: [2, 2],
};
const BIG_TYPES = new Set(['pine', 'broad', 'dead', 'cliff', 'mountain']);

// Calibração de albedo por atlas: folhagem recebe 0.82 (eliminando o corte destrutivo de 0.32),
// Mantém albedos autorais dos .mat Unity fiéis sob iluminação Three.js e ACES.
const ALBEDO_GAIN = { foliage: 1.0, opaque: 1.0 };
const ENV_INTENSITY = 0.25;

// Tipos que o mundo distribui e que o kit passa a desenhar. `crystalShard` continua procedural:
// é um efeito da Região Turbulenta, não vegetação.
const KIT_TYPES = ['pine', 'broad', 'dead', 'bush', 'grass', 'flower', 'mushroom',
  'rock', 'cliff', 'pebble', 'log', 'stump', 'branch', 'mountain'];

// ---------------------------------------------------------------- materiais
async function buildMaterials() {
  const mats = {}, depth = {};
  for (const [id, cfg] of Object.entries(MANIFEST.materials)) {
    if (!cfg.texture || !TEX_URL[cfg.texture]) continue;
    const info = MANIFEST.textures[cfg.texture];
    const data = await pixels(TEX_URL[cfg.texture], info.size, false);
    if (cfg.foliage) {
      // recorte no alfa: os mipmaps preservam a área coberta, senão a copa some com a distância
      const tex = texture(data, info.size, true, cfg.alphaTest);
      tex.wrapS = tex.wrapT = THREE.ClampToEdgeWrapping;
      const m = foliageMat(tex, { wind: id === 'grass' ? 0 : 1, grass: id === 'grass', color: cfg.color });
      m.color.multiplyScalar(ALBEDO_GAIN.foliage);
      m.alphaTest = cfg.alphaTest;
      m.roughness = cfg.roughness;
      m.metalness = 0;
      m.envMapIntensity = ENV_INTENSITY;
      mats[id] = m;
      depth[id] = foliageDepth(tex, id === 'grass' ? 0 : 1);
    } else {
      const tex = texture(data, info.size, true);
      tex.wrapS = tex.wrapT = THREE.ClampToEdgeWrapping;
      const m = new THREE.MeshStandardMaterial({
        map: tex, color: cfg.color, roughness: cfg.roughness, metalness: 0, envMapIntensity: ENV_INTENSITY,
      });
      m.color.multiplyScalar(ALBEDO_GAIN.opaque);
      patchMaterial(m, ['occ']);
      mats[id] = m;
    }
  }
  return { mats, depth };
}

// ---------------------------------------------------------------- instalação
export async function installNatureKit({ tier = 'alta' } = {}) {
  try {
    const [gltf, { mats, depth }] = await Promise.all([parseSceneGLB(kitBin), buildMaterials()]);
    const levels = TIER_LEVELS[tier] || TIER_LEVELS.alta;
    const bigLevels = BIG_TIER_LEVELS[tier] || BIG_TIER_LEVELS.alta;

    // `<tipo>_<variante>_L<nível>` → lista de [geometria, id do material]
    //
    // A quantização do pacote guarda as posições como inteiros normalizados e devolve a escala ao
    // nó. Como o kit desenha protótipos soltos em InstancedMesh, a transformação do nó é assada na
    // geometria; antes disso os atributos precisam virar float, senão a multiplicação satura no
    // intervalo [-1, 1] e o abeto de 12,5 m chega à cena com 2 m.
    gltf.scene.updateMatrixWorld(true);
    const table = new Map();
    gltf.scene.traverse((o) => {
      if (!o.isMesh) return;
      // Malha de uma peça só (rocha, grama, seixo…) leva o nome do nó; malha de várias peças vira
      // um grupo com o nome do nó e filhos numerados. Olhar só o pai deixava as de uma peça de fora,
      // e elas caíam nos protótipos procedurais.
      const name = /^[a-z]+_\d+_L\d$/.test(o.name) ? o.name : o.parent?.name || o.name;
      const m = /^([a-z]+)_(\d+)_L(\d)$/.exec(name);
      if (!m) return;
      const key = `${m[1]}|${m[2]}|${m[3]}`;
      const matId = o.material?.name || 'palette';
      const geo = o.geometry.clone();
      for (const key of ['position', 'normal']) {
        const attr = geo.getAttribute(key);
        if (!attr || !attr.normalized) continue;
        const out = new Float32Array(attr.count * attr.itemSize);
        for (let i = 0; i < attr.count; i++) {
          for (let j = 0; j < attr.itemSize; j++) out[i * attr.itemSize + j] = attr.getComponent(i, j);
        }
        geo.setAttribute(key, new THREE.BufferAttribute(out, attr.itemSize));
      }
      geo.applyMatrix4(o.matrixWorld);
      geo.computeBoundingSphere();
      geo.computeBoundingBox();
      (table.get(key) || table.set(key, []).get(key)).push([geo, matId]);
    });
    if (!table.size) throw new Error('nenhum protótipo reconhecido no kit');

    const typeInfo = {};
    for (const [type, info] of Object.entries(MANIFEST.types)) {
      if (!KIT_TYPES.includes(type)) continue;
      typeInfo[type] = {
        variants: info.variants,
        items: info.items,
        tint: info.tint ? new THREE.Color(info.tint) : null,
      };
    }

    const cache = new Map();
    const partsFor = (kind, variant, near) => {
      const pick = BIG_TYPES.has(kind) ? bigLevels : levels;
      return partsAt(kind, variant, near ? pick[0] : pick[1]);
    };
    // nível exato do kit (0, 1 ou 2): usado pelo LOD por instância de `engine/lod-field.js`
    const partsAt = (kind, variant, lv) => {
      const info = typeInfo[kind];
      if (!info) return null;
      const v = ((variant | 0) % info.variants + info.variants) % info.variants;
      const ck = `${kind}|${v}|${lv}`;
      if (cache.has(ck)) return cache.get(ck);
      let list = table.get(`${kind}|${v}|${lv}`);
      // um nível pode não existir para um tipo muito pequeno: cai para o mais próximo
      for (let d = 1; !list && d <= 2; d++) list = table.get(`${kind}|${v}|${Math.max(0, lv - d)}`) || table.get(`${kind}|${v}|${lv + d}`);
      if (!list) return null;
      // [geometria, material, projeta sombra, profundidade com vento, cor por instância]
      const out = list.map(([geo, matId]) => {
        const material = mats[matId] || mats.palette;
        const cast = matId !== 'grass' && kind !== 'grass' && kind !== 'flower';
        return [geo, material, cast, depth[matId] || null, true];
      });
      cache.set(ck, out);
      return out;
    };

    KIT.parts = partsFor;
    KIT.partsAt = partsAt;
    KIT.variants = (kind) => typeInfo[kind]?.variants ?? 0;
    KIT.has = (kind) => !!typeInfo[kind];
    KIT.collider = (kind, variant) => {
      const info = typeInfo[kind];
      if (!info) return null;
      const v = ((variant | 0) % info.variants + info.variants) % info.variants;
      return info.items[v]?.collider ?? null;
    };
    KIT.metrics = (kind, variant = 0) => {
      const info = typeInfo[kind];
      if (!info) return null;
      const v = ((variant | 0) % info.variants + info.variants) % info.variants;
      return info.items[v] || null;
    };
    // o tom próprio de cada tipo (a folhosa é o mesmo abeto, com verde mais quente)
    KIT.tint = (kind) => typeInfo[kind]?.tint || null;

    KIT_STATUS.loaded = true;
    KIT_STATUS.types = Object.keys(typeInfo).length;
    KIT_STATUS.tier = tier;
    KIT_STATUS.levels = { normal: levels, grandes: bigLevels };
    return KIT_STATUS;
  } catch (err) {
    KIT_STATUS.error = String(err && err.message ? err.message : err);
    KIT.parts = null; KIT.partsAt = null; KIT.collider = null; KIT.variants = null; KIT.has = null;
    console.warn('kit de natureza não carregou; usando os protótipos procedurais:', KIT_STATUS.error);
    return KIT_STATUS;
  }
}

export const kitSky = () => MANIFEST.sky || null;

// ---------------------------------------------------------------- ambiente do HDRI
// O HDRI de 250 MB do pacote virou um equirretangular de 256 × 128 em RGBE. Aqui ele é decodificado
// de volta para float e passa pelo PMREM: entra como luz de ambiente do período diurno, sem tocar
// no céu desenhado, no ciclo dia/noite nem na atmosfera da Turbulenta.
let envPromise = null;
export function loadKitEnvironment(renderer) {
  const sky = MANIFEST.sky;
  if (!sky || !renderer) return Promise.resolve(null);
  envPromise ||= (async () => {
    const img = new Image();
    await new Promise((resolve, reject) => {
      img.onload = resolve;
      img.onerror = () => reject(new Error('envmap do kit não pôde ser aberto'));
      img.src = skyEnvUrl;
    });
    const canvas = document.createElement('canvas');
    canvas.width = sky.width; canvas.height = sky.height;
    const ctx = canvas.getContext('2d', { willReadFrequently: true });
    ctx.drawImage(img, 0, 0);
    const src = ctx.getImageData(0, 0, sky.width, sky.height).data;
    const out = new Float32Array(sky.width * sky.height * 4);
    for (let i = 0, n = sky.width * sky.height; i < n; i++) {
      const e = src[i * 4 + 3];
      const f = e ? 2 ** (e - 128) / 255 : 0;
      out[i * 4] = src[i * 4] * f;
      out[i * 4 + 1] = src[i * 4 + 1] * f;
      out[i * 4 + 2] = src[i * 4 + 2] * f;
      out[i * 4 + 3] = 1;
    }
    const tex = new THREE.DataTexture(out, sky.width, sky.height, THREE.RGBAFormat, THREE.FloatType);
    tex.mapping = THREE.EquirectangularReflectionMapping;
    tex.minFilter = THREE.LinearFilter; tex.magFilter = THREE.LinearFilter;
    tex.generateMipmaps = false;
    tex.needsUpdate = true;
    const pmrem = new THREE.PMREMGenerator(renderer);
    const rt = pmrem.fromEquirectangular(tex);
    pmrem.dispose(); tex.dispose();
    return { texture: rt.texture, colors: sky.colors };
  })().catch((e) => { console.warn('ambiente do kit indisponível:', e.message); return null; });
  return envPromise;
}
