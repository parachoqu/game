#version 450
// Personagens com pele (SkinnedMesh do three, bindMode "attached"): a paleta de cada malha já vem no
// mundo (osso_mundo · inversa · bindMatrix), então a posição sai de Σ wᵢ·Pᵢ·p. A normal segue o
// skinnormal_vertex + normalMatrix do three: M⁻ᵀ·M⁻¹·(Σ wᵢ·Pᵢ)·n, com M a matriz de mundo da malha
// (na instância). Mesmas saídas de mesh.vert: o fragmento é o surface.frag.
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUv;
layout(location = 3) in vec4 inColor;
layout(location = 4) in vec4 inSplatA;
layout(location = 5) in vec4 inSplatB;
layout(location = 6) in vec4 inSplatC;
layout(location = 7) in float inDepth;
layout(location = 8) in vec4 iM0;
layout(location = 9) in vec4 iM1;
layout(location = 10) in vec4 iM2;
layout(location = 11) in vec4 iM3;
layout(location = 12) in vec4 iTint;
layout(location = 13) in uvec4 inJoints;
layout(location = 14) in vec4 inWeights;

layout(std430, set = 0, binding = 0) readonly buffer Bones { mat4 bones[]; };

layout(std140, set = 1, binding = 0) uniform Frame { FRAME_BLOCK } F;
layout(std140, set = 1, binding = 1) uniform Draw {
  vec4 wind;    // w: modo da matiz
  vec4 uvRow0;
  vec4 uvRow1;
  vec4 flags;   // x: cores de vértice; y: primeiro osso da paleta
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
  uint base = uint(D.flags.y + 0.5);
  mat4 skin = bones[base + inJoints.x] * inWeights.x + bones[base + inJoints.y] * inWeights.y +
              bones[base + inJoints.z] * inWeights.z + bones[base + inJoints.w] * inWeights.w;
  vec4 w = skin * vec4(inPos, 1.0);
  vWorld = w.xyz;
  vec3 sn = mat3(skin) * inNormal;
  // M⁻ᵀ·M⁻¹ com eixos ortogonais: Σ mᵢ (mᵢ·v) / |mᵢ|⁴
  mat3 im = mat3(iM0.xyz, iM1.xyz, iM2.xyz);
  vec3 s2 = vec3(dot(im[0], im[0]), dot(im[1], im[1]), dot(im[2], im[2]));
  vec3 d = vec3(dot(im[0], sn), dot(im[1], sn), dot(im[2], sn)) / (s2 * s2);
  vNormal = im * d;
  vWNormal = normalize(sn);
  vUv = vec2(dot(D.uvRow0.xyz, vec3(inUv, 1.0)), dot(D.uvRow1.xyz, vec3(inUv, 1.0)));
  vec4 c = D.flags.x > 0.5 ? inColor : vec4(1.0);
  if (int(D.wind.w + 0.5) == 3) c *= iTint;
  vColor = c;
  vSplatA = inSplatA;
  vSplatB = inSplatB;
  vSplatC = inSplatC.xy;
  vDepth = inDepth;
  gl_Position = F.viewProj * w;
}
