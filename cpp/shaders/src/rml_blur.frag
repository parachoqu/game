#version 450
// Desfoque gaussiano separável de 7 amostras, restrito à região (sem sangrar de fora do recorte).
layout(location = 0) in vec2 vUv;
layout(set = 2, binding = 0) uniform sampler2D uTex;
layout(std140, set = 3, binding = 0) uniform Blur {
  vec4 texel;    // xy: passo entre amostras (uv)
  vec4 region;   // xy: mínimo; zw: máximo (uv)
  vec4 weights;  // pesos 0..3
} B;
layout(location = 0) out vec4 outColor;
void main() {
  vec4 color = vec4(0.0);
  for (int i = 0; i < 7; i++) {
    vec2 uv = vUv - float(i - 3) * B.texel.xy;
    vec2 inside = step(B.region.xy, uv) * step(uv, B.region.zw);
    color += texture(uTex, uv) * inside.x * inside.y * B.weights[abs(i - 3)];
  }
  outColor = color;
}
