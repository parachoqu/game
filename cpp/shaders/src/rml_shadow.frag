#version 450
// Sombra projetada (filter: drop-shadow): o alfa da camada vira a cor da sombra.
layout(location = 0) in vec2 vUv;
layout(set = 2, binding = 0) uniform sampler2D uTex;
layout(std140, set = 3, binding = 0) uniform Shadow {
  vec4 color;   // pré-multiplicada
  vec4 region;  // xy mínimo, zw máximo (uv)
} S;
layout(location = 0) out vec4 outColor;
void main() {
  vec2 inside = step(S.region.xy, vUv) * step(vUv, S.region.zw);
  outColor = texture(uTex, vUv).a * inside.x * inside.y * S.color;
}
