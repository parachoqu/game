#version 450
// Geometria sem luz no espaço do mundo: telegrafias, anéis, barras de vida, partículas.
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;  // sRGB

layout(std140, set = 1, binding = 0) uniform Frame { FRAME_BLOCK } F;

layout(location = 0) out vec4 vColor;

void main() {
  vColor = vec4(srgbToLinear(inColor.rgb), inColor.a);
  gl_Position = F.viewProj * vec4(inPos, 1.0);
}
