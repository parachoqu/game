// Inspeção rápida das exportações do Blender e dos pacotes derivados:
//   node tools/inspect-world-sources.mjs <arquivo.glb> [...]
// Lista malhas, nós, materiais, texturas e contagem de triângulos.
import { NodeIO } from '@gltf-transform/core';
import { KHRONOS_EXTENSIONS, EXTMeshoptCompression } from '@gltf-transform/extensions';
import { statSync } from 'node:fs';
import { MeshoptDecoder, MeshoptEncoder } from 'meshoptimizer';

await MeshoptDecoder.ready;
const io = new NodeIO()
  .registerExtensions([...KHRONOS_EXTENSIONS, EXTMeshoptCompression])
  .registerDependencies({ 'meshopt.decoder': MeshoptDecoder, 'meshopt.encoder': MeshoptEncoder });
const files = process.argv.slice(2);
for (const f of files) {
  try {
    const doc = await io.read(f);
    const root = doc.getRoot();
    const meshes = root.listMeshes();
    let tris = 0, verts = 0;
    for (const m of meshes) for (const p of m.listPrimitives()) {
      const idx = p.getIndices();
      const pos = p.getAttribute('POSITION');
      tris += idx ? idx.getCount() / 3 : (pos ? pos.getCount() / 3 : 0);
      verts += pos ? pos.getCount() : 0;
    }
    const tex = root.listTextures();
    let texBytes = 0; for (const t of tex) texBytes += (t.getImage()?.byteLength || 0);
    const nodes = root.listNodes();
    console.log(`\n=== ${f} (${(statSync(f).size/1048576).toFixed(1)} MB) ===`);
    console.log(`  meshes=${meshes.length} nodes=${nodes.length} mats=${root.listMaterials().length} tex=${tex.length} texMB=${(texBytes/1048576).toFixed(1)} tris=${Math.round(tris)} verts=${verts}`);
    console.log(`  scenes: ${root.listScenes().map(s=>s.getName()).join(', ')}`);
    const names = nodes.slice(0, 40).map(n => `${n.getName()||'?'}${n.getMesh()?'*':''}`);
    console.log(`  nodes(40): ${names.join(' | ')}`);
    // top-level children
    const sc = root.listScenes()[0];
    if (sc) console.log(`  roots(${sc.listChildren().length}): ${sc.listChildren().slice(0,30).map(n=>n.getName()).join(' | ')}`);
    console.log(`  materials: ${root.listMaterials().slice(0,25).map(m=>m.getName()).join(', ')}`);
  } catch (e) { console.log(`\n=== ${f} ERRO: ${e.message}`); }
}
