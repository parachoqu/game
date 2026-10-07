#version 450
// Passes de tela cheia da interface (composição de camadas, filtros): triângulo que cobre a tela,
// uv com escala e deslocamento (redução do desfoque, deslocamento da sombra projetada).
layout(std140, set = 1, binding = 0) uniform Uv { vec4 scaleOffset; } U;
layout(location = 0) out vec2 vUv;
void main() {
  vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
  vUv = p * U.scaleOffset.xy + U.scaleOffset.zw;
  gl_Position = vec4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
}
