#version 450
// Malhas iluminadas: relevo (uma instância identidade) e objetos instanciados (matriz + cor).
#extension GL_GOOGLE_include_directive : require
#include "frame.glsl"

layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inColor;  // sRGB
layout(location = 3) in vec4 iM0;
layout(location = 4) in vec4 iM1;
layout(location = 5) in vec4 iM2;
layout(location = 6) in vec4 iM3;
layout(location = 7) in vec4 iTint;    // sRGB, multiplica a cor do vértice

layout(std140, set = 1, binding = 0) uniform Frame { FRAME_BLOCK } F;

layout(location = 0) out vec3 vWorld;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec4 vColor;

void main() {
  mat4 M = mat4(iM0, iM1, iM2, iM3);
  vec4 w = M * vec4(inPos, 1.0);
  vWorld = w.xyz;
  vNormal = transpose(inverse(mat3(M))) * inNormal;
  vColor = vec4(srgbToLinear(inColor.rgb * iTint.rgb), inColor.a * iTint.a);
  gl_Position = F.viewProj * w;
}
