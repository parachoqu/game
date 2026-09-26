// Teclado protegido: com Ctrl segurado (agachar), apertar W vira Ctrl+W, e o navegador fecha a aba.
// Nenhuma página consegue impedir isso numa janela comum. A exceção é a Keyboard Lock API: em tela
// cheia, no Chrome e no Edge, as teclas travadas chegam ao jogo em vez de virar atalho do navegador —
// Ctrl+W, Ctrl+T, Ctrl+Tab e Ctrl+1…9 deixam de fechar ou trocar a aba.
//
// Ao entrar no jogo (um clique do jogador), a página pede tela cheia e trava as teclas do jogo, as
// letras, os números e o Esc. Esc curto continua abrindo a pausa; segurar Esc sai da tela cheia (o
// próprio navegador avisa). Alt+Tab e a tecla do sistema não são travados.
//
// Onde a trava não existe (Firefox, Safari, dentro de um iframe como o do Artifact, ou fora da tela
// cheia), resta a confirmação de saída do navegador enquanto se joga.
const KEY = 'projeto-game-teclado';
const LETTERS = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'.split('').map((c) => `Key${c}`);
const DIGITS = '0123456789'.split('').map((d) => `Digit${d}`);
const LOCK_KEYS = [...LETTERS, ...DIGITS, 'Numpad1', 'Numpad2', 'Numpad3', 'Space', 'Tab', 'Escape'];

export const KEYGUARD = {
  enabled: true,
  active: false,
  // a trava só existe na janela principal (não num iframe) e em navegadores que a implementam
  supported: typeof navigator !== 'undefined' && !!navigator.keyboard?.lock && typeof window !== 'undefined' && window.top === window,
};
try { const v = localStorage.getItem(KEY); if (v === '0') KEYGUARD.enabled = false; } catch { /* padrão: ligado */ }

let inGame = () => false, onLeave = null;

export function initKeyguard({ isPlaying, onExit } = {}) {
  if (isPlaying) inGame = isPlaying;
  onLeave = onExit || null;
  document.addEventListener('fullscreenchange', () => {
    if (document.fullscreenElement) return;
    const was = KEYGUARD.active;
    KEYGUARD.active = false;
    try { navigator.keyboard?.unlock?.(); } catch { /* ignora */ }
    if (was && inGame()) onLeave?.();
  });
  // último recurso onde a trava não existe: o navegador pergunta antes de fechar a aba
  addEventListener('beforeunload', (e) => {
    if (!inGame() || KEYGUARD.active) return;
    e.preventDefault();
    e.returnValue = '';
  });
}

// Chamado dentro do clique que inicia o jogo (a tela cheia exige um gesto do jogador).
export async function protectKeyboard() {
  if (!KEYGUARD.enabled || !KEYGUARD.supported) return false;
  try {
    if (!document.fullscreenElement) await document.documentElement.requestFullscreen({ navigationUI: 'hide' });
    await navigator.keyboard.lock(LOCK_KEYS);
    KEYGUARD.active = true;
  } catch {
    KEYGUARD.active = false;
  }
  return KEYGUARD.active;
}

export function releaseKeyboard() {
  try { navigator.keyboard?.unlock?.(); } catch { /* ignora */ }
  KEYGUARD.active = false;
  if (document.fullscreenElement) document.exitFullscreen().catch(() => {});
}

export function setKeyguard(on) {
  KEYGUARD.enabled = !!on;
  try { localStorage.setItem(KEY, on ? '1' : '0'); } catch { /* ignora */ }
  if (!on) releaseKeyboard();
}
