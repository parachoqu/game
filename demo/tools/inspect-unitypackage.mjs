import { createReadStream, existsSync } from 'node:fs';
import { createGunzip } from 'node:zlib';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const TOOLS = dirname(fileURLToPath(import.meta.url));
const ROOT = dirname(dirname(TOOLS));
const PKG = join(ROOT, 'modelos 3d animados', 'Ultimate Nature Starter.unitypackage');

async function main() {
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

  console.log(`Total entries: ${entries.size}`);
  const list = [];
  for (const [guid, e] of entries) {
    if (!e.pathname) continue;
    list.push({
      guid,
      pathname: e.pathname,
      hasAsset: !!e.asset,
      assetSize: e.asset ? e.asset.length : 0,
      hasMeta: !!e.meta,
    });
  }
  list.sort((a, b) => a.pathname.localeCompare(b.pathname));
  
  const extensions = {};
  for (const item of list) {
    const ext = item.pathname.includes('.') ? item.pathname.split('.').pop().toLowerCase() : '(dir)';
    extensions[ext] = (extensions[ext] || 0) + 1;
    console.log(`${item.guid.slice(0, 8)} [${ext.padEnd(8)}] ${item.pathname} (${item.assetSize} bytes)`);
  }
  console.log('\nExtensions summary:', JSON.stringify(extensions, null, 2));
}

main().catch(console.error);
