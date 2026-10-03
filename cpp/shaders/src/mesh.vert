#version 450
// Malhas do pacote visual (cenário, relevo, vegetação, modelos): vértice único (PackVertex) e uma
// instância (matriz + matiz). Recursos de vértice de engine/materials.js: `wind` e `grass`.
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inColor;   // linear
layout(location = 4) in vec4 inSplatA;
layout(location = 5) in vec4 inSplatB;
layout(location = 6) in vec4 inSplatC;  // xy: camadas 8 e 9
layout(location = 7) in float inDepth;
layout(location = 8) in vec4 iM0;
layout(location = 9) in vec4 iM1;
layout(location = 10) in vec4 iM2;
layout(location = 11) in vec4 iM3;
layout(location = 12) in vec4 iTint;    // copa (rgb) e tronco (a), ou cor rgb

layout(std140, set = 1, binding = 0) uniform Frame { FRAME_BLOCK } F;
layout(std140, set = 1, binding = 1) uniform Draw {
  vec4 wind;    // x: uWind; y: recursos (1 vento, 2 grama); z: instanciado (InstancedMesh); w: modo da matiz
  vec4 uvRow0;  // matriz da textura (Matrix3 do three), linhas 0 e 1
  vec4 uvRow1;
  vec4 flags;   // x: cores de vértice
} D;

layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUv;
layout(location = 3) out vec4 vColor;
layout(location = 4) out vec4 vSplatA;
layout(location = 5) out vec4 vSplatB;
layout(location = 6) out vec2 vSplatC;
layout(location = 7) out float vDepth;
layout(location = 8) out vec3 vWNormal;

void main() {
  mat4 M = mat4(iM0, iM1, iM2, iM3);
  vec3 transformed = inPos;
  int feats = int(D.wind.y + 0.5);
  vec3 ip = D.wind.z > 0.5 ? iM3.xyz : vec3(0.0);
  float t = F.camPos.w;
  if ((feats & 1) != 0) {  // wind
    float ph = t * 1.3 + dot(ip.xz, vec2(0.13, 0.17));
    float amp = D.wind.x * max(transformed.y - 0.6, 0.0);
    transformed.x += (sin(ph) * 0.05 + sin(ph * 2.7 + transformed.y * 1.3) * 0.02) * amp;
    transformed.z += (cos(ph * 0.8) * 0.04 + sin(ph * 3.1 + transformed.x) * 0.015) * amp;
  }
  if ((feats & 2) != 0) {  // grass
    float grassFar = F.occTarget.w;
    float fade = 1.0 - smoothstep(grassFar * 0.65, grassFar, distance(ip.xz, F.camPos.xz));
    float ph = t * 2.0 + dot(ip.xz, vec2(0.21, 0.29));
    float amp = transformed.y * transformed.y;
    transformed.x += (sin(ph) * 0.12 + sin(ph * 2.3) * 0.05) * amp;
    transformed.z += cos(ph * 0.7) * 0.08 * amp;
    transformed *= fade;
  }
  vec4 w = M * vec4(transformed, 1.0);
  vWorld = w.xyz;
  // normal de iluminação: inversa transposta (defaultnormal_vertex do three, eixos ortogonais)
  mat3 im = mat3(M);
  vNormal = im * (inNormal / vec3(dot(im[0], im[0]), dot(im[1], im[1]), dot(im[2], im[2])));
  // normal de mundo dos recursos (vWNormal de materials.js: sem a inversa transposta)
  vWNormal = normalize(im * inNormal);
  vUv = vec2(dot(D.uvRow0.xyz, vec3(inUv, 1.0)), dot(D.uvRow1.xyz, vec3(inUv, 1.0)));
  vec4 c = D.flags.x > 0.5 ? inColor : vec4(1.0);
  int tint = int(D.wind.w + 0.5);
  if (tint == 1) c.rgb *= iTint.rgb;
  else if (tint == 3) c *= iTint;  // cor livre (instâncias do bake com alfa 1; opacidade animada)
  else if (tint == 2) c.rgb *= vec3(iTint.a);
  vColor = c;
  vSplatA = inSplatA;
  vSplatB = inSplatB;
  vSplatC = inSplatC.xy;
  vDepth = inDepth;
  gl_Position = F.viewProj * w;
}
