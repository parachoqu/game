#version 450
// Materiais do pacote (MeshStandardMaterial/MeshPhysicalMaterial, MeshLambertMaterial, MeshBasicMaterial)
// com os recursos de engine/materials.js: tri, occ, water, shore, anomalia (world-materials.js).
// O relevo (splat) fica em terrain.frag.
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"

layout(std140, set = 3, binding = 0) uniform Frame { FRAME_BLOCK } F;
layout(std140, set = 3, binding = 1) uniform Material {
  vec4 color;     // rgb linear; a: opacidade
  vec4 emissive;  // rgb (× intensidade)
  vec4 pbr;       // x: rugosidade; y: metal; z: alphaTest
  vec4 tri;       // x: uScale; y: uNStr; zw: normalScale
  vec4 tint;
  vec4 tscale[3];
  ivec4 flags;    // x: recursos; y: modelo (0 padrão, 1 Lambert, 2 básico); z: mapas; w: 1 chapado, 2 dois lados, 4 transparente
} M;

#include "pbr.glsl"

layout(set = 2, binding = 0) uniform sampler2D tMap;     // map ou uAlb (tri)
layout(set = 2, binding = 1) uniform sampler2D tNormal;  // normalMap, uNrm (tri) ou uWaterN
layout(set = 2, binding = 2) uniform sampler2D tRough;
layout(set = 2, binding = 3) uniform sampler2D tMetal;
layout(set = 2, binding = 4) uniform sampler2D tAo;
layout(set = 2, binding = 5) uniform sampler2D tEnv;
layout(set = 2, binding = 6) uniform sampler2D tDfg;
layout(set = 2, binding = 7) uniform sampler2DShadow tShadow;

layout(location = 0) in vec3 vWorld;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUv;
layout(location = 3) in vec4 vColor;
layout(location = 4) in vec4 vSplatA;
layout(location = 5) in vec4 vSplatB;
layout(location = 6) in vec2 vSplatC;
layout(location = 7) in float vDepth;
layout(location = 8) in vec3 vWNormal;

layout(location = 0) out vec4 outColor;

const int FEAT_TRI = 1, FEAT_OCC = 2, FEAT_WATER = 32, FEAT_SHORE = 64, FEAT_ANOMALY = 128;
const int MAP_ALBEDO = 1, MAP_NORMAL = 2, MAP_ROUGH = 4, MAP_METAL = 8, MAP_AO = 16;

// perturbNormal2Arb do three (sem tangentes). dFdy com o sinal do WebGL (y para cima).
mat3 tangentFrame(vec3 eyePos, vec3 n, vec2 uv) {
  vec3 q0 = dFdx(eyePos), q1 = -dFdy(eyePos);
  vec2 st0 = dFdx(uv), st1 = -dFdy(uv);
  vec3 q1perp = cross(q1, n), q0perp = cross(n, q0);
  vec3 T = q1perp * st0.x + q0perp * st1.x;
  vec3 B = q1perp * st0.y + q0perp * st1.y;
  float det = max(dot(T, T), dot(B, B));
  float scale = det == 0.0 ? 0.0 : inversesqrt(det);
  return mat3(T * scale, B * scale, n);
}

void main() {
  int feats = M.flags.x, model = M.flags.y, maps = M.flags.z, misc = M.flags.w;
  if ((feats & FEAT_OCC) != 0) occlusionDiscard(vWorld);
  float t = F.camPos.w;

  vec4 diffuseColor = M.color;
  if ((maps & MAP_ALBEDO) != 0) diffuseColor *= texture(tMap, vUv);
  // recursos de cor (inseridos depois de map_fragment)
  vec3 tw = vec3(0.0);
  vec4 tNx = vec4(0.0), tNy = vec4(0.0), tNz = vec4(0.0);
  vec2 uvX = vec2(0.0), uvY = vec2(0.0), uvZ = vec2(0.0);
  if ((feats & FEAT_TRI) != 0) {
    tw = triW(normalize(vWNormal));
    float s = M.tri.x;
    uvX = vWorld.zy * s; uvY = vWorld.xz * s; uvZ = vWorld.xy * s;
    vec4 tA = texture(tMap, uvX) * tw.x + texture(tMap, uvY) * tw.y + texture(tMap, uvZ) * tw.z;
    tNx = texture(tNormal, uvX); tNy = texture(tNormal, uvY); tNz = texture(tNormal, uvZ);
    diffuseColor.rgb *= tA.rgb;
  }
  if ((feats & FEAT_SHORE) != 0) {
    float dz = clamp(vDepth, 0.0, 2.0);
    float deep = smoothstep(0.05, 1.1, dz);
    diffuseColor.rgb = mix(diffuseColor.rgb * vec3(1.35, 1.3, 1.1) + vec3(0.02, 0.035, 0.03), diffuseColor.rgb * 0.85, deep);
    diffuseColor.a *= mix(0.16, 1.0, smoothstep(0.0, 0.55, dz));
  }
  diffuseColor *= vColor;  // color_fragment (cor de vértice e de instância)
  if (M.pbr.z > 0.0 && diffuseColor.a < M.pbr.z) discard;

  vec3 viewDir = normalize(F.camPos.xyz - vWorld);
  if (model == 2) {  // MeshBasicMaterial
    vec3 c = diffuseColor.rgb;
    outColor = vec4(applyFogExp2(c, vWorld), (misc & 4) != 0 ? diffuseColor.a : 1.0);
    return;
  }

  // normal_fragment_begin
  vec3 normal;
  if ((misc & 1) != 0) {
    normal = normalize(cross(dFdx(vWorld), dFdy(vWorld)));
    if (dot(normal, viewDir) < 0.0) normal = -normal;
  } else {
    normal = normalize(vNormal);
    if ((misc & 2) != 0 && !gl_FrontFacing) normal = -normal;
  }
  vec3 nonPerturbed = normal;

  float roughnessFactor = M.pbr.x;
  if ((maps & MAP_ROUGH) != 0) roughnessFactor *= texture(tRough, vUv).g;
  if ((feats & FEAT_TRI) != 0)
    roughnessFactor = clamp(roughnessFactor * (0.45 + (tNx.a * tw.x + tNy.a * tw.y + tNz.a * tw.z) * 1.1), 0.04, 1.0);
  float metalnessFactor = M.pbr.y;
  if ((maps & MAP_METAL) != 0) metalnessFactor *= texture(tMetal, vUv).b;

  // normal_fragment_maps
  if ((maps & MAP_NORMAL) != 0) {
    vec3 mapN = texture(tNormal, vUv).xyz * 2.0 - 1.0;
    mapN.xy *= M.tri.zw;
    normal = normalize(tangentFrame(vWorld, normal, vUv) * mapN);
  }
  if ((feats & FEAT_TRI) != 0) {
    vec3 wn = normalize(vWNormal);
    vec3 nX = tNx.xyz * 2.0 - 1.0, nY = tNy.xyz * 2.0 - 1.0, nZ = tNz.xyz * 2.0 - 1.0;
    nX.xy *= M.tri.y; nY.xy *= M.tri.y; nZ.xy *= M.tri.y;
    nX = vec3(nX.xy + wn.zy, abs(nX.z) * wn.x);
    nY = vec3(nY.xy + wn.xz, abs(nY.z) * wn.y);
    nZ = vec3(nZ.xy + wn.xy, abs(nZ.z) * wn.z);
    normal = normalize(nX.zyx * tw.x + nY.xzy * tw.y + nZ.xyz * tw.z);
  }
  if ((feats & FEAT_WATER) != 0) {
    vec2 wuv = vWorld.xz;
    vec3 n1 = texture(tNormal, wuv * 0.06 + vec2(t * 0.011, t * 0.007)).xyz * 2.0 - 1.0;
    vec3 n2 = texture(tNormal, wuv * 0.13 - vec2(t * 0.009, -t * 0.013)).xyz * 2.0 - 1.0;
    vec3 tn = normalize(vec3((n1.xy + n2.xy) * 0.22, 1.0));
    normal = normalize(vec3(tn.x, tn.z, tn.y));
  }

  vec3 emissive = M.emissive.rgb;
  if ((feats & FEAT_ANOMALY) != 0) emissive *= 0.72 + 0.38 * sin(t * 1.7);
  float ao = (maps & MAP_AO) != 0 ? texture(tAo, vUv).r : 1.0;

  float shadow = sunShadow(tShadow, vWorld, vNormal);
  vec3 light;
  if (model == 1) {
    light = shadeLambert(diffuseColor.rgb, normal, ao, shadow);
  } else {
    Physical m = makePhysical(diffuseColor.rgb, roughnessFactor, metalnessFactor, nonPerturbed);
    light = shadeStandard(m, normal, viewDir, ao, tEnv, tDfg, shadow);
  }
  light += emissive;
  outColor = vec4(applyFogExp2(light, vWorld), (misc & 4) != 0 ? diffuseColor.a : 1.0);
}
