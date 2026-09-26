// Impostores das árvores do kit: o nível mais distante da floresta.
//
// No boot, cada árvore é fotografada de lado em duas vistas ortográficas (frente e perfil) para um
// alvo de 512 × 512, guardando só cor e recorte. A árvore distante vira dois quads cruzados
// (4 triângulos) com essa imagem — no lugar de ~2.600 triângulos do nível mais simples do kit. É
// isso que deixa a floresta densa inteira visível até o horizonte sem rarear árvores.
//
// A foto é feita sem luz (cor do material e da textura); o quad recebe a luz da cena com a normal
// para cima, como o chão em volta, e o tom de cada árvore chega pela cor por instância.
import * as THREE from 'three';
import { KIT, levelParts } from '../engine/props.js';

const SIZE = 512;
const IMPOSTOR_TONE = 0.56;
export const IMPOSTOR_STATUS = { baked: [], ms: 0 };

function bakeOne(renderer, kind, variant) {
  const parts = levelParts(kind, variant, 1);
  if (!parts || !parts.length) return null;
  const box = new THREE.Box3();
  for (const [geo] of parts) { geo.computeBoundingBox(); box.union(geo.boundingBox); }
  const hw = Math.max(Math.abs(box.min.x), Math.abs(box.max.x), Math.abs(box.min.z), Math.abs(box.max.z)) * 1.04;
  const top = box.max.y * 1.02, bottom = Math.min(0, box.min.y);

  const scene = new THREE.Scene();
  let avg = new THREE.Color(0, 0, 0), nFol = 0;
  for (const [geo, material] of parts) {
    const m = new THREE.MeshBasicMaterial({
      map: material.map || null, color: material.color ? material.color.clone() : 0xffffff,
      alphaTest: Math.max(0.35, material.alphaTest || 0), side: THREE.DoubleSide,
    });
    if ((material.alphaTest || 0) > 0 && material.color) { avg.add(material.color); nFol++; }
    scene.add(new THREE.Mesh(geo, m));
  }
  if (nFol) avg.multiplyScalar(0.5 / nFol);

  const rt = new THREE.WebGLRenderTarget(SIZE, SIZE, {
    generateMipmaps: true, minFilter: THREE.LinearMipmapLinearFilter, magFilter: THREE.LinearFilter,
    depthBuffer: true, colorSpace: THREE.LinearSRGBColorSpace,
  });
  const cam = new THREE.OrthographicCamera(-hw, hw, top, bottom, 0.1, hw * 4 + 10);
  const prevTarget = renderer.getRenderTarget();
  const prevColor = renderer.getClearColor(new THREE.Color()), prevAlpha = renderer.getClearAlpha();
  const prevAuto = renderer.autoClear, prevShadow = renderer.shadowMap.enabled;
  renderer.shadowMap.enabled = false;
  renderer.autoClear = false;
  // fundo com a cor média da folhagem e alfa 0: os mipmaps não escurecem a borda da copa
  renderer.setClearColor(avg, 0);
  rt.viewport.set(0, 0, SIZE, SIZE); rt.scissor.set(0, 0, SIZE, SIZE); rt.scissorTest = false;
  renderer.setRenderTarget(rt);
  renderer.clear(true, true, true);
  // vista de frente (olhando para -z) na metade esquerda; de perfil (olhando para -x) na direita
  const views = [[0, new THREE.Vector3(0, 0, hw * 2 + 5)], [SIZE / 2, new THREE.Vector3(hw * 2 + 5, 0, 0)]];
  for (const [x, pos] of views) {
    cam.position.copy(pos);
    cam.lookAt(0, 0, 0);
    cam.updateMatrixWorld();
    rt.viewport.set(x, 0, SIZE / 2, SIZE);
    rt.scissor.set(x, 0, SIZE / 2, SIZE);
    rt.scissorTest = true;
    renderer.setRenderTarget(rt);
    renderer.render(scene, cam);
  }
  rt.scissorTest = false;
  renderer.setRenderTarget(prevTarget);
  renderer.setClearColor(prevColor, prevAlpha);
  renderer.autoClear = prevAuto;
  renderer.shadowMap.enabled = prevShadow;
  scene.traverse((o) => { if (o.isMesh) o.material.dispose(); });

  // dois quads cruzados; cada um usa uma metade da imagem
  const w = hw, h0 = bottom, h1 = top;
  const pos = [], uv = [], nor = [], idx = [];
  const quad = (ax, az, bx, bz, u0, u1) => {
    const b = pos.length / 3;
    pos.push(ax, h0, az, bx, h0, bz, bx, h1, bz, ax, h1, az);
    uv.push(u0, 0, u1, 0, u1, 1, u0, 1);
    for (let i = 0; i < 4; i++) nor.push(0, 1, 0);
    idx.push(b, b + 1, b + 2, b, b + 2, b + 3);
  };
  quad(-w, 0, w, 0, 0, 0.5);          // plano XY: foto de frente
  quad(0, w, 0, -w, 0.5, 1);          // plano ZY: foto de perfil
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  geo.setAttribute('uv', new THREE.Float32BufferAttribute(uv, 2));
  geo.setAttribute('normal', new THREE.Float32BufferAttribute(nor, 3));
  geo.setIndex(idx);
  geo.computeBoundingSphere();
  // a foto não tem a sombra que as próprias agulhas fazem umas nas outras: o tom é baixado para a
  // copa distante casar com a árvore de perto
  const mat = new THREE.MeshStandardMaterial({
    map: rt.texture, color: new THREE.Color(IMPOSTOR_TONE, IMPOSTOR_TONE, IMPOSTOR_TONE),
    alphaTest: 0.5, side: THREE.DoubleSide, roughness: 1, metalness: 0, envMapIntensity: 0.15,
  });
  // alfa de 0,5 para o recorte; `alphaTest > 0` também marca a peça como folhagem (cor da copa)
  return [[geo, mat, false, null, true]];
}

// Fotografa as árvores do kit e publica `KIT.impostor(tipo, variante)`. Sem renderer (testes) ou se
// algo falhar, o nível mais distante continua sendo o L2 do kit.
export function bakeImpostors(renderer, kinds = ['pine', 'broad', 'dead']) {
  if (!renderer || !KIT.partsAt) return IMPOSTOR_STATUS;
  const t0 = performance.now();
  const cache = new Map();
  try {
    for (const kind of kinds) {
      const nv = KIT.variants ? KIT.variants(kind) || 1 : 1;
      for (let v = 0; v < nv; v++) {
        const parts = bakeOne(renderer, kind, v);
        if (parts) { cache.set(`${kind}|${v}`, parts); IMPOSTOR_STATUS.baked.push(`${kind}#${v}`); }
      }
    }
    KIT.impostor = (kind, variant = 0) => cache.get(`${kind}|${variant | 0}`) || null;
  } catch (err) {
    console.warn('impostores indisponíveis; a floresta distante usa o nível L2:', err.message);
    KIT.impostor = null;
  }
  IMPOSTOR_STATUS.ms = Math.round(performance.now() - t0);
  return IMPOSTOR_STATUS;
}
