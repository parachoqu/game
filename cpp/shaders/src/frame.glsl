// Uniformes por quadro, iguais para todos os passes de cena (std140). A mesma estrutura vai no
// conjunto 1 do vértice e no conjunto 3 do fragmento (regras de ligação do SDL_GPU para SPIR-V).
// Espelho em C++: client/render/FrameUniforms.h. Todas as cores são lineares (THREE.Color).
#define FRAME_BLOCK            \
  mat4 viewProj;               \
  mat4 invViewProj;            \
  vec4 camPos;     /* w: tempo */            \
  vec4 camDir;     /* xyz: frente da câmera; w: largura da tela */ \
  vec4 sunDir;     /* luz direcional; w: intensidade */ \
  vec4 sunColor;   /* w: intensidade do hemisfério */ \
  vec4 hemiSky;                \
  vec4 hemiGround;             \
  vec4 fog;        /* rgb: cor linear; w: densidade (FogExp2) */ \
  vec4 skyTop;     /* w: sol visível (0–1) */ \
  vec4 skyHorizon; /* w: noite (0–1) */       \
  vec4 params;     /* x: exposição; y: vazio da Turbulenta; z: intensidade do ambiente; w: mip máximo do ambiente */ \
  vec4 skySun;     /* xyz: direção do sol; w: dia */ \
  vec4 occCam;     /* w: altura da tela */ \
  vec4 occTarget;  /* w: alcance da grama */ \
  vec4 sky;        /* x: tempo do céu; y: estrelas (uNight); z: linhas das constelações; w: escala de pixel */ \
  vec4 sh[9];

vec3 srgbToLinear(vec3 c) {
  return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
