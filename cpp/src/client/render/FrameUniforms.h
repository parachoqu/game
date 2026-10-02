#pragma once
// Espelho em C++ do bloco FRAME_BLOCK de shaders/src/frame.glsl (std140: só mat4 e vec4).
#include <array>
#include <cstring>

#include <glm/glm.hpp>

namespace rpg::client {

struct FrameUniforms {
  float viewProj[16];
  float invViewProj[16];
  float camPos[4];     // w: tempo (U.uTime)
  float camDir[4];     // xyz: frente da câmera (profundidade da névoa); w: largura da tela em pixels
  float sunDir[4];     // de onde vem a luz direcional; w: intensidade
  float sunColor[4];   // w: intensidade do hemisfério
  float hemiSky[4];
  float hemiGround[4];
  float fog[4];        // rgb linear, w densidade (FogExp2)
  float skyTop[4];     // w: sol visível
  float skyHorizon[4]; // w: noite
  float params[4];     // x: exposição; y: vazio da Turbulenta; z: intensidade do ambiente; w: mip máximo do ambiente (< 0 = sem)
  float skySun[4];     // xyz: direção do sol (o disco e a atmosfera); w: dia
  float occCam[4];     // recorte pontilhado (OCC.uCam); w: altura da tela em pixels
  float occTarget[4];  // OCC.uTarget; w: alcance da grama (U.uGrassFar)
  float sky[4];        // x: tempo do céu (G.time; no título, o tempo da tela); y: estrelas (uNight); z: linhas; w: escala de pixel
  float sh[9][4];      // irradiância do ambiente em harmônicos esféricos (E(n), já com as constantes)

  static void put(float* dst, const glm::mat4& m) { std::memcpy(dst, &m[0][0], sizeof(float) * 16); }
  static void put4(float* dst, glm::vec3 v, float w) {
    dst[0] = v.x;
    dst[1] = v.y;
    dst[2] = v.z;
    dst[3] = w;
  }
};
static_assert(sizeof(FrameUniforms) == 16 * 4 * 2 + 16 * 14 + 16 * 9);

}  // namespace rpg::client
