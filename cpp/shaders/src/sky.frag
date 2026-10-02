#version 450
// Céu (engine/sky.js):
//   - fora da Turbulenta: a atmosfera de Preetham com nuvens (addon Sky do three, parâmetros de
//     `makeAtmosphere` e exposição 0,32);
//   - na Turbulenta: a cúpula estilizada (gradiente topo/horizonte/chão, brilho do sol e as faixas
//     do vazio).
// Desenhado como triângulo de tela cheia no fundo (z = w), a direção é a da vista.
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"

layout(location = 0) in vec2 vUv;
layout(location = 1) in vec2 vNdc;

layout(std140, set = 3, binding = 0) uniform Frame { FRAME_BLOCK } F;

layout(location = 0) out vec4 outColor;

const float pi = 3.141592653589793;
const vec3 up = vec3(0.0, 1.0, 0.0);
const float turbidity = 4.5, rayleigh = 1.5, mieCoefficient = 0.004, mieDirectionalG = 0.86;
const float cloudScale = 0.0002, cloudSpeed = 0.0001, cloudCoverage = 0.34, cloudDensity = 0.55, cloudElevation = 0.55;
const float skyExposure = 0.32;

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123); }
float noise(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  float a = hash(i), b = hash(i + vec2(1.0, 0.0)), c = hash(i + vec2(0.0, 1.0)), d = hash(i + vec2(1.0, 1.0));
  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
float fbm(vec2 p) {
  float value = 0.0, amplitude = 0.5;
  for (int i = 0; i < 5; i++) {
    value += amplitude * noise(p);
    p *= 2.0;
    amplitude *= 0.5;
  }
  return value;
}

vec3 atmosphere(vec3 direction, vec3 sunPosition, float time) {
  // vertex shader do Sky
  const vec3 totalRayleigh = vec3(5.804542996261093E-6, 1.3562911419845635E-5, 3.0265902468824876E-5);
  const vec3 MieConst = vec3(1.8399918514433978E14, 2.7798023919660528E14, 4.0790479543861094E14);
  const float cutoffAngle = 1.6110731556870734, steepness = 1.5, EE = 1000.0;
  vec3 vSunDirection = normalize(sunPosition);
  float zc = clamp(dot(vSunDirection, up), -1.0, 1.0);
  float vSunE = EE * max(0.0, 1.0 - pow(2.718281828459045, -((cutoffAngle - acos(zc)) / steepness)));
  float vSunfade = 1.0 - clamp(1.0 - exp(sunPosition.y / 450000.0), 0.0, 1.0);
  vec3 vBetaR = totalRayleigh * (rayleigh - (1.0 - vSunfade));
  vec3 vBetaM = 0.434 * ((0.2 * turbidity) * 10E-18) * MieConst * mieCoefficient;

  // fragment shader do Sky
  float zenithAngle = acos(max(0.0, dot(up, direction)));
  float inverse = 1.0 / (cos(zenithAngle) + 0.15 * pow(93.885 - ((zenithAngle * 180.0) / pi), -1.253));
  float sR = 8.4E3 * inverse, sM = 1.25E3 * inverse;
  vec3 Fex = exp(-(vBetaR * sR + vBetaM * sM));
  float cosTheta = dot(direction, vSunDirection);
  float rc = cosTheta * 0.5 + 0.5;
  float rPhase = 0.05968310365946075 * (1.0 + pow(rc, 2.0));
  vec3 betaRTheta = vBetaR * rPhase;
  float g2 = pow(mieDirectionalG, 2.0);
  float mPhase = 0.07957747154594767 * ((1.0 - g2) / pow(1.0 - 2.0 * mieDirectionalG * cosTheta + g2, 1.5));
  vec3 betaMTheta = vBetaM * mPhase;
  vec3 Lin = pow(vSunE * ((betaRTheta + betaMTheta) / (vBetaR + vBetaM)) * (1.0 - Fex), vec3(1.5));
  Lin *= mix(vec3(1.0), pow(vSunE * ((betaRTheta + betaMTheta) / (vBetaR + vBetaM)) * Fex, vec3(1.0 / 2.0)),
             clamp(pow(1.0 - dot(up, vSunDirection), 5.0), 0.0, 1.0));
  vec3 L0 = vec3(0.1) * Fex;
  const float sunAngularDiameterCos = 0.999956676946448443553574619906976478926848692873900859324;
  float sundisc = smoothstep(sunAngularDiameterCos, sunAngularDiameterCos + 0.00002, cosTheta);
  L0 += (vSunE * 19000.0 * Fex) * sundisc;
  vec3 texColor = (Lin + L0) * 0.04 + vec3(0.0, 0.0003, 0.00075);

  if (direction.y > 0.0 && cloudCoverage > 0.0) {
    float elevation = mix(1.0, 0.1, cloudElevation);
    vec2 cloudUV = direction.xz / (direction.y * elevation);
    cloudUV *= cloudScale;
    cloudUV += time * cloudSpeed;
    float cloudNoise = fbm(cloudUV * 1000.0);
    cloudNoise += 0.5 * fbm(cloudUV * 2000.0 + 3.7);
    cloudNoise = cloudNoise * 0.5 + 0.5;
    float cloudMask = smoothstep(1.0 - cloudCoverage, 1.0 - cloudCoverage + 0.3, cloudNoise);
    float horizonFade = smoothstep(0.0, 0.1 + 0.2 * cloudElevation, direction.y);
    cloudMask *= horizonFade;
    float sunInfluence = dot(direction, vSunDirection) * 0.5 + 0.5;
    float daylight = max(0.0, vSunDirection.y * 2.0);
    vec3 atmosphereColor = Lin * 0.04;
    vec3 cloudColor = mix(vec3(0.3), vec3(1.0), daylight);
    cloudColor = mix(cloudColor, atmosphereColor + vec3(1.0), sunInfluence * 0.5);
    cloudColor *= vSunE * 0.00002;
    texColor = mix(texColor, cloudColor, cloudMask * cloudDensity);
  }
  return texColor * skyExposure;
}

// domo da Turbulenta (domeMat)
vec3 dome(vec3 vDir, float time) {
  vec3 top = F.skyTop.rgb, horizon = F.skyHorizon.rgb, bottom = F.skyHorizon.rgb * 0.7;
  float h = vDir.y;
  vec3 c = h > 0.0 ? mix(horizon, top, pow(h, 0.55)) : mix(horizon, bottom, pow(-h, 0.35));
  float s = max(dot(normalize(vDir), normalize(F.skySun.xyz)), 0.0);
  c += vec3(1.0, 0.85, 0.6) * (pow(s, 900.0) * 3.0 + pow(s, 12.0) * 0.18) * F.skyTop.w;
  float uVoid = F.params.y;
  float band = sin(vDir.x * 9.0 + time * 0.2) * sin(vDir.z * 7.0 - time * 0.13) * sin(h * 11.0 + time * 0.1);
  c = mix(c, vec3(0.05, 0.02, 0.09), smoothstep(0.1, 0.5, band) * uVoid);
  c += vec3(0.25, 0.1, 0.45) * smoothstep(0.92, 1.0, abs(band)) * uVoid;
  return c;
}

void main() {
  vec4 far = F.invViewProj * vec4(vNdc, 1.0, 1.0);
  vec3 dir = normalize(far.xyz / far.w - F.camPos.xyz);
  float time = F.sky.x;
  vec3 c = F.params.y > 0.5 ? dome(dir, time) : atmosphere(dir, F.skySun.xyz, time);
  outColor = vec4(c, 1.0);
}
