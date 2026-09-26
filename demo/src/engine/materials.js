// Materiais do mapa (PBR com texturas procedurais) e o material estilizado dos personagens.
// Os recursos de shader são "patches" compostos sobre os shaders do Three.js:
//   occ   — recorte pontilhado entre a câmera e o personagem
//   tri   — projeção triplanar em espaço de mundo (albedo, normal, rugosidade); dispensa UV
//   splat — terreno: mistura de 10 camadas por pesos de vértice, com mistura por altura
//   wind  — balanço de folhagem;  grass — vento forte + some com a distância
//   water — duas normais animadas em espaço de mundo;  shore — raso claro e margem sem recorte
import * as THREE from 'three';
import { TEX } from './textures.js';

export const OCC = { uCam: { value: new THREE.Vector3() }, uTarget: { value: new THREE.Vector3() } };
export const U = { uTime: { value: 0 }, uGrassFar: { value: 70 } };

const HEAD = `varying vec3 vWPos;
varying vec3 vWNormal;
vec3 triW(vec3 n) { vec3 w = pow(abs(n), vec3(6.0)); return w / (w.x + w.y + w.z); }
`;
const WORLD_VARY = `#include <worldpos_vertex>
  vec4 wpP = vec4(transformed, 1.0);
  #ifdef USE_INSTANCING
    wpP = instanceMatrix * wpP;
  #endif
  vWPos = (modelMatrix * wpP).xyz;
  vec3 wpN = objectNormal;
  #ifdef USE_INSTANCING
    wpN = mat3(instanceMatrix) * wpN;
  #endif
  vWNormal = normalize(mat3(modelMatrix) * wpN);`;

const FEAT = {
  occ: {
    uniforms: () => ({ uCam: OCC.uCam, uTarget: OCC.uTarget }),
    fsHead: 'uniform vec3 uCam; uniform vec3 uTarget;\n',
    fsMain: `
      vec3 occSeg = uTarget - uCam; float occL = length(occSeg);
      if (occL > 0.5) {
        vec3 occDir = occSeg / occL; float occT = dot(vWPos - uCam, occDir);
        if (occT > 0.0 && occT < occL - 0.9) {
          float occD = length(vWPos - (uCam + occDir * occT));
          if (occD < 2.4) {
            float occK = occD / 2.4;
            float occN = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
            if (occN > occK * occK) discard;
          }
        }
      }`,
  },
  tri: {
    fsHead: 'uniform sampler2D uAlb; uniform sampler2D uNrm; uniform float uScale; uniform float uNStr;\n',
    map: `
      vec3 tw = triW(normalize(vWNormal));
      vec2 uvX = vWPos.zy * uScale, uvY = vWPos.xz * uScale, uvZ = vWPos.xy * uScale;
      vec4 tA = texture2D(uAlb, uvX) * tw.x + texture2D(uAlb, uvY) * tw.y + texture2D(uAlb, uvZ) * tw.z;
      vec4 tNx = texture2D(uNrm, uvX), tNy = texture2D(uNrm, uvY), tNz = texture2D(uNrm, uvZ);
      diffuseColor.rgb *= tA.rgb;`,
    rough: 'roughnessFactor = clamp(roughnessFactor * (0.45 + (tNx.a * tw.x + tNy.a * tw.y + tNz.a * tw.z) * 1.1), 0.04, 1.0);',
    normal: `{
      vec3 wn = normalize(vWNormal);
      vec3 nX = tNx.xyz * 2.0 - 1.0, nY = tNy.xyz * 2.0 - 1.0, nZ = tNz.xyz * 2.0 - 1.0;
      nX.xy *= uNStr; nY.xy *= uNStr; nZ.xy *= uNStr;
      nX = vec3(nX.xy + wn.zy, abs(nX.z) * wn.x);
      nY = vec3(nY.xy + wn.xz, abs(nY.z) * wn.y);
      nZ = vec3(nZ.xy + wn.xy, abs(nZ.z) * wn.z);
      vec3 wN = normalize(nX.zyx * tw.x + nY.xzy * tw.y + nZ.xyz * tw.z);
      normal = normalize((viewMatrix * vec4(wN, 0.0)).xyz);
    }`,
  },
  splat: {
    vsHead: 'attribute vec4 splatA; attribute vec4 splatB; attribute vec2 splatC; varying vec4 vSA; varying vec4 vSB; varying vec2 vSC;\n',
    vsBegin: 'vSA = splatA; vSB = splatB; vSC = splatC;',
    fsHead: `uniform highp sampler2DArray uTA; uniform highp sampler2DArray uTN; uniform sampler2D uMacro; uniform float uTScales[10]; uniform vec3 uTint;
varying vec4 vSA; varying vec4 vSB; varying vec2 vSC;\n`,
    map: `
      vec3 swn = normalize(vWNormal);
      vec3 stw = triW(swn);
      float wts[10];
      wts[0] = vSA.x; wts[1] = vSA.y; wts[2] = vSA.z; wts[3] = vSA.w; wts[4] = vSB.x; wts[5] = vSB.y; wts[6] = vSB.z; wts[7] = vSB.w;
      wts[8] = vSC.x; wts[9] = vSC.y;
      vec3 sCol = vec3(0.0), sNrm = vec3(0.0); float sRough = 0.0, sSum = 0.0;
      for (int i = 0; i < 10; i++) {
        float w = wts[i];
        if (w < 0.01) continue;
        vec2 uvP = vWPos.xz * uTScales[i];
        vec4 a, n;
        if (i == 2) {   // rocha: triplanar nas encostas
          vec2 ux = vWPos.zy * uTScales[i], uz = vWPos.xy * uTScales[i], uy = uvP;
          a = texture(uTA, vec3(ux, 2.0)) * stw.x + texture(uTA, vec3(uy, 2.0)) * stw.y + texture(uTA, vec3(uz, 2.0)) * stw.z;
          vec3 nX = texture(uTN, vec3(ux, 2.0)).xyz * 2.0 - 1.0, nY = texture(uTN, vec3(uy, 2.0)).xyz * 2.0 - 1.0, nZ = texture(uTN, vec3(uz, 2.0)).xyz * 2.0 - 1.0;
          nX = vec3(nX.xy + swn.zy, abs(nX.z) * swn.x); nY = vec3(nY.xy + swn.xz, abs(nY.z) * swn.y); nZ = vec3(nZ.xy + swn.xy, abs(nZ.z) * swn.z);
          n = vec4(normalize(nX.zyx * stw.x + nY.xzy * stw.y + nZ.xyz * stw.z), texture(uTN, vec3(uy, 2.0)).a);
        } else {
          a = texture(uTA, vec3(uvP, float(i)));
          vec4 t = texture(uTN, vec3(uvP, float(i)));
          vec3 tn = t.xyz * 2.0 - 1.0;
          n = vec4(normalize(vec3(tn.xy + swn.xz, abs(tn.z) * swn.y).xzy), t.a);
        }
        float hw = w * pow(a.a * 0.85 + 0.15, 2.5);   // mistura por altura: pedras e tufos aparecem nas transições
        sCol += a.rgb * hw; sNrm += n.xyz * hw; sRough += n.a * hw; sSum += hw;
      }
      sCol /= max(sSum, 1e-4); sRough /= max(sSum, 1e-4);
      float mc = texture2D(uMacro, vWPos.xz * 0.011).r, mc2 = texture2D(uMacro, vWPos.xz * 0.043 + 0.37).r;
      sCol *= mix(0.8, 1.16, mc) * mix(0.93, 1.07, mc2);
      diffuseColor.rgb = sCol * uTint;`,
    rough: 'roughnessFactor = clamp(sRough, 0.05, 1.0);',
    normal: 'normal = normalize((viewMatrix * vec4(normalize(sNrm + swn * 0.001), 0.0)).xyz);',
  },
  wind: {
    uniforms: () => ({ uTime: U.uTime }),
    vsHead: 'uniform float uTime; uniform float uWind;\n',
    begin: `{
      vec3 ip = vec3(0.0);
      #ifdef USE_INSTANCING
        ip = instanceMatrix[3].xyz;
      #endif
      float ph = uTime * 1.3 + dot(ip.xz, vec2(0.13, 0.17));
      float amp = uWind * max(transformed.y - 0.6, 0.0);
      transformed.x += (sin(ph) * 0.05 + sin(ph * 2.7 + transformed.y * 1.3) * 0.02) * amp;
      transformed.z += (cos(ph * 0.8) * 0.04 + sin(ph * 3.1 + transformed.x) * 0.015) * amp;
    }`,
  },
  grass: {
    uniforms: () => ({ uTime: U.uTime, uGrassFar: U.uGrassFar }),
    vsHead: 'uniform float uTime; uniform float uGrassFar;\n',
    begin: `{
      vec3 ip = vec3(0.0);
      #ifdef USE_INSTANCING
        ip = instanceMatrix[3].xyz;
      #endif
      float fade = 1.0 - smoothstep(uGrassFar * 0.65, uGrassFar, distance(ip.xz, cameraPosition.xz));
      float ph = uTime * 2.0 + dot(ip.xz, vec2(0.21, 0.29));
      float amp = transformed.y * transformed.y;
      transformed.x += (sin(ph) * 0.12 + sin(ph * 2.3) * 0.05) * amp;
      transformed.z += cos(ph * 0.7) * 0.08 * amp;
      transformed *= fade;
    }`,
  },
  water: {
    uniforms: () => ({ uTime: U.uTime }),
    fsHead: 'uniform sampler2D uWaterN; uniform float uTime;\n',
    normal: `{
      vec2 wuv = vWPos.xz;
      vec3 n1 = texture2D(uWaterN, wuv * 0.06 + vec2(uTime * 0.011, uTime * 0.007)).xyz * 2.0 - 1.0;
      vec3 n2 = texture2D(uWaterN, wuv * 0.13 - vec2(uTime * 0.009, -uTime * 0.013)).xyz * 2.0 - 1.0;
      vec3 tn = normalize(vec3((n1.xy + n2.xy) * 0.22, 1.0));
      normal = normalize((viewMatrix * vec4(normalize(vec3(tn.x, tn.z, tn.y)), 0.0)).xyz);
    }`,
  },
  // margem do rio: a profundidade local (atributo por vértice) clareia e abre o raso, e a água some
  // antes de encostar no barranco — sem a linha dura de uma lâmina recortada
  shore: {
    vsHead: 'attribute float aDepth; varying float vDepth;\n',
    vsBegin: 'vDepth = aDepth;',
    fsHead: 'varying float vDepth;\n',
    map: `{
      float dz = clamp(vDepth, 0.0, 2.0);
      float deep = smoothstep(0.05, 1.1, dz);
      diffuseColor.rgb = mix(diffuseColor.rgb * vec3(1.35, 1.3, 1.1) + vec3(0.02, 0.035, 0.03), diffuseColor.rgb * 0.85, deep);
      diffuseColor.a *= mix(0.16, 1.0, smoothstep(0.0, 0.55, dz));
    }`,
  },
};

// aplica um conjunto de recursos a um material do Three
export function patchMaterial(m, feats, uniforms = {}) {
  const key = feats.join('+');
  m.customProgramCacheKey = () => key;
  m.onBeforeCompile = (sh) => {
    for (const f of feats) if (FEAT[f].uniforms) Object.assign(sh.uniforms, FEAT[f].uniforms());
    Object.assign(sh.uniforms, uniforms);
    let vs = sh.vertexShader, fs = sh.fragmentShader;
    vs = HEAD + feats.map((f) => FEAT[f].vsHead || '').join('') + vs;
    fs = HEAD + feats.map((f) => FEAT[f].fsHead || '').join('') + fs;
    const begin = feats.map((f) => FEAT[f].begin || '').join('\n');
    const vsBegin = feats.map((f) => FEAT[f].vsBegin || '').join('\n');
    vs = vs.replace('#include <begin_vertex>', `#include <begin_vertex>\n${begin}\n${vsBegin}`);
    vs = vs.replace('#include <worldpos_vertex>', WORLD_VARY);
    const main = feats.map((f) => FEAT[f].fsMain || '').join('\n');
    fs = fs.replace('void main() {', `void main() {\n${main}`);
    const map = feats.map((f) => FEAT[f].map || '').join('\n');
    if (map) fs = fs.replace('#include <map_fragment>', `#include <map_fragment>\n${map}`);
    const rough = feats.map((f) => FEAT[f].rough || '').join('\n');
    if (rough) fs = fs.replace('#include <roughnessmap_fragment>', `#include <roughnessmap_fragment>\n${rough}`);
    const normal = feats.map((f) => FEAT[f].normal || '').join('\n');
    if (normal) fs = fs.replace('#include <normal_fragment_maps>', `#include <normal_fragment_maps>\n${normal}`);
    sh.vertexShader = vs; sh.fragmentShader = fs;
  };
  return m;
}

// ---------- superfícies de arquitetura, rochas e troncos ----------
const PARAMS = {
  alvenaria: { rough: 0.9, metal: 0, nstr: 1.1 },
  reboco: { rough: 0.95, metal: 0, nstr: 0.8 },
  madeira: { rough: 0.85, metal: 0, nstr: 1.0 },
  telhas: { rough: 0.55, metal: 0, nstr: 1.3 },
  metal: { rough: 0.55, metal: 0.75, nstr: 0.6 },
  tecido: { rough: 1, metal: 0, nstr: 0.7 },
  casca: { rough: 1, metal: 0, nstr: 1.4 },
  rocha: { rough: 0.9, metal: 0, nstr: 1.3 },
  cascaNatural: { rough: 0.95, metal: 0, nstr: 0.7 },
  cascaPinheiro: { rough: 0.95, metal: 0, nstr: 0.7 },
  rochaNatural: { rough: 0.9, metal: 0, nstr: 0.85 },
  rochaEscura: { rough: 0.95, metal: 0, nstr: 0.85 },
  trilha: { rough: 0.98, metal: 0, nstr: 0.8 },
  gramaCampo: { rough: 1, metal: 0, nstr: 0.6 },
};
const cache = new Map();
export function surfaceMat(surface, color = '#ffffff', extra) {
  const k = surface + color + (extra ? JSON.stringify(extra) : '');
  if (cache.has(k)) return cache.get(k);
  const p = PARAMS[surface] || PARAMS.reboco;
  const t = TEX.surf?.[surface] || TEX.surf?.reboco || { alb: null, nrm: null, scale: 1 };
  const m = new THREE.MeshStandardMaterial({ color, roughness: p.rough, metalness: p.metal, ...extra });
  m.userData.surface = surface;   // permite juntar peças da mesma superfície levando a cor para os vértices
  if (t.alb) {
    patchMaterial(m, ['tri', 'occ'], { uAlb: { value: t.alb }, uNrm: { value: t.nrm }, uScale: { value: t.scale }, uNStr: { value: p.nstr } });
  }
  cache.set(k, m);
  return m;
}

// ---------- folhagem (cartões com transparência) ----------
export function foliageMat(tex, { wind = 1, grass = false, color = '#ffffff' } = {}) {
  const m = new THREE.MeshStandardMaterial({ map: tex || null, color, alphaTest: 0.45, side: THREE.DoubleSide, roughness: 0.85, metalness: 0 });
  if (tex?.userData?.normalMap) {
    m.normalMap = tex.userData.normalMap;
    m.normalScale.set(0.35, 0.35);
    m.roughnessMap = tex.userData.roughnessMap;
  }
  patchMaterial(m, grass ? ['grass'] : ['wind', 'occ'], { uWind: { value: wind } });
  return m;
}
export function foliageDepth(tex, wind = 1) {
  const m = new THREE.MeshDepthMaterial({ depthPacking: THREE.RGBADepthPacking, map: tex || null, alphaTest: 0.45, side: THREE.DoubleSide });
  m.customProgramCacheKey = () => 'nature-foliage-depth-wind';
  m.onBeforeCompile = (sh) => {
    Object.assign(sh.uniforms, { uTime: U.uTime, uWind: { value: wind } });
    sh.vertexShader = FEAT.wind.vsHead + sh.vertexShader.replace('#include <begin_vertex>', '#include <begin_vertex>\n' + FEAT.wind.begin);
  };
  return m;
}

// ---------- terreno e água ----------
export function terrainMat(tint = '#ffffff') {
  const m = new THREE.MeshStandardMaterial({ roughness: 1, metalness: 0 });
  patchMaterial(m, ['splat'], {
    uTA: { value: TEX.terrainAlb }, uTN: { value: TEX.terrainNrm }, uMacro: { value: TEX.macro },
    uTScales: { value: TEX.terrainScales }, uTint: { value: new THREE.Color(tint) },
  });
  return m;
}
// Água com parâmetros extraídos de UNS_Water.mat: azul-petróleo (0.22, 0.43, 0.52 = #386e85),
// smoothness 0.82 (rugosidade 0.18), transparência, reflexos de ambiente e normais animadas.
export function waterMat({ shore = false } = {}) {
  const m = new THREE.MeshStandardMaterial({
    color: '#386e85', roughness: 0.18, metalness: 0,
    transparent: true, opacity: shore ? 0.84 : 0.78, depthWrite: false, envMapIntensity: 1.0,
  });
  patchMaterial(m, shore ? ['water', 'shore'] : ['water'], { uWaterN: { value: TEX.waterNrm } });
  return m;
}

// ---------- personagens: o visual estilizado original (Lambert chapado) ----------
const charCache = new Map();
export function charMat(color, extra) {
  const k = color + (extra ? JSON.stringify(extra) : '');
  if (!charCache.has(k)) charCache.set(k, patchMaterial(new THREE.MeshLambertMaterial({ color, flatShading: true, ...extra }), ['occ']));
  return charCache.get(k);
}
