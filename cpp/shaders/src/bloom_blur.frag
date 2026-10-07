#version 450
// UnrealBloomPass (three r185), desfoque gaussiano separável: kernel 6, 10, 14, 18, 22 nos cinco níveis,
// sigma = raio / 3. Os coeficientes já somam ~1 (sem dividir pela soma dos pesos, como no r185).
layout(location = 0) in vec2 vUv;
layout(set = 2, binding = 0) uniform sampler2D tColor;
layout(std140, set = 3, binding = 0) uniform Blur {
  vec4 dir;       // xy: direção × 1/tamanho do nível; z: raio do kernel
  vec4 coeff[6];  // coeficientes 0–23
} B;
layout(location = 0) out vec4 outColor;
float coef(int i) { return B.coeff[i / 4][i % 4]; }
void main() {
  int radius = int(B.dir.z + 0.5);
  vec3 sum = texture(tColor, vUv).rgb * coef(0);
  for (int i = 1; i < radius; i++) {
    vec2 off = B.dir.xy * float(i);
    sum += (texture(tColor, vUv + off).rgb + texture(tColor, vUv - off).rgb) * coef(i);
  }
  outColor = vec4(sum, 1.0);
}
