#version 450
// Interface 2D em pixels (origem no canto superior esquerdo).
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;

layout(std140, set = 1, binding = 0) uniform Screen { vec4 size; } S;  // xy: largura, altura

layout(location = 0) out vec2 vUv;
layout(location = 1) out vec4 vColor;

void main() {
  vUv = inUv;
  vColor = inColor;
  gl_Position = vec4(inPos.x / S.size.x * 2.0 - 1.0, 1.0 - inPos.y / S.size.y * 2.0, 0.0, 1.0);
}
