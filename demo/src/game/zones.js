// Matriz de derrota por zona (cap. 07–08): o que fica, o que é saqueável, o que é destruído.
import * as THREE from 'three';
import { G, emit, stat, clamp } from '../state.js';
import { ITEMS, ZONES } from '../config.js';
import { groundHeight } from '../engine/terrain.js';
import { mat } from '../engine/props.js';
import { addEntry, wear, equippedList } from './inventory.js';
import { zoneAt, placeAt, LOC, isTurbulentSpace } from './layout.js';
import { first, log } from './book.js';

const sackGeo = new THREE.IcosahedronGeometry(0.55, 0);
export function createLootBag(x, z, items, label = 'carga', opts = {}) {
  const g = new THREE.Group();
  const s = new THREE.Mesh(sackGeo, mat(opts.own ? '#8a5a2a' : '#6a5040', null, 'tecido')); s.scale.set(1, 0.8, 1); s.position.y = 0.4; s.castShadow = true; g.add(s);
  const tie = new THREE.Mesh(new THREE.ConeGeometry(0.2, 0.35, 5), mat('#c9a25a', null, 'tecido')); tie.position.y = 0.95; g.add(tie);
  const beacon = new THREE.Mesh(new THREE.CylinderGeometry(0.06, 0.06, 6, 5), new THREE.MeshBasicMaterial({ color: opts.own ? '#f2c86a' : '#d9c7a0', transparent: true, opacity: 0.45, depthWrite: false }));
  beacon.position.y = 3.5; g.add(beacon);
  g.position.set(x, groundHeight(x, z), z);
  G.scene.add(g);
  const bag = {
    x, z, items: items.map((e) => ({ ...e })), label, own: !!opts.own, mesh: g, claimed: null,
    expires: G.time + (opts.ttl || 300), place: placeAt(x, z), zone: zoneAt(x, z),
    remove() { G.scene.remove(g); const i = G.lootBags.indexOf(bag); if (i >= 0) G.lootBags.splice(i, 1); },
  };
  G.lootBags.push(bag);
  return bag;
}
export function updateLootBags() {
  for (let i = G.lootBags.length - 1; i >= 0; i--) {
    const b = G.lootBags[i];
    if (!b.items.length || G.time > b.expires) b.remove();
  }
}

const itemLine = (e) => ({ id: e.id, name: ITEMS[e.id].name, qty: e.qty || 1 });

export function handleDeath(P, src) {
  const zone = isTurbulentSpace(P.pos.x) ? 'turbulenta' : zoneAt(P.pos.x, P.pos.z);
  const place = placeAt(P.pos.x, P.pos.z);
  const report = { zone, place, cause: src?.name || 'ferimentos', lost: [], destroyed: [], kept: [], bag: null };
  const kept = ['Conhecimentos e receitas', 'Capítulos do Livro', `Moedas (${P.coins}, saldo protegido)`, 'Itens no armazém'];

  if (zone === 'protegida') {
    for (const g of equippedList(P)) if (g.id !== 'montaria') wear(g, 15);
    report.kept = [...kept, 'Carga transportada', 'Conjunto equipado (−15% de condição)'];
  } else if (zone === 'fronteira') {
    const items = [...P.inv.splice(0), ...P.saddle.splice(0)];
    for (const g of equippedList(P)) if (g.id !== 'montaria') wear(g, 15);
    report.lost = items.map(itemLine);
    if (items.length) report.bag = createLootBag(P.pos.x, P.pos.z, items, 'sua carga', { own: true, ttl: 300 });
    report.kept = [...kept, 'Conjunto equipado (−15% de condição)', P.equip.mount ? 'Item de montaria (volta ao estábulo)' : null].filter(Boolean);
  } else {
    // Full Loot e Turbulenta: tudo o que é levado entra na regra; uma parte é destruída
    const items = [...P.inv.splice(0), ...P.saddle.splice(0)];
    for (const s of ['weapon', 'armor', 'bag', 'mount']) if (P.equip[s]) { items.push(P.equip[s]); P.equip[s] = null; }
    const survived = [];
    for (const e of items) {
      if (e.uid) {
        if (Math.random() < 0.3) report.destroyed.push(itemLine(e)); else survived.push(e);
      } else {
        const d = Math.floor(e.qty * 0.3);
        if (d > 0) report.destroyed.push({ ...itemLine(e), qty: d });
        if (e.qty - d > 0) survived.push({ id: e.id, qty: e.qty - d });
      }
    }
    report.lost = survived.map(itemLine);
    if (survived.length) report.bag = createLootBag(P.pos.x, P.pos.z, survived, 'sua carga', { own: true, ttl: zone === 'turbulenta' ? 9999 : 300 });
    report.kept = kept;
    P.model.setWeapon(null);
  }
  if (G.mount && G.mount.present) G.mount.despawn();

  // Livro: a derrota também é parte da trajetória
  const n = stat('deaths');
  first('primeira-derrota', 'feitos', 'risco', 'Primeira derrota', `Caiu em ${place}, numa área ${ZONES[zone].name}. O Livro guarda também os recomeços.`);
  const lostTxt = report.lost.length || report.destroyed.length
    ? `Deixou para trás ${report.lost.reduce((a, b) => a + b.qty, 0)} item(ns)${report.destroyed.length ? `; ${report.destroyed.reduce((a, b) => a + b.qty, 0)} foram destruídos` : ''}.`
    : 'Nada foi saqueado.';
  if (zone !== 'turbulenta') log('historia', 'risco', `Derrota em ${place}`, `Caiu diante de ${report.cause.toLowerCase()} (${ZONES[zone].name}). ${lostTxt}`, 'morte' + n);
  emit('player-died', report);
  return report;
}

// Retorno ao abrigo do Vale
// Abrigo: a praça do mercado do entreposto sul, a âncora segura do mundo.
export const SHELTER = { x: LOC.mercado.x, z: LOC.mercado.z + 12 };
export function respawn(P) {
  P.pos.set(SHELTER.x, groundHeight(SHELTER.x, SHELTER.z), SHELTER.z);
  P.maxHp = 100 + (P.equip.armor ? ITEMS[P.equip.armor.id].hp : 0);
  P.hp = P.maxHp; P.vigor = P.maxVigor; P.state = 'free'; P.stateT = 0; P.mounted = false; P.invuln = 1.5;
  P.lastCombat = -99;
  P.model.setWeapon(P.equip.weapon && P.equip.weapon.cond > 0 ? ITEMS[P.equip.weapon.id].family : null);
  emit('player-respawn');
}
export const conditionLabel = (c) => (c <= 0 ? 'inutilizado' : c < 35 ? 'gasto' : c < 70 ? 'usado' : 'bom');
export const condPct = (c) => clamp(Math.round(c), 0, 100) + '%';
