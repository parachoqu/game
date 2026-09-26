import * as THREE from 'three';
import { EffectComposer } from '../../vendor/three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from '../../vendor/three/addons/postprocessing/RenderPass.js';
import { GTAOPass } from '../../vendor/three/addons/postprocessing/GTAOPass.js';
import { UnrealBloomPass } from '../../vendor/three/addons/postprocessing/UnrealBloomPass.js';
import { OutputPass } from '../../vendor/three/addons/postprocessing/OutputPass.js';
import { SMAAPass } from '../../vendor/three/addons/postprocessing/SMAAPass.js';

export function createRenderer() {
  const canvas = document.getElementById('game');
  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true, powerPreference: 'high-performance' });
  renderer.setPixelRatio(Math.min(window.devicePixelRatio || 1, 1.5));
  renderer.setSize(window.innerWidth, window.innerHeight);
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFShadowMap;
  // o mapa de sombra é desenhado uma vez por quadro (ctx.render); sem isso, cada passe que
  // redesenha a cena (o GTAO, por exemplo) refaria a sombra inteira
  renderer.shadowMap.autoUpdate = false;
  renderer.outputColorSpace = THREE.SRGBColorSpace;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.0;

  const scene = new THREE.Scene();
  // matrizes do mundo atualizadas uma vez por quadro em ctx.render (cada passe chamaria de novo)
  scene.matrixWorldAutoUpdate = false;
  // A densidade acompanha a escala do mundo: com 1.024 m de lado, 0,0024 fechava o horizonte
  // a menos de 400 m e escondia a serra, a garganta e os dois entrepostos.
  scene.fog = new THREE.FogExp2(0xbccbd6, 0.0011);
  const camera = new THREE.PerspectiveCamera(55, window.innerWidth / window.innerHeight, 0.1, 2400);

  const hemi = new THREE.HemisphereLight(0xd4e6ff, 0x4d4234, 0.6);
  const sun = new THREE.DirectionalLight(0xfff0da, 2.8);
  sun.castShadow = true;
  sun.shadow.mapSize.set(4096, 4096);
  const sc = sun.shadow.camera;
  sc.left = -70; sc.right = 70; sc.top = 70; sc.bottom = -70; sc.near = 1; sc.far = 340;
  sun.shadow.bias = -0.0004; sun.shadow.normalBias = 0.05; sun.shadow.radius = 2.5;
  scene.add(hemi, sun, sun.target);

  const ctx = { renderer, scene, camera, hemi, sun, composer: null };
  // pós-processamento conforme a qualidade: GTAO (oclusão de ambiente), bloom sutil, saída ACES e SMAA
  ctx.setupPost = (q) => {
    if (ctx.composer) { ctx.composer.dispose(); ctx.composer = null; }
    if (!q.post) return;
    const w = window.innerWidth, h = window.innerHeight;
    const c = new EffectComposer(renderer);
    c.addPass(new RenderPass(scene, camera));
    if (q.gtao) {
      const ao = new GTAOPass(scene, camera, w, h);
      // oclusão calculada em meia resolução (o denoise e a mistura suavizam a volta para a tela cheia)
      const setAoSize = ao.setSize.bind(ao);
      ao.setSize = (sw, sh) => setAoSize(Math.ceil(sw / 2), Math.ceil(sh / 2));
      // O passe de normais ignora céu, água e toda a vegetação instanciada (folhagem, troncos, rochas
      // espalhadas): eram ~150 chamadas por quadro e a oclusão que geravam quase não aparece.
      ao._overrideVisibility = () => {
        const cache = ao._visibilityCache;
        scene.traverseVisible((o) => {
          const m = o.material;
          if (o.isPoints || o.isLine || o.isSprite || o.isInstancedMesh
            || (m && (m.transparent || m.alphaTest > 0 || m.isShaderMaterial || m.side === THREE.BackSide))) cache.push(o);
        });
        for (const o of cache) o.visible = false;
      };
      ao.blendIntensity = 0.85;
      ao.updateGtaoMaterial({ radius: 0.8, distanceExponent: 1.5, thickness: 1.2, scale: 1, samples: 12 });
      ao.updatePdMaterial({ lumaPhi: 10, depthPhi: 2, normalPhi: 3, radius: 5, rings: 2, samples: 12 });
      c.addPass(ao);
    }
    if (q.bloom) {
      const bloom = new UnrealBloomPass(new THREE.Vector2(w, h), 0.14, 0.4, 1.15);
      // o disco do sol chega a milhares em HDR: sem limite, o borrão do bloom o espalha como um véu
      // branco sobre a tela inteira. O teto preserva o brilho de forja, cristais e reflexos.
      const hp = bloom.materialHighPassFilter;
      hp.fragmentShader = hp.fragmentShader.replace(
        'gl_FragColor = mix( vec4( 0.0 ), texel, alpha );',
        'gl_FragColor = min( mix( vec4( 0.0 ), texel, alpha ), vec4( 3.0 ) );');
      hp.needsUpdate = true;
      c.addPass(bloom);
    }
    c.addPass(new OutputPass());
    if (q.smaa) c.addPass(new SMAAPass());
    c.setPixelRatio(renderer.getPixelRatio());
    c.setSize(w, h);
    ctx.composer = c;
  };
  // compila de antemão os sombreadores de tudo que existe na cena (inclusive regiões distantes),
  // para não travar um quadro na primeira vez que algo aparece; usa o mesmo alvo do pós-processamento
  ctx.precompile = () => {
    const prev = renderer.getRenderTarget();
    scene.updateMatrixWorld();
    renderer.setRenderTarget(ctx.composer ? ctx.composer.renderTarget1 : null);
    const done = renderer.compileAsync(scene, camera);
    renderer.setRenderTarget(prev);
    return done.catch(() => {});
  };
  ctx.render = () => {
    scene.updateMatrixWorld();
    renderer.shadowMap.needsUpdate = true;
    if (ctx.composer) ctx.composer.render(); else renderer.render(scene, camera);
  };

  window.addEventListener('resize', () => {
    ctx.dirty = true;   // redimensionar limpa o canvas: redesenha mesmo com o jogo pausado
    renderer.setSize(window.innerWidth, window.innerHeight);
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    if (ctx.composer) ctx.composer.setSize(window.innerWidth, window.innerHeight);
  });
  return ctx;
}
