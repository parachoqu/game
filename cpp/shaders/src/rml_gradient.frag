#version 450
// Degradês do RmlUi 6 (o shader de gradiente do renderizador GL3 de referência).
#define LINEAR 0
#define RADIAL 1
#define CONIC 2
#define REPEATING_LINEAR 3
#define REPEATING_RADIAL 4
#define REPEATING_CONIC 5
#define PI 3.14159265
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(std140, set = 3, binding = 0) uniform Gradient {
  ivec4 info;        // x: função; y: número de paradas
  vec4 pv;           // xy: ponto inicial/centro; zw: vetor até o fim / curvatura / direção
  vec4 positions[4]; // 16 posições normalizadas
  vec4 colors[16];   // pré-multiplicadas
} G;
layout(location = 0) out vec4 outColor;

float stopPos(int i) { return G.positions[i >> 2][i & 3]; }

vec4 mixStops(float t) {
  vec4 color = G.colors[0];
  for (int i = 1; i < G.info.y; i++) color = mix(color, G.colors[i], smoothstep(stopPos(i - 1), stopPos(i), t));
  return color;
}

void main() {
  int func = G.info.x;
  vec2 p = G.pv.xy, v = G.pv.zw;
  float t = 0.0;
  if (func == LINEAR || func == REPEATING_LINEAR) {
    t = dot(v, vUv - p) / dot(v, v);
  } else if (func == RADIAL || func == REPEATING_RADIAL) {
    t = length(v * (vUv - p));
  } else {
    mat2 R = mat2(v.x, -v.y, v.y, v.x);
    vec2 V = R * (vUv - p);
    t = 0.5 + atan(-V.x, V.y) / (2.0 * PI);
  }
  if (func >= REPEATING_LINEAR) {
    float t0 = stopPos(0), t1 = stopPos(G.info.y - 1);
    t = t0 + mod(t - t0, t1 - t0);
  }
  outColor = vColor * mixStops(t);
}
