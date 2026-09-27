// Luz do mundo (engine/renderer.js + sky.js applyDaylight), no modelo físico do three.js:
//   direta   = albedo/π · cor do sol · intensidade · max(n·l, 0)
//   indireta = albedo/π · mix(chão, céu, n.y/2 + 1/2) · intensidade do hemisfério
//            + albedo · ambiente (o HDRI do kit, aproximado pela cor média do céu)
// e névoa exponencial quadrática (THREE.FogExp2).
vec3 shade(vec3 albedo, vec3 n, vec4 sunDir, vec4 sunColor, vec4 hemiSky, vec4 hemiGround, vec3 skyAvg, float envI) {
  float ndl = max(dot(n, sunDir.xyz), 0.0);
  vec3 hemi = mix(hemiGround.rgb, hemiSky.rgb, n.y * 0.5 + 0.5) * sunColor.w;
  vec3 direct = sunColor.rgb * sunDir.w * ndl;
  return albedo * ((hemi + direct) / 3.14159265 + skyAvg * envI);
}

vec3 applyFog(vec3 color, vec3 world, vec3 cam, vec4 fog) {
  float d = length(world - cam);
  float f = 1.0 - exp(-fog.w * fog.w * d * d);
  return mix(color, fog.rgb, clamp(f, 0.0, 1.0));
}
