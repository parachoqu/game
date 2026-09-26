// Teclado (por e.code, independe do layout ABNT/US), mouse e ponteiro travado (mira).
export const input = {
  keys: new Set(), pressed: new Set(), released: new Set(),
  // mira: com o ponteiro travado o cursor some, o mouse vira giro de câmera (look) e a mira fica
  // no centro da tela. lockWanted é ligado pelo jogo nos quadros em que mirar é permitido: o
  // travamento só pode ser pedido dentro do gesto do usuário, então sai do próprio mousedown.
  // aimLook: o jogo liga enquanto mira. Com ou sem travamento, o mouse vira giro de câmera e a
  // mira fica presa no centro da tela; rawX/rawY guardam onde o cursor de verdade está, para o
  // empurrão na borda continuar girando quando ele encosta no fim da tela.
  locked: false, lockBlocked: false, lockWanted: false, aimLook: false, pinned: false, unlockedAt: -1e9,
  look: { dx: 0, dy: 0 },
  mouse: { x: innerWidth / 2, y: innerHeight / 2, rawX: innerWidth / 2, rawY: innerHeight / 2, left: false, right: false, middle: false, leftPressed: false, rightPressed: false, wheel: 0, dragDX: 0, dragDY: 0, leftDX: 0, leftDY: 0 },
};

let cv = null;
const center = () => { input.mouse.x = innerWidth / 2; input.mouse.y = innerHeight / 2; };
export function requestLook() {
  if (!cv || input.locked || input.lockBlocked || document.pointerLockElement) return;
  try {
    const p = cv.requestPointerLock();
    if (p && p.catch) p.catch(() => { input.lockBlocked = true; });
  } catch { input.lockBlocked = true; }
}
export function releaseLook() {
  if (document.pointerLockElement) document.exitPointerLock();
}

// Teclas do jogo. Com Ctrl pressionado (agachar), o navegador trataria várias delas como atalho
// (Ctrl+D favorito, Ctrl+S salvar, Ctrl+F busca…): o jogo as segura. Ctrl+W, Ctrl+T e Ctrl+Tab o
// navegador não entrega à página fora da tela cheia protegida (`keyguard.js`).
export const GAME_KEYS = new Set([
  'KeyW', 'KeyA', 'KeyS', 'KeyD', 'KeyQ', 'KeyE', 'KeyR', 'KeyF', 'KeyC', 'KeyV', 'KeyM', 'KeyI', 'KeyB', 'KeyJ',
  'Digit1', 'Digit2', 'Digit3', 'Numpad1', 'Numpad2', 'Numpad3', 'Space', 'Tab',
]);
const ALWAYS_HELD = ['Space', 'Tab', 'ArrowLeft', 'ArrowRight', 'ArrowUp', 'ArrowDown', 'ControlLeft', 'ControlRight'];

// campos de texto visíveis recebem o teclado; o jogo ignora essas teclas
const isField = (t) => t && t.closest && t.closest('input[type=text], input:not([type]), textarea, select') && t.offsetParent !== null;

export function initInput(canvas) {
  cv = canvas;
  addEventListener('keydown', (e) => {
    if (isField(e.target)) return;
    if (!input.keys.has(e.code)) input.pressed.add(e.code);
    input.keys.add(e.code);
    if (ALWAYS_HELD.includes(e.code) || (e.ctrlKey && GAME_KEYS.has(e.code))) e.preventDefault();
  });
  addEventListener('keyup', (e) => {
    if (input.keys.delete(e.code)) input.released.add(e.code);
  });
  addEventListener('blur', () => { input.keys.clear(); input.mouse.left = input.mouse.right = input.mouse.middle = false; releaseLook(); });
  canvas.addEventListener('mousedown', (e) => {
    if (e.button === 0) { input.mouse.left = true; input.mouse.leftPressed = true; }
    if (e.button === 2) { input.mouse.right = true; input.mouse.rightPressed = true; if (input.lockWanted) requestLook(); }
    if (e.button === 1) { input.mouse.middle = true; e.preventDefault(); }   // arrastar gira a câmera
  });
  addEventListener('mouseup', (e) => {
    if (e.button === 0) input.mouse.left = false;
    if (e.button === 2) { input.mouse.right = false; releaseLook(); }
    if (e.button === 1) input.mouse.middle = false;
  });
  addEventListener('mousemove', (e) => {
    input.mouse.rawX = e.clientX; input.mouse.rawY = e.clientY;
    if (input.locked || input.aimLook) { input.look.dx += e.movementX; input.look.dy += e.movementY; center(); return; }
    input.mouse.x = e.clientX; input.mouse.y = e.clientY;
    if (input.mouse.middle) { input.mouse.dragDX += e.movementX; input.mouse.dragDY += e.movementY; }
    if (input.mouse.left) { input.mouse.leftDX += e.movementX; input.mouse.leftDY += e.movementY; }
  });
  document.addEventListener('pointerlockchange', () => {
    input.locked = document.pointerLockElement === canvas;
    if (input.locked) center();
    else { input.unlockedAt = performance.now(); input.look.dx = input.look.dy = 0; }
  });
  document.addEventListener('pointerlockerror', () => { input.lockBlocked = true; input.locked = false; });
  canvas.addEventListener('wheel', (e) => { input.mouse.wheel += Math.sign(e.deltaY); e.preventDefault(); }, { passive: false });
  canvas.addEventListener('contextmenu', (e) => e.preventDefault());
}

export function endFrame() {
  input.pressed.clear();
  input.released.clear();
  input.mouse.leftPressed = input.mouse.rightPressed = false;
  input.mouse.wheel = 0; input.mouse.dragDX = input.mouse.dragDY = input.mouse.leftDX = input.mouse.leftDY = 0;
  input.look.dx = input.look.dy = 0;
  // mirando, a mira é o centro da tela (inclusive depois de redimensionar); ao soltar, o ponteiro
  // volta para onde o cursor de verdade parou
  const pin = input.locked || input.aimLook;
  if (pin) center();
  else if (input.pinned) { input.mouse.x = input.mouse.rawX; input.mouse.y = input.mouse.rawY; }
  input.pinned = pin;
}
export const down = (code) => input.keys.has(code);
export const hit = (code) => input.pressed.has(code);
// soltou neste quadro
export const released = (code) => input.released.has(code);
