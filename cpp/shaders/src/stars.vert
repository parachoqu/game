#version 450
// Estrelas (engine/sky.js, THREE.Points): um quadrado por estrela, do tamanho do ponto em pixels,
// com cintilação e o sumiço (aGone) das estrelas apagadas. A cúpula acompanha a câmera.
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"

layout(location = 0) in vec4 iPos;   // xyz: direção × R; w: tamanho (aSize)
layout(location = 1) in vec4 iAttr;  // x: brilho; y: fase; z: sumiço

layout(std140, set = 1, binding = 0) uniform Frame { FRAME_BLOCK } F;

layout(location = 0) out vec2 vCoord;
layout(location = 1) out float vA;

void main() {
  const vec2 corners[6] = vec2[](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
  vec2 c = corners[gl_VertexIndex];
  vec4 clip = F.viewProj * vec4(F.camPos.xyz + iPos.xyz, 1.0);
  float size = iPos.w * F.sky.w;  // gl_PointSize = aSize * uPixel
  vec2 viewport = vec2(F.camDir.w, F.occCam.w);
  clip.xy += c * size / viewport * clip.w;
  gl_Position = clip;
  vCoord = c * 0.5 + 0.5;
  float aPhase = iAttr.y;
  float tw = 0.78 + 0.22 * sin(F.sky.x * (1.5 + aPhase * 2.0) + aPhase * 6.28);
  vA = iAttr.x * F.sky.y * (1.0 - iAttr.z) * tw;
}
