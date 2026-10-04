#version 450
// Passada de sombra do sol: só profundidade. Recorta por alfa como o material de profundidade do
// three (MeshDepthMaterial com map e alphaTest do material original).
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"

layout(std140, set = 3, binding = 0) uniform Frame { FRAME_BLOCK } F;
layout(std140, set = 3, binding = 1) uniform Material {
  vec4 color;
  vec4 emissive;
  vec4 pbr;  // z: alphaTest
  vec4 tri;
  vec4 tint;
  vec4 tscale[3];
  ivec4 flags;  // z: mapas (1 = albedo)
} M;
layout(set = 2, binding = 0) uniform sampler2D tMap;

layout(location = 2) in vec2 vUv;
layout(location = 3) in vec4 vColor;

void main() {
  if (M.pbr.z <= 0.0) return;
  float a = M.color.a * vColor.a;
  if ((M.flags.z & 1) != 0) a *= texture(tMap, vUv).a;
  if (a < M.pbr.z) discard;
}
