#version 450
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"
#include "lighting.glsl"

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec4 vColor;

layout(std140, set = 3, binding = 0) uniform Frame { FRAME_BLOCK } F;

layout(location = 0) out vec4 outColor;

void main() {
  vec3 n = normalize(vNormal);
  if (!gl_FrontFacing) n = -n;
  vec3 skyAvg = mix(F.skyHorizon.rgb, F.skyTop.rgb, 0.5);
  vec3 c = shade(vColor.rgb, n, F.sunDir, F.sunColor, F.hemiSky, F.hemiGround, skyAvg, F.params.z);
  c = applyFog(c, vWorld, F.camPos.xyz, F.fog);
  outColor = vec4(c, vColor.a);
}
