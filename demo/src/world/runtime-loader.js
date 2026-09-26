// Carregador genérico de cenário a partir de GLB embutidos nos bytes do próprio HTML.
//
// Reaproveita a mesma infraestrutura offline dos personagens (`engine/models.js`): GLTFLoader local,
// MeshoptDecoder local e decodificação direta das imagens com `createImageBitmap`, sem `fetch`,
// sem CDN e sem URL `blob:` — que a política de segurança do modo Artifact bloqueia.
//
// Diferente dos personagens, o cenário NÃO passa por `mold()`: centralizar, redimensionar e trocar
// materiais destruiria a escala métrica, a posição e os materiais PBR vindos do Blender.
import * as THREE from 'three';
import { GLTFLoader } from '../../vendor/three/addons/loaders/GLTFLoader.js';
import { MeshoptDecoder } from '../../vendor/three/addons/libs/meshopt_decoder.module.js';

function directImages(parser) {
  parser.loadImageSource = function (sourceIndex) {
    if (this.sourceCache[sourceIndex] !== undefined) return this.sourceCache[sourceIndex].then((t) => t.clone());
    const def = this.json.images[sourceIndex];
    const p = this.getDependency('bufferView', def.bufferView)
      .then((view) => createImageBitmap(new Blob([view], { type: def.mimeType }), { imageOrientation: 'none', premultiplyAlpha: 'none', colorSpaceConversion: 'none' }))
      .then((bmp) => { const t = new THREE.Texture(bmp); t.needsUpdate = true; t.userData.mimeType = def.mimeType; return t; });
    this.sourceCache[sourceIndex] = p;
    return p;
  };
  return { name: 'direct_images' };
}

let ready = null;
export function decoderReady() {
  ready ||= MeshoptDecoder.ready;
  return ready;
}

// Analisa os bytes de um GLB e devolve o glTF cru, sem tocar em escala, posição ou material.
export async function parseSceneGLB(bin) {
  await decoderReady();
  const loader = new GLTFLoader().setMeshoptDecoder(MeshoptDecoder).register(directImages);
  const buf = bin.buffer.slice(bin.byteOffset, bin.byteOffset + bin.byteLength);
  return loader.parseAsync(buf, '');
}

// Carrega um GLB de cenário aplicando um resolvedor de materiais por nome.
//   resolveMaterial(nomeDoMaterial, mesh) → THREE.Material | null (null mantém o material original)
//   onNode(nome, objeto)                  → gancho por nó de primeiro nível
export async function loadWorldGLB(bin, { resolveMaterial, onNode, cast = true, receive = true } = {}) {
  const gltf = await parseSceneGLB(bin);
  const root = gltf.scene;
  root.updateMatrixWorld(true);
  const nodes = [];
  for (const child of [...root.children]) {
    child.traverse((o) => {
      if (!o.isMesh) return;
      o.castShadow = cast;
      o.receiveShadow = receive;
      o.frustumCulled = true;
      if (resolveMaterial) {
        const mats = Array.isArray(o.material) ? o.material : [o.material];
        const next = mats.map((m) => resolveMaterial(m?.name || '', o, m) || m);
        o.material = Array.isArray(o.material) ? next : next[0];
      }
      if (!o.geometry.boundingSphere) o.geometry.computeBoundingSphere();
    });
    nodes.push(child);
    onNode?.(child.name, child);
  }
  return { root, nodes, gltf };
}
