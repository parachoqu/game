// Efeitos sonoros sintetizados (WebAudio), sem arquivos externos.
let ctx = null, master = null, ambGain = null, ambNodes = [], noiseBuf = null;
export const audio = { muted: false };

export function initAudio() {
  if (ctx) return;
  try {
    ctx = new (window.AudioContext || window.webkitAudioContext)();
    master = ctx.createGain(); master.gain.value = 0.55; master.connect(ctx.destination);
    ambGain = ctx.createGain(); ambGain.gain.value = 0.0; ambGain.connect(master);
    noiseBuf = ctx.createBuffer(1, ctx.sampleRate * 2, ctx.sampleRate);
    const d = noiseBuf.getChannelData(0); for (let i = 0; i < d.length; i++) d[i] = Math.random() * 2 - 1;
  } catch { ctx = null; }
}
export function setMuted(m) { audio.muted = m; if (master) master.gain.value = m ? 0 : 0.55; }

function env(g, t, a, d, peak) { g.gain.setValueAtTime(0.0001, t); g.gain.exponentialRampToValueAtTime(peak, t + a); g.gain.exponentialRampToValueAtTime(0.0001, t + a + d); }
function tone(freq, dur, type = 'sine', peak = 0.2, slide = 0, delay = 0) {
  const t = ctx.currentTime + delay;
  const o = ctx.createOscillator(), g = ctx.createGain();
  o.type = type; o.frequency.setValueAtTime(freq, t);
  if (slide) o.frequency.exponentialRampToValueAtTime(Math.max(20, freq * slide), t + dur);
  env(g, t, 0.008, dur, peak); o.connect(g); g.connect(master); o.start(t); o.stop(t + dur + 0.05);
}
function noise(dur, freq, q = 1, peak = 0.3, type = 'bandpass', slide = 0, delay = 0) {
  const t = ctx.currentTime + delay;
  const s = ctx.createBufferSource(); s.buffer = noiseBuf;
  const f = ctx.createBiquadFilter(); f.type = type; f.frequency.setValueAtTime(freq, t); f.Q.value = q;
  if (slide) f.frequency.exponentialRampToValueAtTime(freq * slide, t + dur);
  const g = ctx.createGain(); env(g, t, 0.005, dur, peak);
  s.connect(f); f.connect(g); g.connect(master); s.start(t, Math.random()); s.stop(t + dur + 0.05);
}

export function sfx(name) {
  if (!ctx || audio.muted) return;
  if (ctx.state === 'suspended') ctx.resume();
  switch (name) {
    case 'swing': noise(0.16, 900, 1.2, 0.18, 'bandpass', 3); break;
    case 'hit': tone(120, 0.14, 'triangle', 0.35, 0.5); noise(0.08, 2000, 0.8, 0.2); break;
    case 'hitmark': tone(1950, 0.035, 'triangle', 0.22); tone(2800, 0.025, 'sine', 0.15, 0, 0.015); break;
    case 'hurt': tone(90, 0.22, 'sawtooth', 0.18, 0.6); noise(0.12, 600, 0.7, 0.25); break;
    case 'block': tone(1400, 0.18, 'square', 0.08, 0.9); tone(2100, 0.12, 'sine', 0.08); break;
    case 'dodge': noise(0.22, 500, 0.8, 0.14, 'bandpass', 4); break;
    case 'arrow': tone(420, 0.08, 'triangle', 0.15, 0.4); noise(0.12, 3000, 2, 0.1); break;
    case 'warn': tone(660, 0.07, 'square', 0.05); break;
    case 'pickup': tone(520, 0.08, 'sine', 0.15); tone(780, 0.1, 'sine', 0.13, 0, 0.07); break;
    case 'coin': tone(1300, 0.06, 'square', 0.06); tone(1750, 0.12, 'square', 0.05, 0, 0.05); break;
    case 'craft': tone(900, 0.25, 'triangle', 0.15, 0.98); noise(0.05, 4000, 3, 0.2); tone(1350, 0.3, 'sine', 0.06, 1, 0.02); break;
    case 'gather': noise(0.07, 1500, 1.5, 0.18); tone(220, 0.06, 'triangle', 0.1, 0.7); break;
    case 'star': tone(1560, 1.6, 'sine', 0.09, 0.5); tone(1040, 2.2, 'sine', 0.06, 0.5, 0.25); break;
    case 'portal': tone(70, 1.2, 'sawtooth', 0.08, 1.4); tone(105, 1.2, 'sine', 0.1, 0.8); break;
    case 'death': tone(220, 1.1, 'sine', 0.2, 0.3); tone(165, 1.3, 'triangle', 0.12, 0.3, 0.2); break;
    case 'book': noise(0.18, 3000, 0.6, 0.08, 'highpass'); tone(880, 0.5, 'sine', 0.05, 1, 0.1); break;
    case 'ui': tone(700, 0.04, 'sine', 0.06); break;
    case 'deny': tone(200, 0.12, 'square', 0.06, 0.8); break;
    case 'event': tone(523, 0.3, 'triangle', 0.12); tone(659, 0.3, 'triangle', 0.12, 1, 0.15); tone(784, 0.6, 'triangle', 0.12, 1, 0.3); break;
    case 'horn': tone(146, 0.9, 'sawtooth', 0.07, 1.02); tone(220, 0.9, 'sawtooth', 0.05, 1.01, 0.05); break;
    case 'magic': tone(620, 0.28, 'sine', 0.12, 1.4); noise(0.2, 1800, 1.5, 0.08, 'bandpass', 1.8); break;
    case 'roar': tone(85, 0.65, 'sawtooth', 0.22, 0.7); noise(0.4, 450, 0.8, 0.25, 'lowpass', 0.5); break;
    case 'slam': tone(75, 0.35, 'triangle', 0.35, 0.4); noise(0.25, 300, 1.2, 0.3); break;
  }
}

// Ambiência contínua: 'world' (vento suave) ou 'turb' (zumbido dissonante)
export function setAmbience(kind) {
  if (!ctx) return;
  for (const n of ambNodes) { try { n.stop(); } catch { /* já parado */ } }
  ambNodes = [];
  const t = ctx.currentTime;
  if (kind === 'world') {
    const s = ctx.createBufferSource(); s.buffer = noiseBuf; s.loop = true;
    const f = ctx.createBiquadFilter(); f.type = 'lowpass'; f.frequency.value = 380;
    const g = ctx.createGain(); g.gain.value = 0.12;
    s.connect(f); f.connect(g); g.connect(ambGain); s.start(); ambNodes.push(s);
  } else if (kind === 'turb') {
    for (const fr of [55, 58.3, 82.4]) {
      const o = ctx.createOscillator(); o.type = 'sawtooth'; o.frequency.value = fr;
      const f = ctx.createBiquadFilter(); f.type = 'lowpass'; f.frequency.value = 260;
      const g = ctx.createGain(); g.gain.value = 0.05;
      o.connect(f); f.connect(g); g.connect(ambGain); o.start(); ambNodes.push(o);
    }
  }
  ambGain.gain.cancelScheduledValues(t);
  ambGain.gain.setValueAtTime(0.0001, t);
  ambGain.gain.exponentialRampToValueAtTime(kind === 'none' ? 0.0001 : 0.8, t + 1.5);
}
