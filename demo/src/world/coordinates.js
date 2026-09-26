// Contrato único de coordenadas entre o mundo autoral (Blender/glTF) e a cena da demo.
//
// O Blender exporta glTF com Y para cima: um ponto do plano do mundo (x, y) vira (x, altura, -y).
// A conferência com as âncoras confirma o sinal: `bridge_main` está em (0, -145) no plano e o nó
// `Bridge_Main` aparece em z = +145; `observatory` (-260, 205) aparece em z = -205. Todo o resto do
// jogo passa por aqui — nenhum módulo deve inverter eixos por conta própria.
//
// A Região Turbulenta é uma cena separada de 256 × 256 m. Para que as duas nunca se sobreponham e
// os portais continuem funcionando como hoje, ela é desenhada deslocada em X. O deslocamento é
// técnico: não representa distância na história.

export const PLAN_Z_SIGN = -1;                       // z_three = PLAN_Z_SIGN * y_plano

// 1.400 m deixa a Turbulenta a 760 m da borda leste da região (x = 512) com folga de 128 m para a
// própria área: fora do alcance da névoa e das luzes da região, e ainda dentro da precisão de float.
export const TURB_RUNTIME_OFFSET = { x: 1400, y: 0, z: 0 };
// Fronteira técnica entre as duas cenas; nada do mundo principal passa daqui.
export const TURB_GATE_X = 1000;

export const isTurbulentSpace = (x) => x > TURB_GATE_X;

// ---------------------------------------------------------------- região comercial
export function worldPlanToThree(x, y, out = {}) {
  out.x = x;
  out.z = PLAN_Z_SIGN * y;
  return out;
}
export function threeToWorldPlan(x, z, out = {}) {
  out.x = x;
  out.y = PLAN_Z_SIGN * z;
  return out;
}
export const planXToThreeX = (x) => x;
export const planYToThreeZ = (y) => PLAN_Z_SIGN * y;
export const threeZToPlanY = (z) => PLAN_Z_SIGN * z;

// âncora do manifesto: [x_plano, y_plano, altura]
export function regionAnchorToThree(anchor, out = {}) {
  out.x = anchor[0];
  out.y = anchor[2];
  out.z = PLAN_Z_SIGN * anchor[1];
  return out;
}

// ---------------------------------------------------------------- Região Turbulenta
export function turbPlanToThree(x, y, out = {}) {
  out.x = x + TURB_RUNTIME_OFFSET.x;
  out.z = PLAN_Z_SIGN * y + TURB_RUNTIME_OFFSET.z;
  return out;
}
export function threeToTurbPlan(x, z, out = {}) {
  out.x = x - TURB_RUNTIME_OFFSET.x;
  out.y = PLAN_Z_SIGN * (z - TURB_RUNTIME_OFFSET.z);
  return out;
}
export function turbulentAnchorToThree(anchor, out = {}) {
  out.x = anchor[0] + TURB_RUNTIME_OFFSET.x;
  out.y = anchor[2] + TURB_RUNTIME_OFFSET.y;
  out.z = PLAN_Z_SIGN * anchor[1] + TURB_RUNTIME_OFFSET.z;
  return out;
}

// Converte uma âncora conforme a cena a que pertence.
export function anchorToThree(anchor, scene = 'region', out = {}) {
  return scene === 'turbulent' ? turbulentAnchorToThree(anchor, out) : regionAnchorToThree(anchor, out);
}

// Polilinha do manifesto ([x, y, altura]) → pares [x, z] da cena.
export function routeToThree(points, scene = 'region') {
  const dx = scene === 'turbulent' ? TURB_RUNTIME_OFFSET.x : 0;
  const dz = scene === 'turbulent' ? TURB_RUNTIME_OFFSET.z : 0;
  return points.map((p) => [p[0] + dx, PLAN_Z_SIGN * p[1] + dz]);
}
