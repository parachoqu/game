#version 450
// Relevo (recurso `splat` de engine/materials.js): 10 camadas misturadas pelos pesos do vértice, com
// mistura por altura, rocha triplanar nas encostas e variação macro; depois a luz do MeshStandardMaterial.
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"

layout(std140, set = 3, binding = 0) uniform Frame { FRAME_BLOCK } F;
layout(std140, set = 3, binding = 1) uniform Material {
  vec4 color;
  vec4 emissive;
  vec4 pbr;
  vec4 tri;
  vec4 tint;      // uTint
  vec4 tscale[3]; // uTScales[10]
  ivec4 flags;
} M;

#include "pbr.glsl"

layout(set = 2, binding = 0) uniform sampler2DArray uTA;
layout(set = 2, binding = 1) uniform sampler2DArray uTN;
layout(set = 2, binding = 2) uniform sampler2D uMacro;
layout(set = 2, binding = 3) uniform sampler2D tEnv;
layout(set = 2, binding = 4) uniform sampler2D tDfg;
layout(set = 2, binding = 5) uniform sampler2DShadow tShadow;

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUv;
layout(location = 3) in vec4 vColor;
layout(location = 4) in vec4 vSA;
layout(location = 5) in vec4 vSB;
layout(location = 6) in vec2 vSC;
layout(location = 7) in float vDepth;
layout(location = 8) in vec3 vWNormal;

layout(location = 0) out vec4 outColor;

float tscale(int i) { return M.tscale[i / 4][i % 4]; }

void main() {
  vec3 swn = normalize(vWNormal);
  vec3 stw = triW(swn);
  float wts[10];
  wts[0] = vSA.x; wts[1] = vSA.y; wts[2] = vSA.z; wts[3] = vSA.w; wts[4] = vSB.x; wts[5] = vSB.y; wts[6] = vSB.z; wts[7] = vSB.w;
  wts[8] = vSC.x; wts[9] = vSC.y;
  vec3 sCol = vec3(0.0), sNrm = vec3(0.0);
  float sRough = 0.0, sSum = 0.0;
  for (int i = 0; i < 10; i++) {
    float w = wts[i];
    if (w < 0.01) continue;
    float sc = tscale(i);
    vec2 uvP = vWorld.xz * sc;
    vec4 a, n;
    if (i == 2) {  // rocha: triplanar nas encostas
      vec2 ux = vWorld.zy * sc, uz = vWorld.xy * sc, uy = uvP;
      a = texture(uTA, vec3(ux, 2.0)) * stw.x + texture(uTA, vec3(uy, 2.0)) * stw.y + texture(uTA, vec3(uz, 2.0)) * stw.z;
      vec3 nX = texture(uTN, vec3(ux, 2.0)).xyz * 2.0 - 1.0, nY = texture(uTN, vec3(uy, 2.0)).xyz * 2.0 - 1.0, nZ = texture(uTN, vec3(uz, 2.0)).xyz * 2.0 - 1.0;
      nX = vec3(nX.xy + swn.zy, abs(nX.z) * swn.x);
      nY = vec3(nY.xy + swn.xz, abs(nY.z) * swn.y);
      nZ = vec3(nZ.xy + swn.xy, abs(nZ.z) * swn.z);
      n = vec4(normalize(nX.zyx * stw.x + nY.xzy * stw.y + nZ.xyz * stw.z), texture(uTN, vec3(uy, 2.0)).a);
    } else {
      a = texture(uTA, vec3(uvP, float(i)));
      vec4 tt = texture(uTN, vec3(uvP, float(i)));
      vec3 tn = tt.xyz * 2.0 - 1.0;
      n = vec4(normalize(vec3(tn.xy + swn.xz, abs(tn.z) * swn.y).xzy), tt.a);
    }
    float hw = w * pow(a.a * 0.85 + 0.15, 2.5);  // mistura por altura
    sCol += a.rgb * hw;
    sNrm += n.xyz * hw;
    sRough += n.a * hw;
    sSum += hw;
  }
  sCol /= max(sSum, 1e-4);
  sRough /= max(sSum, 1e-4);
  float mc = texture(uMacro, vWorld.xz * 0.011).r, mc2 = texture(uMacro, vWorld.xz * 0.043 + 0.37).r;
  sCol *= mix(0.8, 1.16, mc) * mix(0.93, 1.07, mc2);
  vec3 diffuse = sCol * M.tint.rgb * vColor.rgb;  // o splat substitui a cor do material

  vec3 geomNormal = normalize(vNormal);
  float roughness = clamp(sRough, 0.05, 1.0);
  vec3 normal = normalize(sNrm + swn * 0.001);
  vec3 viewDir = normalize(F.camPos.xyz - vWorld);
  Physical m = makePhysical(diffuse, roughness, M.pbr.y, geomNormal);
  vec3 light = shadeStandard(m, normal, viewDir, 1.0, tEnv, tDfg, sunShadow(tShadow, vWorld, vNormal));
  outColor = vec4(applyFogExp2(light, vWorld), 1.0);
}
