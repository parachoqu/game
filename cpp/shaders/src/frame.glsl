// Uniformes por quadro, iguais para todos os passes de cena (std140). A mesma estrutura vai no
// conjunto 1 do vértice e no conjunto 3 do fragmento (regras de ligação do SDL_GPU para SPIR-V).
// Espelho em C++: client/render/FrameUniforms.h. Todas as cores são lineares (THREE.Color).
#define FRAME_BLOCK            \
  mat4 viewProj;               \
  mat4 invViewProj;            \
  vec4 camPos;     /* w: tempo */            \
  vec4 sunDir;     /* w: intensidade do sol */ \
  vec4 sunColor;   /* w: intensidade do hemisfério */ \
  vec4 hemiSky;                \
  vec4 hemiGround;             \
  vec4 fog;        /* rgb: cor linear; w: densidade (FogExp2) */ \
  vec4 skyTop;     /* w: sol visível (0–1) */ \
  vec4 skyHorizon; /* w: noite (0–1) */       \
  vec4 params;     /* x: exposição; y: vazio da Turbulenta; z: intensidade do ambiente */

vec3 srgbToLinear(vec3 c) {
  return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c));
}
