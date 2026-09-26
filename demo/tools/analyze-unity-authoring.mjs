import { createReadStream } from 'node:fs';
import { createGunzip } from 'node:zlib';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const TOOLS = dirname(fileURLToPath(import.meta.url));
const ROOT = dirname(dirname(TOOLS));
const PKG = join(ROOT, 'modelos 3d animados', 'Ultimate Nature Starter.unitypackage');

async function extractAll() {
  const chunks = [];
  await new Promise((resolve, reject) => {
    createReadStream(PKG).pipe(createGunzip())
      .on('data', (c) => chunks.push(c))
      .on('end', resolve).on('error', reject);
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
        const guid = parts[0], kind = parts[parts.length - 1];
        const e = entries.get(guid) || {};
        if (kind === 'pathname') e.pathname = body.toString('utf8').split('\n')[0].trim();
        else if (kind === 'asset') e.asset = Buffer.from(body);
        else if (kind === 'asset.meta') e.meta = body.toString('utf8');
        entries.set(guid, e);
      }
    }
    p += Math.ceil(size / 512) * 512;
  }
  return entries;
}

async function main() {
  const entries = await extractAll();
  const guidToPath = new Map();
  for (const [guid, e] of entries) {
    if (e.pathname) guidToPath.set(guid, e.pathname);
  }

  console.log('--- 10 MATERIAIS UNITY ---');
  const materials = [];
  for (const [guid, e] of entries) {
    if (e.pathname && e.pathname.endsWith('.mat')) {
      const text = e.asset ? e.asset.toString('utf8') : '';
      materials.push({ guid, path: e.pathname, text });
    }
  }
  materials.sort((a, b) => a.path.localeCompare(b.path));
  for (const m of materials) {
    console.log(`\nMaterial: ${m.path} (GUID: ${m.guid})`);
    // extract shader, textures, colors, floats
    const shaderMatch = m.text.match(/m_Shader:\s*\{fileID:\s*\d+,\s*guid:\s*([a-f0-9]+)/);
    const shader = shaderMatch ? guidToPath.get(shaderMatch[1]) || shaderMatch[1] : 'builtin';
    console.log(`  Shader: ${shader}`);
    
    // find tex envs
    const texMatches = [...m.text.matchAll(/-\s*([a-zA-Z0-9_]+):\s*\n\s*m_Texture:\s*\{fileID:\s*\d+,\s*guid:\s*([a-f0-9]+)/g)];
    for (const tm of texMatches) {
      console.log(`  Tex: ${tm[1]} -> ${guidToPath.get(tm[2]) || tm[2]}`);
    }

    // find colors
    const colMatches = [...m.text.matchAll(/-\s*([a-zA-Z0-9_]+):\s*\{r:\s*([\d\.-]+),\s*g:\s*([\d\.-]+),\s*b:\s*([\d\.-]+),\s*a:\s*([\d\.-]+)\}/g)];
    for (const cm of colMatches) {
      console.log(`  Color: ${cm[1]} = rgba(${cm[2]}, ${cm[3]}, ${cm[4]}, ${cm[5]})`);
    }

    // find key floats (roughness, smoothness, metallic, cutoff)
    const floatMatches = [...m.text.matchAll(/-\s*([a-zA-Z0-9_]+):\s*([\d\.-]+)/g)];
    for (const fm of floatMatches) {
      if (['_Smoothness', '_Glossiness', '_Metallic', '_Cutoff', '_Surface', '_Blend', '_Cull'].includes(fm[1])) {
        console.log(`  Float: ${fm[1]} = ${fm[2]}`);
      }
    }
  }

  console.log('\n--- 27 PREFABS UNITY (LODs & COLLIDERS) ---');
  const prefabs = [];
  for (const [guid, e] of entries) {
    if (e.pathname && e.pathname.endsWith('.prefab')) {
      const text = e.asset ? e.asset.toString('utf8') : '';
      prefabs.push({ guid, path: e.pathname, text });
    }
  }
  prefabs.sort((a, b) => a.path.localeCompare(b.path));
  
  let totalMeshColliders = 0;
  let totalCapsuleColliders = 0;

  for (const p of prefabs) {
    const baseName = p.path.split('/').pop().replace('.prefab', '');
    console.log(`\nPrefab: ${baseName}`);
    
    // Check colliders
    const hasMeshCol = p.text.includes('MeshCollider:');
    const hasCapCol = p.text.includes('CapsuleCollider:');
    if (hasMeshCol) {
      const count = (p.text.match(/MeshCollider:/g) || []).length;
      totalMeshColliders += count;
      console.log(`  Colliders: MeshCollider (count: ${count})`);
      // Find mesh ref
      const meshRefs = [...p.text.matchAll(/m_Mesh:\s*\{fileID:\s*(-?\d+),\s*guid:\s*([a-f0-9]+)/g)];
      for (const mr of meshRefs) {
        console.log(`    Mesh ref: fileID=${mr[1]}, guid=${mr[2]} (${guidToPath.get(mr[2])})`);
      }
    }
    if (hasCapCol) {
      const count = (p.text.match(/CapsuleCollider:/g) || []).length;
      totalCapsuleColliders += count;
      const r = p.text.match(/m_Radius:\s*([\d\.-]+)/);
      const h = p.text.match(/m_Height:\s*([\d\.-]+)/);
      const c = p.text.match(/m_Center:\s*\{x:\s*([\d\.-]+),\s*y:\s*([\d\.-]+),\s*z:\s*([\d\.-]+)\}/);
      console.log(`  Colliders: CapsuleCollider (r=${r ? r[1] : '?'}, h=${h ? h[1] : '?'}, center=[${c ? [c[1],c[2],c[3]].join(',') : '?'}])`);
    }
    if (!hasMeshCol && !hasCapCol) {
      console.log(`  Colliders: NONE`);
    }

    // Check LODGroup
    if (p.text.includes('LODGroup:')) {
      const lodHeights = [...p.text.matchAll(/screenRelativeTransitionHeight:\s*([\d\.-]+)/g)].map(m => parseFloat(m[1]));
      const fadeMode = p.text.match(/m_FadeMode:\s*(\d+)/);
      console.log(`  LODGroup: screenRelativeHeights=[${lodHeights.join(', ')}], fadeMode=${fadeMode ? fadeMode[1] : '?'}`);
    }
  }

  console.log(`\nTotal Colliders in Prefabs: MeshColliders=${totalMeshColliders}, CapsuleColliders=${totalMeshColliders + totalCapsuleColliders}`);

  // Dump detailed info for all prefabs
  const prefabDetails = {};
  for (const p of prefabs) {
    const baseName = p.path.split('/').pop().replace('.prefab', '');
    const lods = [];
    const lodIdx = p.text.indexOf('LODGroup:');
    let lodSection = '';
    if (lodIdx !== -1) {
      lodSection = p.text.slice(lodIdx, lodIdx + 4000);
      const heights = [...lodSection.matchAll(/screenRelativeHeight:\s*([\d\.-]+)/g)].map(m => parseFloat(m[1]));
      const fadeMode = (lodSection.match(/m_FadeMode:\s*(\d+)/) || [])[1];
      const animateCrossFade = (lodSection.match(/m_AnimateCrossFading:\s*(\d+)/) || [])[1];
      prefabDetails[baseName] = {
        path: p.path,
        screenRelativeHeights: heights,
        fadeMode,
        animateCrossFade,
      };
    }
  }
  
  import('node:fs').then(fs => {
    fs.writeFileSync('tools/authoring-summary.json', JSON.stringify({
      materials: materials.map(m => ({ guid: m.guid, path: m.path })),
      prefabs: prefabDetails,
    }, null, 2));
    console.log('Saved tools/authoring-summary.json');
  });
}

main().catch(console.error);
