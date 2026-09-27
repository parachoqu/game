#pragma once
// Espelho em C++ do bloco FRAME_BLOCK de shaders/src/frame.glsl (std140: só mat4 e vec4).
#include <cstring>

#include <glm/glm.hpp>

namespace rpg::client {

struct FrameUniforms {
  float viewProj[16];
  float invViewProj[16];
  float camPos[4];     // w: tempo
  float sunDir[4];     // w: intensidade do sol
  float sunColor[4];   // w: intensidade do hemisfério
  float hemiSky[4];
  float hemiGround[4];
  float fog[4];        // rgb sRGB, w densidade
  float skyTop[4];     // w: sol visível
  float skyHorizon[4]; // w: noite
  float params[4];     // x: exposição; y: vazio da Turbulenta

  static void put(float* dst, const glm::mat4& m) { std::memcpy(dst, &m[0][0], sizeof(float) * 16); }
  static void put4(float* dst, glm::vec3 v, float w) {
    dst[0] = v.x;
    dst[1] = v.y;
    dst[2] = v.z;
    dst[3] = w;
  }
};
static_assert(sizeof(FrameUniforms) == 16 * 4 * 2 + 16 * 9);

}  // namespace rpg::client
