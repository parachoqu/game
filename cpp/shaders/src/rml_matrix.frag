#version 450
// Filtros de cor (brilho, contraste, saturação…): matriz aplicada no espaço pré-multiplicado.
layout(location = 0) in vec2 vUv;
layout(set = 2, binding = 0) uniform sampler2D uTex;
layout(std140, set = 3, binding = 0) uniform ColorMatrix { mat4 m; } C;
layout(location = 0) out vec4 outColor;
void main() {
  vec4 c = texture(uTex, vUv);
  outColor = vec4(vec3(C.m * c), c.a);
}
