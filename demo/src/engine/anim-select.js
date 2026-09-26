// Escolha de clipes a partir do estado do jogo. Funções puras, sem three.js: o animador
// (`characters.js`), o combate e os inimigos usam estas regras, e `tests/animation.test.js` as
// confere com registros parciais.
//
// Referencial do personagem: o modelo olha para +z; +x é o lado ESQUERDO dele (o mesmo de `bend`).
// `has(key)` diz se o clipe existe no registro resolvido do personagem.

// Vetor do mundo (dx, dz) no referencial de um personagem virado para `yaw`.
export function toLocal(yaw, dx, dz) {
  const s = Math.sin(yaw), c = Math.cos(yaw);
  return { fwd: dx * s + dz * c, left: dx * c - dz * s };
}

// ---------------------------------------------------------------- locomoção direcional
// Pesos de caminhada para frente, para trás e de lado a partir da direção local do passo. Nas
// diagonais os dois clipes vizinhos dividem o peso (a soma é sempre 1). Sem o clipe para trás, o
// peso volta à caminhada tocada ao contrário (`reverse`, o comportamento antigo); sem os laterais,
// volta à caminhada para frente.
export function locomotionWeights(fwd, left, has) {
  const w = { walk: 1, walkBack: 0, strafeLeft: 0, strafeRight: 0, reverse: false };
  const L = Math.hypot(fwd, left);
  if (L < 1e-4) return w;
  const f = fwd / L, l = left / L;
  w.walk = Math.max(0, f); w.walkBack = Math.max(0, -f);
  w.strafeLeft = Math.max(0, l); w.strafeRight = Math.max(0, -l);
  if (!has('walkBack')) { w.reverse = f < -0.15; w.walk += w.walkBack; w.walkBack = 0; }
  if (!has('strafeLeft')) { w.walk += w.strafeLeft; w.strafeLeft = 0; }
  if (!has('strafeRight')) { w.walk += w.strafeRight; w.strafeRight = 0; }
  const sum = w.walk + w.walkBack + w.strafeLeft + w.strafeRight;
  for (const k of ['walk', 'walkBack', 'strafeLeft', 'strafeRight']) w[k] /= sum;
  return w;
}

// ---------------------------------------------------------------- esquiva
// Clipe da esquiva pelo sentido do deslocamento em relação a para onde o corpo está virado; sem o
// clipe direcional (ou quando ele não combina com a arma, `allow = false`), o rolamento de sempre.
export function dodgeClip(fwd, left, has, allow = true) {
  let key;
  if (Math.abs(fwd) >= Math.abs(left)) key = fwd >= 0 ? 'dodgeForward' : 'dodgeBackward';
  else key = left > 0 ? 'dodgeLeft' : 'dodgeRight';
  if (allow && has(key)) return key;
  return has('roll') ? 'roll' : null;
}

// Quanto o corpo fica girado em relação ao sentido da esquiva para o clipe mostrar esse sentido:
// yaw do modelo = yaw do deslocamento − DODGE_FACING[clipe]. O lado esquerdo de quem olha para yaw
// aponta para yaw + π/2 (ver toLocal).
export const DODGE_FACING = { dodgeForward: 0, roll: 0, dodgeLeft: Math.PI / 2, dodgeRight: -Math.PI / 2, dodgeBackward: Math.PI };

// ---------------------------------------------------------------- impacto e morte
// Lado de onde veio o golpe, no referencial da vítima: o vetor vai da vítima até a origem do golpe e
// vence o eixo dominante. Sem origem conhecida (queimadura, dano de script), vale 'front'.
export function hitSide(victimYaw, vx, vz, from) {
  if (!from || !Number.isFinite(from.x) || !Number.isFinite(from.z)) return 'front';
  const { fwd, left } = toLocal(victimYaw, from.x - vx, from.z - vz);
  if (Math.abs(fwd) < 1e-6 && Math.abs(left) < 1e-6) return 'front';
  if (Math.abs(fwd) >= Math.abs(left)) return fwd >= 0 ? 'front' : 'back';
  return left > 0 ? 'left' : 'right';
}
export const HIT_CLIPS = { front: 'hitFront', back: 'hitBack', left: 'hitLeft', right: 'hitRight' };
export function hitClip(side, has) {
  const key = HIT_CLIPS[side];
  return key && has(key) ? key : null;
}

// Para que lado o corpo cai: golpe vindo da metade da frente empurra para trás; da metade de trás,
// para frente. Sem origem, para trás.
export function deathFall(victimYaw, vx, vz, from) {
  if (!from || !Number.isFinite(from.x) || !Number.isFinite(from.z)) return 'backward';
  return toLocal(victimYaw, from.x - vx, from.z - vz).fwd >= 0 ? 'backward' : 'forward';
}
export function deathClip(fall, has) {
  const key = fall === 'forward' ? 'deathForward' : 'deathBackward';
  if (has(key)) return key;
  return has('down') ? 'down' : null;
}

// Só humanoides recebem os clipes humanos; criaturas e montaria seguem no animador próprio.
export const isHumanoidModel = (kind) => kind === 'human';
