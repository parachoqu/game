#version 450
// FXAA (Timothy Lottes, variante de console) na saída já em sRGB, no lugar do SMAAPass da demo: o mesmo
// papel (suavizar as bordas serrilhadas da geometria e do alphaTest da folhagem) num passe só.
layout(location = 0) in vec2 vUv;
layout(set = 2, binding = 0) uniform sampler2D tColor;
layout(std140, set = 3, binding = 0) uniform Fxaa {
  vec4 size;  // zw: 1/tamanho
} X;
layout(location = 0) out vec4 outColor;

const float REDUCE_MIN = 1.0 / 128.0;
const float REDUCE_MUL = 1.0 / 8.0;
const float SPAN_MAX = 8.0;

void main() {
  vec2 inv = X.size.zw;
  vec3 rgbNW = texture(tColor, vUv + vec2(-1.0, -1.0) * inv).rgb;
  vec3 rgbNE = texture(tColor, vUv + vec2(1.0, -1.0) * inv).rgb;
  vec3 rgbSW = texture(tColor, vUv + vec2(-1.0, 1.0) * inv).rgb;
  vec3 rgbSE = texture(tColor, vUv + vec2(1.0, 1.0) * inv).rgb;
  vec3 rgbM = texture(tColor, vUv).rgb;
  const vec3 L = vec3(0.299, 0.587, 0.114);
  float lNW = dot(rgbNW, L), lNE = dot(rgbNE, L), lSW = dot(rgbSW, L), lSE = dot(rgbSE, L), lM = dot(rgbM, L);
  float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
  float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
  vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));
  float reduce = max((lNW + lNE + lSW + lSE) * (0.25 * REDUCE_MUL), REDUCE_MIN);
  float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
  dir = clamp(dir * rcpMin, vec2(-SPAN_MAX), vec2(SPAN_MAX)) * inv;
  vec3 rgbA = 0.5 * (texture(tColor, vUv + dir * (1.0 / 3.0 - 0.5)).rgb + texture(tColor, vUv + dir * (2.0 / 3.0 - 0.5)).rgb);
  vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(tColor, vUv - dir * 0.5).rgb + texture(tColor, vUv + dir * 0.5).rgb);
  float lB = dot(rgbB, L);
  outColor = vec4((lB < lMin || lB > lMax) ? rgbA : rgbB, 1.0);
}
