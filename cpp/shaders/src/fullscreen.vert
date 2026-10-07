#version 450
// Triângulo que cobre a tela (sem buffer de vértices). uv (0,0) no canto superior esquerdo.
layout(location = 0) out vec2 vUv;
layout(location = 1) out vec2 vNdc;
void main() {
  vec2 p = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2));
  vUv = p;
  vNdc = vec2(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0);
  gl_Position = vec4(vNdc, 0.0, 1.0);
}
