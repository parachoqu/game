// Boot da demo: mundo 3D, telas de título e criação, laço principal e atalhos.
import * as THREE from 'three';
import { G, on, clock } from './state.js';
import { ORIGINS, STARTS, ITEMS, ZONES } from './config.js';
import { createRenderer } from './engine/renderer.js';
import { createSky, applyDaylight, setKitEnvironment, calibrateDaylight } from './engine/sky.js';
import { initInput, endFrame, hit, down, released, input } from './engine/input.js';
import { initKeyguard, protectKeyboard } from './engine/keyguard.js';
import { initAudio, setAmbience, sfx } from './engine/audio.js';
import { initFx, updateFx } from './engine/fx.js';
import { groundHeight } from './engine/terrain.js';
import { loadWorldScene, WORLD_STATUS } from './world/index.js';
import { LOD_FIELDS, activeCount, updateLODFields } from './engine/lod-field.js';
import { COVER, updateGroundCover } from './world/ground-cover.js';
import { loadKitEnvironment } from './world/nature-kit.js';
import { updateWorldDetail } from './world/scene-world.js';
import { generateTextures, TEX } from './engine/textures.js';
import { loadModels, MODELS } from './engine/models.js';
import * as CH from './engine/characters.js';
import { U } from './engine/materials.js';
import { initialQuality, applyQuality, sampleFrame, updateDetail, withVegetationVisible, LEVELS, QUALITY } from './engine/quality.js';
import { buildWorld, initCanteiroGuard } from './game/world.js';
import { createPlayer, updatePlayer, updateCamera, drinkPotion, toggleShoulder, setCameraPreset } from './game/player.js';
import { updatePointer } from './game/pointer.js';
import { initMount, mountAction, updateMount } from './game/mount.js';
import { spawnEnemy, updateEnemies, updateGroups } from './game/enemies.js';
import { updateProjectiles, hurtPlayer, hurtEnemy } from './game/combat.js';
import { initCamp, updateCamp, CAMP } from './game/camp.js';
import { initEvent, updateEvent, EVT, contribute } from './game/event.js';
import { SKY, updateStars, endObserve } from './game/stars.js';
import { TS, initTurbulent, updateTurbulent, forcePortalNear, onTurbulentDeath } from './game/turbulent.js';
import { updateMarkets } from './game/economy.js';
import { createLootBag, updateLootBags } from './game/zones.js';
import { updateNpcs } from './game/npcs.js';
import { updateInteract } from './game/interact.js';
import { updateDiscovery } from './game/discovery.js';
import { first } from './game/book.js';
import { receive } from './game/inventory.js';
import { hasSave, readSave, saveGame, applySave, clearSave } from './game/save.js';
import { isTurbulentSpace, LOC } from './game/layout.js';
import { initHud, updateHud, showHud, showZone, toast, toggleMinimap } from './ui/hud.js';
import { buildBaseMap } from './ui/map.js';
import { initPanels, closePanel, panelOpen, deathPanel, fourQuestionsPanel, intentsPanel, pausePanel } from './ui/panels.js';
import { initBook, closeBook, bookOpen } from './ui/bookui.js';
import { initMenu, toggleMenu, updateMenu } from './ui/menu.js';

const $ = (id) => document.getElementById(id);
const ctx = createRenderer();
G.three = ctx.renderer; G.scene = ctx.scene; G.camera = ctx.camera;
G.cam = { yaw: 0, pitch: 0.67, dist: 14, mode: 'follow', look: new THREE.Vector3(), skyDir: null };
let mode = 'loading', last = performance.now(), saveT = 0, pausedFrames = 0;
let titleStar = null, titleT = 0;
const titlePos = new THREE.Vector3();
// Tela de título: acima do entreposto sul, olhando para o norte (a Passagem no horizonte).
const TITLE_VIEW = new THREE.Vector3(LOC.mercado.x - 30, 0, LOC.mercado.z + 60);
const TITLE_AZIMUTH = 350;

initInput($('game'));
initKeyguard({
  isPlaying: () => mode === 'game',
  onExit: () => toast('Fora da tela cheia, Ctrl+W volta a fechar a aba. Para voltar: Esc → Voltar à tela cheia.', 'warn', 7000),
});
initFx(ctx.scene, ctx.camera);
initHud(); initPanels(); initBook(); initMenu();

// barra de ações clicável: poção e montaria
$('cam-north').addEventListener('mousedown', (e) => e.preventDefault());
$('mini-size').addEventListener('mousedown', (e) => e.preventDefault());
$('mini-size').addEventListener('click', () => toggleMinimap());
$('cam-north').addEventListener('click', () => { G.cam.resetting = true; });
for (const [id, act] of [['ab-pot', drinkPotion], ['ab-mount', mountAction]]) {
  $(id).addEventListener('mousedown', (e) => e.preventDefault());   // não rouba o foco (Espaço segue pulando)
  $(id).addEventListener('click', () => { if (G.player && !G.uiOpen) act(G.player); });
}

// Constrói o mundo em etapas depois do primeiro desenho da tela de título (texturas → terreno → construções)
setTimeout(boot, 30);
async function boot() {
  const b = $('btn-new');
  const fill = $('boot-fill'), text = $('boot-text');
  // fases macro com peso relativo (a barra soma até 1 mesmo com sub-etapas internas de duração variável)
  const PHASES = [['texturas', 0.28], ['modelos', 0.18], ['mundo', 0.26], ['construções e floresta', 0.14], ['sombreadores', 0.14]];
  // lista das fases e marcas na barra, nas fronteiras entre elas
  $('boot-phases').innerHTML = PHASES.map(([n]) => `<li>${n}</li>`).join('');
  const items = [...$('boot-phases').children];
  let acc = 0;
  $('boot-track').insertAdjacentHTML('beforeend', PHASES.slice(0, -1).map(([, w]) => `<em class="mk" style="left:${((acc += w) * 100).toFixed(1)}%"></em>`).join(''));
  // enquanto monta, a tela ensina as regras das zonas (o texto é o mesmo que a faixa de zona mostra em jogo)
  $('boot-lore').innerHTML = Object.values(ZONES)
    .map((z, i) => `<p style="--i:${i};--lc:${z.color}"><b>${z.mark} Zona ${z.name}</b>${z.rule}</p>`).join('');
  let base = 0, subs = 0, cur = -1;
  const report = (label, sub) => {
    const at = PHASES.findIndex(([n]) => n === label);
    if (at !== cur) { cur = at; subs = 0; }
    if (sub) subs++;
    // dentro da fase a barra avança por aproximação: cada sub-etapa cobre parte do que falta,
    // então ela se mexe sem prometer mais do que já foi feito nem passar da fase seguinte
    const creep = at >= 0 ? PHASES[at][1] * (1 - Math.pow(0.86, subs)) * 0.92 : 0;
    text.textContent = sub ? `${label} — ${sub}` : `${label}…`;
    fill.style.width = `${Math.min(100, Math.round((base + creep) * 100))}%`;
    for (let i = 0; i < items.length; i++) items[i].className = i < at ? 'done' : i === at ? 'now' : '';
  };
  const step = (t) => new Promise((r) => { report(t); setTimeout(r, 0); });
  report(PHASES[0][0]);
  await generateTextures((t) => report(PHASES[0][0], t));
  base += PHASES[0][1];
  report(PHASES[1][0]);
  await loadModels((t) => report(PHASES[1][0], t));
  base += PHASES[1][1];
  await step(PHASES[2][0]);
  // mundo autoral: manifesto → relevo e colisores → terreno → construções → vegetação → Turbulenta
  const level = initialQuality();
  await loadWorldScene(ctx.scene, (t) => report(PHASES[2][0], t), {
    density: LEVELS[level].vegetation ?? 1, debug: G.debug, tier: level, renderer: ctx.renderer,
  });
  TITLE_VIEW.y = groundHeight(TITLE_VIEW.x, TITLE_VIEW.z) + 26;
  // luz de ambiente e paleta do dia vindas do HDRI reduzido do kit de natureza
  report(PHASES[2][0], 'luz do ambiente');
  const kitEnv = await loadKitEnvironment(ctx.renderer);
  if (kitEnv) { setKitEnvironment(kitEnv); calibrateDaylight(kitEnv.colors); }
  G.sky = createSky(ctx.scene, ctx.renderer);
  ctx.sky = G.sky;
  base += PHASES[2][1];
  await step(PHASES[3][0]);
  buildWorld();
  initTurbulent();
  report(PHASES[3][0], 'mapa da região');
  buildBaseMap();
  applyQuality(ctx, level);
  base += PHASES[3][1];
  await step(PHASES[4][0]);
  // com a luz do título (e o mapa de ambiente dela) e sem ambiente, como na Região Turbulenta
  const t0 = performance.now();
  applyDaylight(ctx, 23.2, titlePos.copy(TITLE_VIEW), false);
  // a vegetação nasce oculta (nível por instância, cobertura por célula): compila com tudo à vista
  await withVegetationVisible(titlePos, async () => {
    await ctx.precompile();
    const env = ctx.scene.environment;
    ctx.scene.environment = null; await ctx.precompile(); ctx.scene.environment = env;
  });
  G.bootCompileMs = Math.round(performance.now() - t0);
  base += PHASES[4][1];
  fill.style.width = '100%';
  for (const li of items) li.className = 'done';
  titleStar = G.sky.titleStar;
  mode = 'title';
  b.disabled = false; b.textContent = 'Nova trajetória';
  $('screen-loading').hidden = true;
  $('screen-title').hidden = false;
  if (hasSave()) $('btn-continue').hidden = false;
}

// ---------- telas ----------
const NAMES = ['Maren', 'Ilo', 'Tessaly', 'Ravi', 'Oriane', 'Kael', 'Nadja', 'Bento', 'Suri', 'Aurel'];
const CHAR_MODELS = {
  paladina: { name: 'Heroína (.blend)', desc: 'Personagem feminina realista detalhada do modelo .blend.' },
  kachujin: { name: 'Kachujin', desc: 'Guerreiro clássico do Mixamo.' },
  eve: { name: 'Eve', desc: 'Batedora clássica do Mixamo.' },
};
function buildCreate() {
  $('c-name').value = NAMES[Math.floor(Math.random() * NAMES.length)];
  $('c-models').innerHTML = Object.entries(CHAR_MODELS).map(([id, m], i) => `<label class="card"><input type="radio" name="model" value="${id}" ${i === 0 ? 'checked' : ''}><b>${m.name}</b><span>${m.desc}</span></label>`).join('');
  $('c-origins').innerHTML = Object.entries(ORIGINS).map(([id, o], i) => `<label class="card"><input type="radio" name="origin" value="${id}" ${i === 0 ? 'checked' : ''}><span class="sw" style="background:linear-gradient(90deg, ${o.skin} 50%, ${o.cloth} 50%)"></span><b>${o.name}</b><span>${o.hint}</span></label>`).join('');
  $('c-starts').innerHTML = Object.entries(STARTS).map(([id, s], i) => `<label class="card"><input type="radio" name="start" value="${id}" ${i === 2 ? 'checked' : ''}><b>${s.label}</b><span>${s.desc}</span></label>`).join('');
}
$('btn-new').addEventListener('click', () => { initAudio(); sfx('ui'); $('screen-title').hidden = true; $('screen-create').hidden = false; buildCreate(); $('c-name').focus(); });
$('btn-continue').addEventListener('click', () => {
  protectKeyboard();   // tela cheia com teclado protegido: precisa sair do próprio clique
  initAudio();
  const d = readSave();
  if (!d) { $('btn-continue').hidden = true; return; }
  startGame(d.profile, d);
});
$('c-back').addEventListener('click', () => { $('screen-create').hidden = true; $('screen-title').hidden = false; });
$('create-form').addEventListener('submit', (e) => {
  e.preventDefault();
  protectKeyboard();
  const f = new FormData(e.target);
  const name = String(f.get('name') || '').trim() || 'Viajante';
  clearSave();
  startGame({ name, origin: f.get('origin'), start: f.get('start'), model: f.get('model') });
});

function startGame(profile, saved) {
  $('screen-title').hidden = true; $('screen-create').hidden = true;
  if (document.activeElement) document.activeElement.blur();
  G.player = createPlayer(profile);
  initMount();
  initCamp(); initEvent();
  initCanteiroGuard(spawnEnemy);
  if (saved) applySave(saved);
  G.sky.markGap(0, false);
  G.cam.yaw = 0;
  updateCamera(0.016, true);
  showHud(true);
  showZone(G.player.zone, false);
  setAmbience('world');
  mode = 'game'; G.running = true;
  if (!saved) {
    first('chegada', 'historia', null, 'Chegada ao Entreposto do Vale', `${profile.name} chegou ao Vale com ${STARTS[profile.start].label} como primeiro conhecimento e ${ITEMS[STARTS[profile.start].weapon].name.toLowerCase()} na mão.`);
    intentsPanel(true);
  } else {
    const m = saved.migrated;
    if (m && m.notes.length) {
      toast(`Trajetória retomada. ${m.notes.join('; ')}. Progresso, bolsa e moedas preservados.`, 'info', 9000);
    } else toast('Trajetória retomada no Entreposto do Vale.', 'info');
  }
}

// ---------- eventos do jogo → interface ----------
on('show-death', (report) => {
  if (report.zone === 'turbulenta') onTurbulentDeath(G.player.downSrc);
  if (bookOpen()) closeBook();
  closePanel();
  setTimeout(() => deathPanel(report), 700);
});
on('event-done', (q) => { closePanel(); if (bookOpen()) closeBook(); fourQuestionsPanel(q); });
on('observe', (c) => { $('cap-title').textContent = c.title; $('cap-text').textContent = c.text; $('caption').hidden = false; });
on('drop', (e) => { const P = G.player; createLootBag(P.pos.x + Math.sin(P.yaw) * 1.2, P.pos.z + Math.cos(P.yaw) * 1.2, [e], 'item largado', { own: true, ttl: 240 }); toast(`Largou ${ITEMS[e.id].name}.`, 'info'); });
on('turb-enter', () => { toast('Você atravessou. Nomes e rostos não se reconhecem aqui.', 'turb', 6000); showZone('turbulenta', true); });
on('turb-leave', () => { showZone(G.player.zone, true); });
on('player-respawn', () => { G.cam.yaw = 0; updateCamera(0.016, true); showZone(G.player.zone, false); });
$('cap-close').addEventListener('click', stopObserve);
function stopObserve() { endObserve(); $('caption').hidden = true; }

// ---------- atalhos ----------
// Tab abre o menu único ao ser solto; segurando Tab, 1/2/3 escolhem a distância da câmera (e aí o
// menu não abre). B/I/M/J continuam como atalhos diretos para cada aba.
let tabChord = false;
function handleKeys() {
  const P = G.player;
  if (G.cinematic) { if (hit('Escape') || hit('KeyF')) stopObserve(); return; }
  if (hit('Escape')) {
    // o navegador solta o ponteiro travado com Esc; esse mesmo Esc não deve abrir a pausa junto
    if (performance.now() - input.unlockedAt < 300) return;
    if (bookOpen()) closeBook();
    else if (panelOpen()) closePanel();
    else pausePanel(() => { clearSave(); location.reload(); }, (lv) => applyQuality(ctx, lv, true));
    return;
  }
  if (P.state === 'dead') return;
  if (hit('Tab')) tabChord = false;
  if (down('Tab') && !G.uiOpen) {
    for (const n of [1, 2, 3]) if (hit(`Digit${n}`) || hit(`Numpad${n}`)) { setCameraPreset(n); tabChord = true; }
  }
  if (released('Tab')) { const chord = tabChord; tabChord = false; if (!chord) toggleMenu(); }
  else if (hit('KeyI')) toggleMenu('inventory');
  else if (hit('KeyB')) toggleMenu('book');
  else if (hit('KeyJ')) toggleMenu('intents');
  if (hit('KeyR') && !G.uiOpen) mountAction(P);
  if (hit('KeyM') && !G.uiOpen) toggleMinimap();
  if (hit('KeyV') && !G.uiOpen) toggleShoulder();
}

// ---------- laço ----------
function frame(now) {
  requestAnimationFrame(frame);
  const raw = now - last;
  const dt = Math.min(0.05, raw / 1000); last = now;
  tick(dt);
  // ajuste automático de qualidade (só durante o jogo; não roda com escolha salva)
  if (mode === 'game' && !G.uiOpen) sampleFrame(ctx, raw, (lv) => toast(`Qualidade gráfica ajustada para ${LEVELS[lv].label} para manter a fluidez. Dá para trocar no menu de pausa (Esc).`, 'info', 7000));
}
function tick(dt) {
  U.uTime.value += dt;
  if (mode === 'loading') { ctx.render(); endFrame(); return; }
  if (mode === 'title') {
    titleT += dt;
    // sobre o Vale, olhando para o norte: a Passagem no horizonte e a estrela que vai sumir
    const a = titleT * 0.015;
    titlePos.set(TITLE_VIEW.x + Math.sin(a) * 5, TITLE_VIEW.y, TITLE_VIEW.z);
    ctx.camera.position.copy(titlePos);
    const az = (TITLE_AZIMUTH + Math.sin(a) * 3) * Math.PI / 180, el = 0.2;
    ctx.camera.lookAt(titlePos.x + Math.sin(az) * 100, titlePos.y + Math.tan(el) * 100, titlePos.z - Math.cos(az) * 100);
    applyDaylight(ctx, 23.2, titlePos, false);
    G.sky.update(dt, titleT, ctx.camera.position);
    if (titleT > 5 && titleT - dt <= 5) { G.sky.vanish(titleStar); sfx('star'); }
    updateDetail(ctx.camera.position, ctx.camera.position);
    updateWorldDetail(ctx.camera.position, LEVELS[QUALITY.level]);
    ctx.render();
    endFrame();
    return;
  }
  const P = G.player;
  const paused = G.uiOpen === 'pause';
  handleKeys();
  if (!paused) {
    G.time += dt;
    updatePointer();
    updatePlayer(dt);
    updateMount(dt);
    updateEnemies(dt);
    updateGroups();
    updateProjectiles(dt);
    updateCamp(dt);
    updateEvent(dt);
    updateStars(dt, G.nightNow || 0);
    updateTurbulent(dt);
    updateMarkets(dt);
    updateLootBags();
    updateNpcs(dt);
    updateInteract();
    updateDiscovery(dt);
    saveT += dt; if (saveT > 20) { saveT = 0; saveGame(); }
  }
  const L = applyDaylight(ctx, clock().hour, P.pos, isTurbulentSpace(P.pos.x));
  G.nightNow = L.night;
  G.sky.update(dt, G.time, ctx.camera.position);
  updateCamera(dt);
  updateFx(dt);
  updateHud(dt);
  updateMenu();
  updateDetail(ctx.camera.position, P.pos);
  updateWorldDetail(ctx.camera.position, LEVELS[QUALITY.level]);
  // pausado, a cena não muda: depois de alguns quadros o canvas mantém a última imagem sem redesenhar
  pausedFrames = paused ? pausedFrames + 1 : 0;
  if (pausedFrames < 3 || ctx.dirty) { ctx.render(); ctx.dirty = false; }
  endFrame();
}
requestAnimationFrame(frame);
addEventListener('beforeunload', () => { if (mode === 'game') saveGame(); });

// ---------- ganchos de depuração e automação (?debug=1 ou automação de testes) ----------
if (typeof window !== 'undefined') {
  window.__demo = {
    G, EVT, CAMP, SKY, TS, ctx, MODELS, CH, TEX, input, groundHeight,
    tp(x, z) { const P = G.player; P.pos.set(x, groundHeight(x, z), z); updateCamera(0.016, true); ctx.dirty = true; },
    freecam(x, y, z, tx = 0, ty = 0, tz = 0) {
      G.freeCam = { pos: [x, y, z], look: [tx, ty, tz] };
      updateCamera(0.016, true);
      ctx.dirty = true;
    },
    nocam() { G.freeCam = null; ctx.dirty = true; },
    world() { return { status: WORLD_STATUS, scene: Object.keys(WORLD_STATUS.timings) }; },
    veg() {
      return {
        fields: LOD_FIELDS.map((f) => ({ name: f.name, total: f.total, active: activeCount(f) })),
        cover: { cells: COVER.cells.size, generated: COVER.generated, ms: Math.round(COVER.ms), radius: COVER.radius },
      };
    },
    // gera toda a cobertura do chão e acerta os níveis de detalhe de uma vez (capturas e testes)
    settle() { updateGroundCover(ctx.camera.position, 999); updateLODFields(ctx.camera.position, true); ctx.dirty = true; },
    give(id, n = 1) { return receive(G.player, id, n); },
    coins(n) { G.player.coins += n; },
    hurt(n = 999) { hurtPlayer(n, { x: G.player.pos.x + 1, z: G.player.pos.z, name: 'teste' }); },
    portal() { forcePortalNear(); },
    killNear(r = 30, max = 99) { const P = G.player; let n = 0; for (const e of [...G.enemies]) { if (n >= max) break; if (e.alive && e.faction !== 'guard' && Math.hypot(e.pos.x - P.pos.x, e.pos.z - P.pos.z) < r) { hurtEnemy(e, 9999); n++; } } return n; },
    contribute(n = 10) { contribute('ferramentas', n); },
    night() { G.time += ((22 - clock().hour + 24) % 24) / 24 * 420; },
    vanish() { SKY.next = G.time; },
    step(frames = 1, dt = 1 / 30) { for (let i = 0; i < frames; i++) tick(dt); return mode; },
    mode: () => mode,
    applyQuality(lv) { applyQuality(ctx, lv, false); },
    state() { const P = G.player; return { pos: P.pos.toArray().map((v) => +v.toFixed(1)), zone: P.zone, place: P.place, hp: P.hp, state: P.state, coins: P.coins, inv: P.inv, camp: CAMP.state, evt: EVT.progress, enemies: G.enemies.length }; },
  };
}
