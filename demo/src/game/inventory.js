// Inventário: pilhas de materiais + equipamentos com condição. Peso em três estados (cap. 06).
import { ITEMS, GEAR_KINDS } from '../config.js';
import { G } from '../state.js';

let uidSeq = 1;
export const BASE_CAP = 30;
export const OVER_LIMIT = 1.3;     // acima de 130% nada entra na bolsa

export const isGear = (id) => GEAR_KINDS.has(ITEMS[id].kind);
export function makeGear(id, cond = 100) { return { id, uid: uidSeq++, cond }; }
export function setUidSeq(n) { uidSeq = Math.max(uidSeq, n); }

export function count(list, id) {
  let n = 0; for (const e of list) if (e.id === id) n += e.qty || 1; return n;
}
export function weightOf(list) {
  let w = 0; for (const e of list) w += ITEMS[e.id].w * (e.qty || 1); return w;
}
export function addTo(list, id, qty = 1, cond) {
  if (isGear(id)) { for (let i = 0; i < qty; i++) list.push(makeGear(id, cond ?? 100)); return; }
  const e = list.find((x) => x.id === id && !x.uid);
  if (e) e.qty += qty; else list.push({ id, qty });
}
export function addEntry(list, entry) {
  if (entry.uid) list.push(entry); else addTo(list, entry.id, entry.qty);
}
export function removeFrom(list, id, qty = 1) {
  let left = qty;
  for (let i = list.length - 1; i >= 0 && left > 0; i--) {
    const e = list[i]; if (e.id !== id) continue;
    if (e.uid) { list.splice(i, 1); left--; }
    else { const take = Math.min(e.qty, left); e.qty -= take; left -= take; if (e.qty <= 0) list.splice(i, 1); }
  }
  return qty - left;
}

export function capacity(P) { return BASE_CAP + (P.equip.bag ? ITEMS.bolsa.cap : 0); }
export function saddleCap(P) { return P.equip.mount ? ITEMS.montaria.cap : 0; }
export function loadRatio(P) { return weightOf(P.inv) / capacity(P); }
export function loadState(P) {
  const r = loadRatio(P);
  return r <= 1 ? 'normal' : r <= OVER_LIMIT ? 'sobrecarga' : 'limite';
}
export function saddleReachable(P) {
  const m = G.mount;
  if (!P.equip.mount || !m || !m.present) return false;
  return P.mounted || Math.hypot(m.pos.x - P.pos.x, m.pos.z - P.pos.z) < 7;
}

// Recebe itens coletados/comprados: bolsa → alforjes → sobrecarga. Retorna quantos entraram.
export function receive(P, id, qty = 1) {
  const w = ITEMS[id].w;
  let got = 0;
  for (let i = 0; i < qty; i++) {
    const bagW = weightOf(P.inv), cap = capacity(P);
    if (bagW + w <= cap) { addTo(P.inv, id, 1); got++; continue; }
    if (saddleReachable(P) && weightOf(P.saddle) + w <= saddleCap(P)) { addTo(P.saddle, id, 1); got++; continue; }
    if (bagW + w <= cap * OVER_LIMIT) { addTo(P.inv, id, 1); got++; continue; }
    break;
  }
  return got;
}

// Materiais disponíveis para fabricação: bolsa + (opcional) armazém
export function haveAll(P, req, useStorage) {
  for (const [id, n] of Object.entries(req)) {
    if (count(P.inv, id) + (useStorage ? count(P.storage, id) : 0) < n) return false;
  }
  return true;
}
export function consume(P, req, useStorage) {
  for (const [id, n] of Object.entries(req)) {
    const got = removeFrom(P.inv, id, n);
    if (got < n && useStorage) removeFrom(P.storage, id, n - got);
  }
}

export function equippedList(P) {
  return ['weapon', 'armor', 'bag', 'mount'].map((s) => P.equip[s]).filter(Boolean);
}
export function armorHp(P) { const a = P.equip.armor; return a && a.cond > 0 ? ITEMS[a.id].hp : 0; }
export function armorSpeed(P) { const a = P.equip.armor; return a ? ITEMS[a.id].speed : 1; }
export function wear(gear, amt) { if (gear) gear.cond = Math.max(0, gear.cond - amt); }
