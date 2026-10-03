// Iluminação dos materiais do three.js r185 (MeshStandardMaterial, MeshLambertMaterial), no espaço
// do mundo (os produtos escalares não dependem do espaço):
//   direta    — sol (DirectionalLight): GGX com multiespalhamento + Lambert;
//   indireta  — hemisfério (HemisphereLight) e ambiente (scene.environment): irradiância em
//               harmônicos esféricos e reflexo do equirretangular com mip por rugosidade, no lugar
//               do PMREM; escala e viés do split-sum pela tabela DFG do próprio three.
// Requer FRAME_BLOCK como `F`.
#define PI 3.141592653589793
#define RECIPROCAL_PI 0.3183098861837907
#define EPSILON 1e-6

float pow2(float x) { return x * x; }
float pow4(float x) { float x2 = x * x; return x2 * x2; }
float saturate(float x) { return clamp(x, 0.0, 1.0); }

vec3 BRDF_Lambert(vec3 diffuseColor) { return RECIPROCAL_PI * diffuseColor; }

vec3 F_Schlick(vec3 f0, float f90, float dotVH) {
  float fresnel = exp2((-5.55473 * dotVH - 6.98316) * dotVH);
  return f0 * (1.0 - fresnel) + (f90 * fresnel);
}
float V_GGX_SmithCorrelated(float alpha, float dotNL, float dotNV) {
  float a2 = pow2(alpha);
  float gv = dotNL * sqrt(a2 + (1.0 - a2) * pow2(dotNV));
  float gl = dotNV * sqrt(a2 + (1.0 - a2) * pow2(dotNL));
  return 0.5 / max(gv + gl, EPSILON);
}
float D_GGX(float alpha, float dotNH) {
  float a2 = pow2(alpha);
  float denom = pow2(dotNH) * (a2 - 1.0) + 1.0;
  return RECIPROCAL_PI * a2 / pow2(denom);
}

struct Physical {
  vec3 diffuseColor, diffuseContribution, specularColor, specularColorBlended;
  float roughness, metalness, specularF90;
};

Physical makePhysical(vec3 diffuse, float roughnessFactor, float metalnessFactor, vec3 nonPerturbedNormal) {
  Physical m;
  m.diffuseColor = diffuse;
  m.diffuseContribution = diffuse * (1.0 - metalnessFactor);
  m.metalness = metalnessFactor;
  vec3 dxy = max(abs(dFdx(nonPerturbedNormal)), abs(dFdy(nonPerturbedNormal)));
  float geometryRoughness = max(max(dxy.x, dxy.y), dxy.z);
  m.roughness = min(max(roughnessFactor, 0.0525) + geometryRoughness, 1.0);
  m.specularColor = vec3(0.04);
  m.specularColorBlended = mix(m.specularColor, diffuse, metalnessFactor);
  m.specularF90 = 1.0;
  return m;
}

vec3 BRDF_GGX(vec3 lightDir, vec3 viewDir, vec3 normal, Physical m) {
  float alpha = pow2(m.roughness);
  vec3 halfDir = normalize(lightDir + viewDir);
  float dotNL = saturate(dot(normal, lightDir));
  float dotNV = saturate(dot(normal, viewDir));
  float dotNH = saturate(dot(normal, halfDir));
  float dotVH = saturate(dot(viewDir, halfDir));
  vec3 F = F_Schlick(m.specularColorBlended, m.specularF90, dotVH);
  return F * (V_GGX_SmithCorrelated(alpha, dotNL, dotNV) * D_GGX(alpha, dotNH));
}

vec3 BRDF_GGX_Multiscatter(vec3 lightDir, vec3 viewDir, vec3 normal, Physical m, sampler2D dfg) {
  vec3 singleScatter = BRDF_GGX(lightDir, viewDir, normal, m);
  float dotNL = saturate(dot(normal, lightDir));
  float dotNV = saturate(dot(normal, viewDir));
  vec2 dfgV = texture(dfg, vec2(m.roughness, dotNV)).rg;
  vec2 dfgL = texture(dfg, vec2(m.roughness, dotNL)).rg;
  vec3 FssEss_V = m.specularColorBlended * dfgV.x + m.specularF90 * dfgV.y;
  vec3 FssEss_L = m.specularColorBlended * dfgL.x + m.specularF90 * dfgL.y;
  float Ems_V = 1.0 - (dfgV.x + dfgV.y);
  float Ems_L = 1.0 - (dfgL.x + dfgL.y);
  vec3 Favg = m.specularColorBlended + (1.0 - m.specularColorBlended) * 0.047619;
  vec3 Fms = FssEss_V * FssEss_L * Favg / (1.0 - Ems_V * Ems_L * Favg + EPSILON);
  return singleScatter + Fms * (Ems_V * Ems_L);
}

void computeMultiscattering(vec3 normal, vec3 viewDir, vec3 specularColor, float specularF90, float roughness, sampler2D dfg,
                            inout vec3 singleScatter, inout vec3 multiScatter) {
  float dotNV = saturate(dot(normal, viewDir));
  vec2 fab = texture(dfg, vec2(roughness, dotNV)).rg;
  vec3 FssEss = specularColor * fab.x + specularF90 * fab.y;
  float Ess = fab.x + fab.y;
  float Ems = 1.0 - Ess;
  vec3 Favg = specularColor + (1.0 - specularColor) * 0.047619;
  vec3 Fms = FssEss * Favg / (1.0 - Ems * Favg);
  singleScatter += FssEss;
  multiScatter += Fms * Ems;
}

float computeSpecularOcclusion(float dotNV, float ambientOcclusion, float roughness) {
  return saturate(pow(dotNV + ambientOcclusion, exp2(-16.0 * roughness - 1.0)) - 1.0 + ambientOcclusion);
}

// ---------------------------------------------------------------- ambiente
bool envOn() { return F.params.w >= 0.0 && F.params.z > 0.0; }

vec3 shIrradiance(vec3 n) {
  vec3 e = F.sh[0].rgb * 0.282095;
  e += F.sh[1].rgb * (0.488603 * n.y) + F.sh[2].rgb * (0.488603 * n.z) + F.sh[3].rgb * (0.488603 * n.x);
  e += F.sh[4].rgb * (1.092548 * n.x * n.y) + F.sh[5].rgb * (1.092548 * n.y * n.z);
  e += F.sh[6].rgb * (0.315392 * (3.0 * n.z * n.z - 1.0)) + F.sh[7].rgb * (1.092548 * n.x * n.z);
  e += F.sh[8].rgb * (0.546274 * (n.x * n.x - n.y * n.y));
  return max(e, vec3(0.0));
}

// cube_uv_reflection_fragment `roughnessToMip` (mip = log2 do lado da face)
float roughnessToMip(float roughness) {
  if (roughness >= 0.8) return (1.0 - roughness) * (-1.0 + 2.0) / (1.0 - 0.8) - 2.0;
  if (roughness >= 0.4) return (0.8 - roughness) * (2.0 + 1.0) / (0.8 - 0.4) - 1.0;
  if (roughness >= 0.305) return (0.4 - roughness) * (3.0 - 2.0) / (0.4 - 0.305) + 2.0;
  if (roughness >= 0.21) return (0.305 - roughness) * (4.0 - 3.0) / (0.305 - 0.21) + 3.0;
  return -2.0 * log2(1.16 * roughness);
}
// Desfoque do PMREM em cada mip (sigma em radianos) → nível do equirretangular com o mesmo desfoque.
float envLod(float roughness) {
  float maxMip = F.params.w;
  float m = clamp(roughnessToMip(roughness), -2.0, maxMip);
  float sigma;
  if (m >= maxMip) sigma = 0.0;
  else if (m >= 3.0) sigma = exp2(-m);
  else {
    float t = m + 2.0;  // 0..5: mips −2..3 (EXTRA_LOD_SIGMA)
    float s0 = t < 1.0 ? 0.582 : t < 2.0 ? 0.526 : t < 3.0 ? 0.446 : t < 4.0 ? 0.35 : 0.215;
    float s1 = t < 1.0 ? 0.526 : t < 2.0 ? 0.446 : t < 3.0 ? 0.35 : t < 4.0 ? 0.215 : 0.125;
    sigma = mix(s0, s1, fract(t));
  }
  float width = exp2(maxMip) * 4.0;
  return sigma <= 0.0 ? 0.0 : max(0.0, log2(sigma * width / PI));
}
// equirectUv do three (linha 0 em v = 0, isto é, y = −1)
vec3 envSample(sampler2D env, vec3 dir, float lod) {
  vec2 uv = vec2(atan(dir.z, dir.x) * (0.5 * RECIPROCAL_PI) + 0.5, asin(clamp(dir.y, -1.0, 1.0)) * RECIPROCAL_PI + 0.5);
  return textureLod(env, uv, lod).rgb;
}
vec3 envRadiance(sampler2D env, vec3 viewDir, vec3 normal, float roughness) {
  vec3 r = reflect(-viewDir, normal);
  r = normalize(mix(r, normal, pow4(roughness)));
  return envSample(env, r, envLod(roughness)) * F.params.z;
}

// ---------------------------------------------------------------- luzes do mundo
vec3 hemiIrradiance(vec3 n) {
  return mix(F.hemiGround.rgb, F.hemiSky.rgb, 0.5 * n.y + 0.5) * F.sunColor.w;
}
vec3 sunLight() { return F.sunColor.rgb * F.sunDir.w; }

// MeshStandardMaterial
vec3 shadeStandard(Physical m, vec3 n, vec3 v, float ao, sampler2D env, sampler2D dfg) {
  vec3 l = normalize(F.sunDir.xyz);
  float dotNL = saturate(dot(n, l));
  vec3 irr = dotNL * sunLight();
  vec3 directSpecular = irr * BRDF_GGX_Multiscatter(l, v, n, m, dfg);
  vec3 directDiffuse = irr * BRDF_Lambert(m.diffuseContribution);
  vec3 indirectDiffuse = hemiIrradiance(n) * BRDF_Lambert(m.diffuseContribution);
  vec3 indirectSpecular = vec3(0.0);
  if (envOn()) {
    vec3 iblIrradiance = shIrradiance(n) * F.params.z;
    vec3 radiance = envRadiance(env, v, n, m.roughness);
    vec3 ssD = vec3(0.0), msD = vec3(0.0), ssM = vec3(0.0), msM = vec3(0.0);
    computeMultiscattering(n, v, m.specularColor, m.specularF90, m.roughness, dfg, ssD, msD);
    computeMultiscattering(n, v, m.diffuseColor, m.specularF90, m.roughness, dfg, ssM, msM);
    vec3 single = mix(ssD, ssM, m.metalness);
    vec3 multi = mix(msD, msM, m.metalness);
    vec3 diffuse = m.diffuseContribution * (1.0 - (ssD + msD));
    vec3 cosW = iblIrradiance * RECIPROCAL_PI;
    indirectSpecular = radiance * single + multi * cosW;
    indirectDiffuse += diffuse * cosW;
    if (ao < 1.0) indirectSpecular *= computeSpecularOcclusion(saturate(dot(n, v)), ao, m.roughness);
  }
  indirectDiffuse *= ao;
  return directDiffuse + indirectDiffuse + directSpecular + indirectSpecular;
}

// MeshLambertMaterial (o ambiente entra na irradiância, como no r185)
vec3 shadeLambert(vec3 diffuse, vec3 n, float ao) {
  vec3 l = normalize(F.sunDir.xyz);
  vec3 direct = saturate(dot(n, l)) * sunLight() * BRDF_Lambert(diffuse);
  vec3 irr = hemiIrradiance(n);
  if (envOn()) irr += shIrradiance(n) * F.params.z;
  return direct + irr * BRDF_Lambert(diffuse) * ao;
}

// FogExp2 sobre a profundidade de vista
vec3 applyFogExp2(vec3 color, vec3 world) {
  float depth = dot(world - F.camPos.xyz, F.camDir.xyz);
  float f = 1.0 - exp(-F.fog.w * F.fog.w * depth * depth);
  return mix(color, F.fog.rgb, saturate(f));
}

vec3 triW(vec3 n) { vec3 w = pow(abs(n), vec3(6.0)); return w / (w.x + w.y + w.z); }

// gl_FragCoord na convenção do WebGL (origem embaixo): o pontilhado do recorte usa as mesmas posições
vec2 fragCoordGL() { return vec2(gl_FragCoord.x, F.occCam.w - gl_FragCoord.y); }

// Recorte pontilhado entre a câmera e o personagem (recurso `occ`)
void occlusionDiscard(vec3 world) {
  vec3 occSeg = F.occTarget.xyz - F.occCam.xyz;
  float occL = length(occSeg);
  if (occL > 0.5) {
    vec3 occDir = occSeg / occL;
    float occT = dot(world - F.occCam.xyz, occDir);
    if (occT > 0.0 && occT < occL - 0.9) {
      float occD = length(world - (F.occCam.xyz + occDir * occT));
      if (occD < 2.4) {
        float occK = occD / 2.4;
        float occN = fract(sin(dot(fragCoordGL(), vec2(12.9898, 78.233))) * 43758.5453);
        if (occN > occK * occK) discard;
      }
    }
  }
}
