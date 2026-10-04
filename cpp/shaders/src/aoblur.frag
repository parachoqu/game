#version 450
// Filtro de Poisson da oclusão (PoissonDenoiseShader do three): 12 amostras num disco de 5 pixels,
// pesadas pela diferença de oclusão (lumaPhi), de profundidade (depthPhi) e de normal (normalPhi).
layout(location = 0) in vec2 vUv;
layout(set = 2, binding = 0) uniform sampler2D tAo;
layout(set = 2, binding = 1) uniform sampler2D tDepth;
layout(set = 2, binding = 2) uniform sampler2D tNormal;
layout(std140, set = 3, binding = 0) uniform Blur {
  vec4 params;  // x: raio (px); y: lumaPhi; z: depthPhi; w: normalPhi
  vec4 size;    // zw: 1/tamanho
} B;
layout(location = 0) out vec4 outAo;

const vec2 DISC[12] = vec2[](vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696, 0.457), vec2(-0.203, 0.621),
                             vec2(0.962, -0.195), vec2(0.473, -0.480), vec2(0.519, 0.767), vec2(0.185, -0.893),
                             vec2(0.507, 0.064), vec2(0.896, 0.412), vec2(-0.322, -0.933), vec2(-0.792, -0.598));

float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

void main() {
  float a0 = texture(tAo, vUv).r;
  float d0 = texture(tDepth, vUv).r;
  vec3 n0 = texture(tNormal, vUv).xyz * 2.0 - 1.0;
  if (d0 >= 1.0) { outAo = vec4(1.0); return; }
  float ang = ign(gl_FragCoord.xy) * 6.2831853;
  mat2 rot = mat2(cos(ang), sin(ang), -sin(ang), cos(ang));
  float sum = a0, wsum = 1.0;
  for (int i = 0; i < 12; i++) {
    vec2 uv = vUv + rot * DISC[i] * B.params.x * B.size.zw;
    float a = texture(tAo, uv).r;
    float d = texture(tDepth, uv).r;
    vec3 n = texture(tNormal, uv).xyz * 2.0 - 1.0;
    float w = exp(-abs(a - a0) * B.params.y) * exp(-abs(d - d0) * B.params.z * 4000.0) *
              pow(max(dot(n, n0), 0.0), B.params.w);
    if (d >= 1.0) w = 0.0;
    sum += a * w;
    wsum += w;
  }
  float ao = sum / wsum;
  outAo = vec4(ao, ao, ao, 1.0);
}
