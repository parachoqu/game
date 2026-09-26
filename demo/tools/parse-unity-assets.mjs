import { createReadStream } from 'node:fs';
import { createGunzip } from 'node:zlib';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const TOOLS = dirname(fileURLToPath(import.meta.url));
const ROOT = dirname(dirname(TOOLS));
const PKG = join(ROOT, 'modelos 3d animados', 'Ultimate Nature Starter.unitypackage');

export async function parseUnityAssets() {
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
        const guid = parts[0] === '.' ? parts[1] : parts[0];
        const kind = parts[parts.length - 1];
        const e = entries.get(guid) || {};
        if (kind === 'pathname') e.pathname = body.toString('utf8').split('\n')[0].trim();
        else if (kind === 'asset') e.asset = Buffer.from(body);
        else if (kind === 'asset.meta') e.meta = body.toString('utf8');
        entries.set(guid, e);
      }
    }
    p += Math.ceil(size / 512) * 512;
  }

  const guidToPath = new Map();
  for (const [guid, e] of entries) {
    if (e.pathname) guidToPath.set(guid, e.pathname);
  }

  // Parse materials
  const materials = {};
  for (const [guid, e] of entries) {
    if (!e.pathname || !e.pathname.endsWith('.mat')) continue;
    const txt = e.asset ? e.asset.toString('utf8') : '';
    const base = e.pathname.split('/').pop().replace('.mat', '');
    
    // Extract properties
    const colorMatch = txt.match(/-\s*(?:_Color|_BaseColor):\s*\{r:\s*([\d\.-]+),\s*g:\s*([\d\.-]+),\s*b:\s*([\d\.-]+),\s*a:\s*([\d\.-]+)\}/);
    const smoothnessMatch = txt.match(/-\s*_Smoothness:\s*([\d\.-]+)/) || txt.match(/-\s*_Glossiness:\s*([\d\.-]+)/);
    const metallicMatch = txt.match(/-\s*_Metallic:\s*([\d\.-]+)/);
    const cutoffMatch = txt.match(/-\s*_Cutoff:\s*([\d\.-]+)/);
    const cullMatch = txt.match(/-\s*_Cull:\s*(\d+)/);
    const texMatch = txt.match(/m_Texture:\s*\{fileID:\s*\d+,\s*guid:\s*([a-f0-9]+)/);

    materials[base] = {
      guid,
      path: e.pathname,
      color: colorMatch ? { r: +colorMatch[1], g: +colorMatch[2], b: +colorMatch[3], a: +colorMatch[4] } : null,
      smoothness: smoothnessMatch ? +smoothnessMatch[1] : 0.5,
      roughness: smoothnessMatch ? Math.round((1 - +smoothnessMatch[1]) * 1000) / 1000 : 0.5,
      metallic: metallicMatch ? +metallicMatch[1] : 0,
      cutoff: cutoffMatch ? +cutoffMatch[1] : null,
      doubleSided: cullMatch ? cullMatch[1] === '0' : false,
      textureFile: texMatch ? (guidToPath.get(texMatch[1]) || '').split('/').pop() : null,
    };
  }

  // Parse prefabs
  const prefabs = {};
  for (const [guid, e] of entries) {
    if (!e.pathname || !e.pathname.endsWith('.prefab')) continue;
    const txt = e.asset ? e.asset.toString('utf8') : '';
    const base = e.pathname.split('/').pop().replace('.prefab', '');

    // LODGroup
    let lods = null;
    const lodIdx = txt.indexOf('LODGroup:');
    if (lodIdx !== -1) {
      const section = txt.slice(lodIdx, lodIdx + 4000);
      const heights = [...section.matchAll(/screenRelativeHeight:\s*([\d\.-]+)/g)].map(m => +m[1]);
      const fadeMode = (section.match(/m_FadeMode:\s*(\d+)/) || [])[1];
      const animateCrossFade = (section.match(/m_AnimateCrossFading:\s*(\d+)/) || [])[1];
      lods = {
        screenRelativeHeights: heights,
        fadeMode: +fadeMode || 0,
        crossfade: animateCrossFade === '1',
      };
    }

    // Capsule Collider
    let capsule = null;
    const capIdx = txt.indexOf('CapsuleCollider:');
    if (capIdx !== -1) {
      const section = txt.slice(capIdx, capIdx + 1000);
      const r = section.match(/m_Radius:\s*([\d\.-]+)/);
      const h = section.match(/m_Height:\s*([\d\.-]+)/);
      const c = section.match(/m_Center:\s*\{x:\s*([\d\.-]+),\s*y:\s*([\d\.-]+),\s*z:\s*([\d\.-]+)\}/);
      const dir = section.match(/m_Direction:\s*(\d+)/);
      capsule = {
        radius: r ? +r[1] : 0,
        height: h ? +h[1] : 0,
        center: c ? [+c[1], +c[2], +c[3]] : [0, 0, 0],
        direction: dir ? +dir[1] : 1, // 0=X, 1=Y, 2=Z
      };
    }

    // Mesh Colliders
    const meshColliders = (txt.match(/MeshCollider:/g) || []).length;

    prefabs[base] = {
      guid,
      path: e.pathname,
      lods,
      capsule,
      meshColliders,
    };
  }

  return { entries, materials, prefabs };
}

if (process.argv[1] && process.argv[1].endsWith('parse-unity-assets.mjs')) {
  parseUnityAssets().then(({ materials, prefabs }) => {
    console.log('Materials:', Object.keys(materials).length);
    console.log(JSON.stringify(materials, null, 2));
    console.log('Prefabs:', Object.keys(prefabs).length);
    console.log(JSON.stringify(prefabs, null, 2));
  }).catch(console.error);
}
