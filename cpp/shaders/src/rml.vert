#version 450
// Interface do RmlUi: vértices em pixels (origem no canto superior esquerdo), cor pré-multiplicada.
// `transform` já traz a projeção (pixels → NDC) vezes a transformação do elemento.
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inUv;

layout(std140, set = 1, binding = 0) uniform Xf {
  mat4 transform;
  vec4 translate;  // xy
} X;

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;

void main() {
  vUv = inUv;
  vColor = inColor;
  gl_Position = X.transform * vec4(inPos + X.translate.xy, 0.0, 1.0);
}
