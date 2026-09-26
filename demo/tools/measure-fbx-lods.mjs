import { createReadStream } from 'node:fs';
import { createGunzip } from 'node:zlib';
import { writeFileSync, mkdirSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { tmpdir } from 'node:os';
import { NodeIO } from '@gltf-transform/core';
import { KHRONOS_EXTENSIONS, EXTMeshoptCompression } from '@gltf-transform/extensions';
import { MeshoptDecoder } from 'meshoptimizer';

const TOOLS = dirname(fileURLToPath(import.meta.url));
const ROOT = dirname(dirname(TOOLS));
const PKG = join(ROOT, 'modelos 3d animados', 'Ultimate Nature Starter.unitypackage');
const FBX2GLTF = join(TOOLS, 'node_modules/fbx2gltf/bin/Linux/FBX2glTF');
const TMP = join(tmpdir(), 'measure-fbx-lods');

async function main() {
  await MeshoptDecoder.ready;
  rmSync(TMP, { recursive: true, force: true });
  mkdirSync(TMP, { recursive: true });

  const chunks = [];
  await new Promise((res, rej) => {
    createReadStream(PKG).pipe(createGunzip()).on('data', c => chunks.push(c)).on('end', res).on('error', rej);
  });
  const tar = Buffer.concat(chunks);
  const entries = new Map();
  let p = 0;
  while (p + 512 <= tar.length) {
    const name = tar.toString('utf8', p, p + 100).replace(/\0.*$/, '');
    if (!name) { p += 512; continue; }
    const sizeField = tar.toString('utf8', p + 124, p + 136).replace(/\0.*$/, '').trim();
    const size = parseInt(sizeField, 8) || 0;
    const type = tar.toString('utf8', p + 156, p + 157);
    p += 512;
    if (type === '0' || type === '\0') {
      const body = tar.subarray(p, p + size);
      const parts = name.split('/');
      if (parts.length >= 2) {
        const guid = parts[0] === '.' ? parts[1] : parts[0];
        const kind = parts[parts.length - 1];
        const e = entries.get(guid) || {};
        if (kind === 'pathname') e.pathname = body.toString('utf8').split('\n')[0].trim();
        else if (kind === 'asset') e.asset = Buffer.from(body);
        entries.set(guid, e);
      }
    }
    p += Math.ceil(size / 512) * 512;
  }
  const fbxFiles = entries;

  const io = new NodeIO().registerExtensions([...KHRONOS_EXTENSIONS, EXTMeshoptCompression]).registerDependencies({ 'meshopt.decoder': MeshoptDecoder });

  console.log('--- ALL 27 FBX LODs & TRIANGLES & BOUNDS ---');
  const results = [];
  for (const [guid, item] of fbxFiles) {
    if (!item.pathname || !item.pathname.endsWith('.fbx')) continue;
    const base = item.pathname.split('/').pop().replace('.fbx', '');
    const fbxPath = join(TMP, `${base}.fbx`);
    writeFileSync(fbxPath, item.asset);
    const glbBase = join(TMP, base);
    execFileSync(FBX2GLTF, ['--binary', '--input', fbxPath, '--output', glbBase], { stdio: 'ignore' });
    const doc = await io.read(`${glbBase}.glb`);
    const lods = [];
    for (const node of doc.getRoot().listNodes()) {
      const m = node.getMesh();
      if (!m) continue;
      const name = node.getName() || '';
      let tris = 0;
      for (const prim of m.listPrimitives()) {
        const idx = prim.getIndices();
        tris += idx ? idx.getCount() / 3 : prim.getAttribute('POSITION').getCount() / 3;
      }
      // compute bounding box
      let minY = Infinity, maxY = -Infinity;
      for (const prim of m.listPrimitives()) {
        const pos = prim.getAttribute('POSITION');
        const v = [0, 0, 0];
        for (let i = 0; i < pos.getCount(); i++) {
          pos.getElement(i, v);
          if (v[1] < minY) minY = v[1];
          if (v[1] > maxY) maxY = v[1];
        }
      }
      lods.push({ name, tris, height: maxY - minY, minY, maxY });
    }
    lods.sort((a, b) => a.name.localeCompare(b.name));
    results.push({ base, lods });
    console.log(`\nModel: ${base}`);
    for (const l of lods) {
      console.log(`  ${l.name.padEnd(20)} tris=${String(l.tris).padStart(6)} height=${l.height.toFixed(3)}m (y: ${l.minY.toFixed(3)} to ${l.maxY.toFixed(3)})`);
    }
  }

  writeFileSync('tools/fbx-lods-summary.json', JSON.stringify(results, null, 2));
}

main().catch(console.error);
