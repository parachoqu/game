// Céu: cúpula com gradiente, sol/lua, campo de estrelas e constelações (nomes provisórios).
import * as THREE from 'three';
import { Sky } from '../../vendor/three/addons/objects/Sky.js';

const R = 1500;
function dirFrom(azDeg, elDeg) {
  const az = azDeg * Math.PI / 180, el = elDeg * Math.PI / 180;
  return new THREE.Vector3(Math.sin(az) * Math.cos(el), Math.sin(el), -Math.cos(az) * Math.cos(el));
}
function mulberry(seed) { return () => { seed |= 0; seed = seed + 0x6D2B79F5 | 0; let t = Math.imul(seed ^ seed >>> 15, 1 | seed); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }; }

export const CONSTELLATIONS = [
  { name: 'Lanterna', az: 352, el: 20, pts: [[0, 0], [3, 4], [6.5, 1.5], [5, -3], [1, -3.5], [3.2, 0.4]], lines: [[0, 1], [1, 2], [2, 3], [3, 4], [4, 0], [1, 5], [5, 3]] },
  { name: 'Âncora', az: 24, el: 44, pts: [[0, 0], [0, 5], [0, 10], [-4.5, 1.5], [4.5, 1.5], [-2.5, -1.8], [2.5, -1.8]], lines: [[0, 1], [1, 2], [0, 5], [5, 3], [0, 6], [6, 4]] },
  { name: 'Cervo', az: 82, el: 28, pts: [[0, 0], [4, 1], [8, 0], [9, 4], [11, 7], [7, 5], [2, -3], [6, -3]], lines: [[0, 1], [1, 2], [2, 3], [3, 4], [3, 5], [0, 6], [2, 7]] },
  { name: 'Balança', az: 300, el: 52, pts: [[0, 0], [-5, -1], [5, -1], [-6, -4], [6, -4], [0, 4]], lines: [[0, 1], [0, 2], [1, 3], [2, 4], [0, 5]] },
];

export function createSky(scene, renderer) {
  const group = new THREE.Group(); scene.add(group);

  const domeMat = new THREE.ShaderMaterial({
    uniforms: {
      top: { value: new THREE.Color('#5d8fc2') }, horizon: { value: new THREE.Color('#cfe0e6') },
      bottom: { value: new THREE.Color('#8a9aa0') }, sunDir: { value: new THREE.Vector3(0, 1, 0) },
      sunAmt: { value: 1 }, uVoid: { value: 0 }, uTime: { value: 0 },
    },
    vertexShader: 'varying vec3 vDir; void main(){ vDir = normalize(position); gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }',
    fragmentShader: `uniform vec3 top; uniform vec3 horizon; uniform vec3 bottom; uniform vec3 sunDir; uniform float sunAmt; uniform float uVoid; uniform float uTime; varying vec3 vDir;
      void main(){
        float h = vDir.y;
        vec3 c = h > 0.0 ? mix(horizon, top, pow(h, 0.55)) : mix(horizon, bottom, pow(-h, 0.35));
        float s = max(dot(normalize(vDir), normalize(sunDir)), 0.0);
        c += vec3(1.0, 0.85, 0.6) * (pow(s, 900.0) * 3.0 + pow(s, 12.0) * 0.18) * sunAmt;
        if (uVoid > 0.0) {
          float band = sin(vDir.x * 9.0 + uTime * 0.2) * sin(vDir.z * 7.0 - uTime * 0.13) * sin(h * 11.0 + uTime * 0.1);
          c = mix(c, vec3(0.05, 0.02, 0.09), smoothstep(0.1, 0.5, band) * uVoid);
          c += vec3(0.25, 0.1, 0.45) * smoothstep(0.92, 1.0, abs(band)) * uVoid;
        }
        gl_FragColor = vec4(c, 1.0);
        #include <tonemapping_fragment>
        #include <colorspace_fragment>
      }`,
    side: THREE.BackSide, depthWrite: false, fog: false,
  });
  const dome = new THREE.Mesh(new THREE.SphereGeometry(R + 200, 32, 16), domeMat);
  dome.renderOrder = -10; dome.frustumCulled = false; group.add(dome);
  dome.visible = false;                       // domo estilizado: só na Região Turbulenta
  const atm = makeAtmosphere(2000);           // céu realista de dia, entardecer e noite
  atm.renderOrder = -11; group.add(atm);

  // Estrelas
  const rnd = mulberry(42);
  const pos = [], size = [], bright = [], phase = [];
  const notable = [];   // {index, constellation, name}
  const addStar = (v, s, b) => { pos.push(v.x * R, v.y * R, v.z * R); size.push(s); bright.push(b); phase.push(rnd()); return pos.length / 3 - 1; };
  for (let i = 0; i < 1700; i++) {
    const y = -0.08 + rnd() * 1.08, a = rnd() * Math.PI * 2, r = Math.sqrt(1 - y * y);
    addStar(new THREE.Vector3(Math.cos(a) * r, y, Math.sin(a) * r), 1 + rnd() * 1.6, 0.35 + rnd() * 0.5);
  }
  const constLines = [];
  CONSTELLATIONS.forEach((c, ci) => {
    c.indices = c.pts.map(([dx, dy], k) => {
      const idx = addStar(dirFrom(c.az + dx, c.el + dy), 3.6 + (k % 3) * 0.5, 1);
      notable.push({ index: idx, constellation: ci });
      return idx;
    });
    for (const [a, b] of c.lines) constLines.push(c.indices[a], c.indices[b]);
  });
  // estrela reservada para a tela de título (não entra na ordem do jogo)
  const titleStar = addStar(dirFrom(346, 30), 4.4, 1);
  const isolated = [];
  for (let i = 0; i < 12; i++) {
    const idx = addStar(dirFrom(rnd() * 360, 12 + rnd() * 55), 3.4, 1);
    notable.push({ index: idx, constellation: -1 }); isolated.push(idx);
  }
  const n = pos.length / 3;
  const geo = new THREE.BufferGeometry();
  geo.setAttribute('position', new THREE.Float32BufferAttribute(pos, 3));
  geo.setAttribute('aSize', new THREE.Float32BufferAttribute(size, 1));
  geo.setAttribute('aBright', new THREE.Float32BufferAttribute(bright, 1));
  geo.setAttribute('aPhase', new THREE.Float32BufferAttribute(phase, 1));
  const gone = new Float32Array(n);
  geo.setAttribute('aGone', new THREE.BufferAttribute(gone, 1));
  const starMat = new THREE.ShaderMaterial({
    uniforms: { uNight: { value: 0 }, uTime: { value: 0 }, uPixel: { value: renderer.getPixelRatio() } },
    vertexShader: `attribute float aSize; attribute float aBright; attribute float aPhase; attribute float aGone;
      uniform float uNight; uniform float uTime; uniform float uPixel; varying float vA;
      void main(){ vec4 mv = modelViewMatrix * vec4(position, 1.0); gl_Position = projectionMatrix * mv;
        float tw = 0.78 + 0.22 * sin(uTime * (1.5 + aPhase * 2.0) + aPhase * 6.28);
        vA = aBright * uNight * (1.0 - aGone) * tw; gl_PointSize = aSize * uPixel * (1.0 + aGone * 0.0); }`,
    fragmentShader: `varying float vA; void main(){ vec2 d = gl_PointCoord - 0.5; float r = length(d);
      float a = smoothstep(0.5, 0.05, r); if (a * vA < 0.01) discard; gl_FragColor = vec4(1.0, 0.96, 0.88, a * vA); }`,
    transparent: true, depthWrite: false, blending: THREE.AdditiveBlending, fog: false,
  });
  const stars = new THREE.Points(geo, starMat);
  stars.frustumCulled = false; stars.renderOrder = -9; group.add(stars);

  const lineGeo = new THREE.BufferGeometry().setFromPoints(constLines.map((i) => new THREE.Vector3(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2])));
  const lineMat = new THREE.LineBasicMaterial({ color: '#9fd0ff', transparent: true, opacity: 0, fog: false, depthWrite: false });
  const lines = new THREE.LineSegments(lineGeo, lineMat);
  lines.frustumCulled = false; lines.renderOrder = -8; group.add(lines);

  // anel que marca a lacuna observada
  const gapMark = new THREE.Mesh(new THREE.RingGeometry(34, 40, 40), new THREE.MeshBasicMaterial({ color: '#f2c86a', transparent: true, opacity: 0, fog: false, depthWrite: false, side: THREE.DoubleSide }));
  gapMark.renderOrder = -7; group.add(gapMark);

  const moon = new THREE.Mesh(new THREE.SphereGeometry(38, 16, 12), new THREE.MeshBasicMaterial({ color: '#e8e4d8', fog: false }));
  moon.renderOrder = -8; group.add(moon);

  // ordem de desaparecimento: isoladas primeiro, depois pontos de constelações
  const order = [];
  const cstars = CONSTELLATIONS.map((c) => [...c.indices]);
  const pickOrder = [[0, 3], [-1, 0], [1, 4], [-1, 1], [0, 1], [-1, 2], [2, 2], [-1, 3], [3, 0], [1, 6], [0, 5], [-1, 4], [2, 5], [3, 3], [-1, 5], [1, 2], [2, 7], [0, 0]];
  for (const [ci, k] of pickOrder) order.push(ci < 0 ? { index: isolated[k], constellation: -1 } : { index: cstars[ci][k], constellation: ci });

  const flicker = [];   // {index, t}
  const sky = {
    group, domeMat, starMat, lineMat, gapMark, moon, order, gone, geo, notable, titleStar, dome, atm,
    starDir(index) { return new THREE.Vector3(pos[index * 3], pos[index * 3 + 1], pos[index * 3 + 2]).normalize(); },
    vanish(index, instant = false) {
      if (instant) { gone[index] = 1; geo.attributes.aGone.needsUpdate = true; return; }
      flicker.push({ index, t: 0 });
    },
    markGap(index, on) {
      if (!on) { gapMark.material.opacity = 0; return; }
      const d = sky.starDir(index);
      gapMark.position.copy(d).multiplyScalar(R - 20);
      gapMark.lookAt(0, 0, 0);
      gapMark.material.opacity = 0.9;
    },
    update(dt, time, camPos) {
      group.position.copy(camPos);
      starMat.uniforms.uTime.value = time;
      domeMat.uniforms.uTime.value = time;
      atm.material.uniforms.time.value = time;
      for (let i = flicker.length - 1; i >= 0; i--) {
        const f = flicker[i]; f.t += dt;
        gone[f.index] = f.t < 3 ? (Math.sin(f.t * 22) * 0.5 + 0.5) * (f.t / 3) : 1;
        if (f.t >= 3) flicker.splice(i, 1);
        geo.attributes.aGone.needsUpdate = true;
      }
    },
  };
  return sky;
}

// Céu Preetham com nuvens (addon do Three.js); a exposição própria evita o céu estourado sob ACES
function makeAtmosphere(size) {
  const a = new Sky();
  a.scale.setScalar(size);
  a.frustumCulled = false;
  const u = a.material.uniforms;
  u.turbidity.value = 4.5; u.rayleigh.value = 1.5; u.mieCoefficient.value = 0.004; u.mieDirectionalG.value = 0.86;
  u.cloudCoverage.value = 0.34; u.cloudDensity.value = 0.55; u.cloudElevation.value = 0.55;
  u.skyExposure = { value: 0.32 };
  a.material.fragmentShader = 'uniform float skyExposure;\n' + a.material.fragmentShader.replace('gl_FragColor = vec4( texColor, 1.0 );', 'gl_FragColor = vec4( texColor * skyExposure, 1.0 );');
  a.material.fog = false;
  return a;
}

// Ambiente externo (o HDRI reduzido do kit de natureza), usado enquanto o sol está alto.
let kitEnv = null;
export function setKitEnvironment(env) { kitEnv = env; envKey = ''; }

// Luz de ambiente (IBL) gerada do próprio céu; só é refeita quando o sol anda uns ~2°
let pmrem = null, envScene = null, envSky = null, envRT = null, envKey = '';
function updateEnvironment(renderer, scene, sunDir, turbulent, day = 1) {
  const useKit = !turbulent && kitEnv && day > 0.55;
  const key = turbulent ? 'turb' : useKit ? 'kit' : `${Math.round(sunDir.x * 30)},${Math.round(sunDir.y * 30)},${Math.round(sunDir.z * 30)}`;
  if (key === envKey) return;
  envKey = key;
  if (turbulent) { scene.environment = null; return; }
  if (useKit) { scene.environment = kitEnv.texture; return; }
  if (!pmrem) { pmrem = new THREE.PMREMGenerator(renderer); envScene = new THREE.Scene(); envSky = makeAtmosphere(20); envSky.material.uniforms.cloudCoverage.value = 0; envScene.add(envSky); }
  envSky.material.uniforms.sunPosition.value.copy(sunDir);
  const rt = pmrem.fromScene(envScene, 0, 0.1, 100);
  if (envRT) envRT.dispose();
  envRT = rt;
  scene.environment = rt.texture;
}

// As três cores dominantes do HDRI do kit deslocam a paleta do dia na direção da luz dele, sem
// apagar o azul do céu nem interferir no anoitecer.
let calibrated = false;
export function calibrateDaylight({ zenith, horizon, ground } = {}) {
  if (calibrated) return;
  calibrated = true;
  if (zenith) P.dayTop.lerp(new THREE.Color(zenith), 0.34);
  if (horizon) P.dayHor.lerp(new THREE.Color(horizon), 0.34);
  if (ground) P.dayGround.lerp(new THREE.Color(ground), 0.5);
}

// Paleta do céu ao longo do dia
const P = {
  dayTop: new THREE.Color('#4f86c0'), dayHor: new THREE.Color('#bccbd6'),
  dayGround: new THREE.Color('#4d4234'),
  duskTop: new THREE.Color('#3a4f7a'), duskHor: new THREE.Color('#d8a484'),
  nightTop: new THREE.Color('#070c1a'), nightHor: new THREE.Color('#1b2940'),
  turbTop: new THREE.Color('#150a24'), turbHor: new THREE.Color('#4a2c6a'),
};
const cTop = new THREE.Color(), cHor = new THREE.Color();
export function applyDaylight(ctx, hour, playerPos, turbulent) {
  const { sky, sun, hemi, scene } = ctx;
  const a = (hour - 6) / 24 * Math.PI * 2;           // 6h nascer, 18h pôr
  const elev = Math.sin(a);
  const sunDir = new THREE.Vector3(Math.cos(a) * 0.75, elev, -0.45).normalize();
  const day = THREE.MathUtils.smoothstep(elev, -0.12, 0.25);
  const dusk = Math.max(0, 1 - Math.abs(elev) / 0.3) * (elev > -0.25 ? 1 : 0);
  const night = 1 - THREE.MathUtils.smoothstep(elev, -0.22, 0.02);

  cTop.copy(P.nightTop).lerp(P.dayTop, day); cHor.copy(P.nightHor).lerp(P.dayHor, day);
  cTop.lerp(P.duskTop, dusk * 0.5); cHor.lerp(P.duskHor, dusk * 0.6);
  if (turbulent) { cTop.copy(P.turbTop); cHor.copy(P.turbHor); }
  sky.domeMat.uniforms.top.value.copy(cTop);
  sky.domeMat.uniforms.horizon.value.copy(cHor);
  sky.domeMat.uniforms.bottom.value.copy(cHor).multiplyScalar(0.7);
  sky.domeMat.uniforms.sunDir.value.copy(sunDir);
  sky.domeMat.uniforms.sunAmt.value = turbulent ? 0 : day;
  sky.domeMat.uniforms.uVoid.value = turbulent ? 1 : 0;
  sky.starMat.uniforms.uNight.value = turbulent ? 0.25 : Math.max(0.04, night);
  sky.moon.position.copy(sunDir).multiplyScalar(-1300);
  sky.moon.visible = !turbulent && night > 0.2;
  sky.dome.visible = !!turbulent;
  sky.atm.visible = !turbulent;
  sky.atm.material.uniforms.sunPosition.value.copy(sunDir);
  if (ctx.renderer) updateEnvironment(ctx.renderer, scene, sunDir, turbulent, day);
  scene.environmentIntensity = turbulent ? 0 : 0.25 + day * 0.4;

  // luz: sol de dia, luar azulado à noite
  const lightDir = elev > -0.05 ? sunDir : sunDir.clone().multiplyScalar(-1);
  sun.position.copy(playerPos).addScaledVector(lightDir, 160);
  sun.target.position.copy(playerPos);
  sun.color.set(turbulent ? '#b89aff' : day > 0.5 ? '#fff0da' : dusk > 0.3 ? '#ffbf85' : '#a9bde8');
  sun.intensity = turbulent ? 1.5 : 0.95 + day * 1.35;
  hemi.intensity = turbulent ? 1.1 : 0.75 - day * 0.25;
  hemi.color.set(turbulent ? '#b8a4e8' : day > 0.4 ? '#d4e6ff' : '#8497c8');
  hemi.groundColor.set(turbulent ? '#3a2a4a' : '#ffffff');
  if (!turbulent) hemi.groundColor.copy(P.dayGround);
  // névoa exponencial: perspectiva aérea (o distante fica azulado)
  scene.fog.color.copy(cHor);
  scene.fog.density = turbulent ? 0.012 : 0.0011 + night * 0.0005;
  return { night, day, elev };
}
