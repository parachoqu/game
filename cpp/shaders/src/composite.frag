#version 450
// Saída: cena HDR → ACES filmic (a mesma aproximação do three.js) → sRGB.
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec2 vNdc;

layout(set = 2, binding = 0) uniform sampler2D uScene;
layout(std140, set = 3, binding = 0) uniform Post { vec4 params; } P;  // x: exposição

layout(location = 0) out vec4 outColor;

// three.js ACESFilmicToneMapping (ajuste de Stephen Hill)
vec3 rrtOdt(vec3 v) {
  vec3 a = v * (v + 0.0245786) - 0.000090537;
  vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
  return a / b;
}
vec3 aces(vec3 color) {
  const mat3 inM = mat3(vec3(0.59719, 0.07600, 0.02840), vec3(0.35458, 0.90834, 0.13383), vec3(0.04823, 0.01566, 0.83777));
  const mat3 outM = mat3(vec3(1.60475, -0.10208, -0.00327), vec3(-0.53108, 1.10813, -0.07276), vec3(-0.07367, -0.00605, 1.07602));
  color *= 1.0 / 0.6;
  color = inM * color;
  color = rrtOdt(color);
  color = outM * color;
  return clamp(color, 0.0, 1.0);
}
vec3 linearToSrgb(vec3 c) {
  return mix(c * 12.92, 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), c));
}

void main() {
  vec3 hdr = texture(uScene, vUv).rgb * P.params.x;
  outColor = vec4(linearToSrgb(aces(hdr)), 1.0);
}
