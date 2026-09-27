#version 450
// Céu (engine/sky.js): gradiente topo/horizonte, disco e halo do sol. No vazio da Turbulenta, só o
// gradiente violeta.
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec2 vNdc;

layout(std140, set = 3, binding = 0) uniform Frame { FRAME_BLOCK } F;

layout(location = 0) out vec4 outColor;

void main() {
  vec4 far = F.invViewProj * vec4(vNdc, 1.0, 1.0);
  vec3 dir = normalize(far.xyz / far.w - F.camPos.xyz);
  float h = dir.y;
  vec3 top = F.skyTop.rgb, hor = F.skyHorizon.rgb;
  vec3 c = h >= 0.0 ? mix(hor, top, pow(clamp(h, 0.0, 1.0), 0.55)) : hor * mix(1.0, 0.7, clamp(-h * 4.0, 0.0, 1.0));
  float s = max(dot(dir, normalize(F.sunDir.xyz)), 0.0);
  float sunAmt = F.skyTop.w;
  c += vec3(1.0, 0.92, 0.78) * (pow(s, 1400.0) * 30.0 + pow(s, 12.0) * 0.25) * sunAmt;
  outColor = vec4(c, 1.0);
}
