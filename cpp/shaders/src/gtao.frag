#version 450
// Oclusão de ambiente por horizonte (GTAO, Jimenez 2016), em meia resolução, com os parâmetros da demo:
// raio 0,8 m, expoente de distância 1,5, espessura 1,2 e 12 amostras (4 fatias × 3 passos por lado).
layout(location = 0) in vec2 vUv;
layout(set = 2, binding = 0) uniform sampler2D tDepth;
layout(set = 2, binding = 1) uniform sampler2D tNormal;
layout(std140, set = 3, binding = 0) uniform Ao {
  mat4 proj;
  mat4 invProj;
  mat4 view;
  vec4 params;  // x: raio (m); y: expoente de distância; z: espessura; w: escala
  vec4 size;    // xy: tamanho do alvo; zw: 1/tamanho
} A;
layout(location = 0) out vec4 outAo;

const float PI = 3.14159265;
const int SLICES = 4;
const int STEPS = 3;

vec3 viewPos(vec2 uv) {
  float d = textureLod(tDepth, uv, 0.0).r;
  vec4 v = A.invProj * vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, d, 1.0);
  return v.xyz / v.w;
}

float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }

void main() {
  float depth = textureLod(tDepth, vUv, 0.0).r;
  vec4 nt = textureLod(tNormal, vUv, 0.0);
  if (depth >= 1.0 || nt.a < 0.5) { outAo = vec4(1.0); return; }
  vec3 P = viewPos(vUv);
  vec3 N = normalize(mat3(A.view) * (nt.xyz * 2.0 - 1.0));
  vec3 V = normalize(-P);
  float radius = A.params.x * A.params.w;
  // raio projetado em pixels do alvo (proj[1][1] = cotangente de meio campo vertical)
  float radiusPx = radius * A.proj[1][1] * 0.5 * A.size.y / max(-P.z, 0.1);
  if (radiusPx < 1.0) { outAo = vec4(1.0); return; }
  radiusPx = min(radiusPx, 0.25 * A.size.y);
  float noise = ign(gl_FragCoord.xy), jitter = fract(noise * 7.13);
  float vis = 0.0;
  for (int s = 0; s < SLICES; s++) {
    float phi = (float(s) + noise) * PI / float(SLICES);
    vec2 omega = vec2(cos(phi), sin(phi));
    vec3 dir = vec3(omega.x, -omega.y, 0.0);
    vec3 ortho = dir - dot(dir, V) * V;
    vec3 axis = normalize(cross(dir, V));
    vec3 projN = N - axis * dot(N, axis);
    float projLen = length(projN);
    if (projLen < 1e-4) { vis += 1.0; continue; }
    float cosN = clamp(dot(projN, V) / projLen, -1.0, 1.0);
    float n = sign(dot(ortho, projN)) * acos(cosN);
    float h[2];
    for (int side = 0; side < 2; side++) {
      float sgn = side == 0 ? -1.0 : 1.0;
      float lowest = cos(n + sgn * PI * 0.5);
      float hc = lowest;
      for (int k = 1; k <= STEPS; k++) {
        float t = pow((float(k) - 1.0 + jitter) / float(STEPS), A.params.y) * radiusPx + 1.0;
        vec2 suv = vUv + sgn * omega * vec2(1.0, 1.0) * t * A.size.zw;
        if (suv.x < 0.0 || suv.y < 0.0 || suv.x > 1.0 || suv.y > 1.0) break;
        vec3 d = viewPos(suv) - P;
        float len = length(d);
        float c = dot(d, V) / max(len, 1e-4);
        // longe demais (atrás de algo fino) conta cada vez menos: a espessura do three
        float fall = clamp(1.0 - (len - radius) / (radius * A.params.z), 0.0, 1.0);
        hc = max(hc, mix(lowest, c, fall));
      }
      h[side] = n + clamp(sgn * acos(clamp(hc, -1.0, 1.0)) - n, -PI * 0.5, PI * 0.5);
    }
    float a0 = 0.25 * (-cos(2.0 * h[0] - n) + cosN + 2.0 * h[0] * sin(n));
    float a1 = 0.25 * (-cos(2.0 * h[1] - n) + cosN + 2.0 * h[1] * sin(n));
    vis += projLen * (a0 + a1);
  }
  float ao = clamp(vis / float(SLICES), 0.0, 1.0);
  outAo = vec4(ao, ao, ao, 1.0);
}
