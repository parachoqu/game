#version 450
// Lâmina d'água do rio: cor com fresnel contra o céu, reflexo do sol e ondulação procedural.
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
#include "lighting.glsl"

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vColor;

layout(std140, set = 3, binding = 0) uniform Frame { FRAME_BLOCK } F;

layout(location = 0) out vec4 outColor;

void main() {
  float t = F.camPos.w;
  vec2 p = vWorld.xz;
  vec3 n = normalize(vec3(sin(p.x * 0.9 + t * 1.3) * 0.05 + sin(p.y * 1.7 - t * 0.9) * 0.04, 1.0,
                          cos(p.y * 0.8 + t * 1.1) * 0.05 + cos(p.x * 1.3 + t * 0.7) * 0.04));
  vec3 v = normalize(F.camPos.xyz - vWorld);
  float fres = 0.04 + 0.96 * pow(1.0 - max(dot(n, v), 0.0), 5.0);
  vec3 sky = mix(F.skyHorizon.rgb, F.skyTop.rgb, 0.35);
  vec3 base = shade(vColor.rgb, n, F.sunDir, F.sunColor, F.hemiSky, F.hemiGround, sky, F.params.z);
  vec3 h = normalize(F.sunDir.xyz + v);
  float spec = pow(max(dot(n, h), 0.0), 180.0) * F.sunDir.w * F.skyTop.w;
  vec3 c = mix(base, sky, fres) + F.sunColor.rgb * spec;
  c = applyFog(c, vWorld, F.camPos.xyz, F.fog);
  outColor = vec4(c, mix(vColor.a, 1.0, fres * 0.6));
}
