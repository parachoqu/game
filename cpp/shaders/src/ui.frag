#version 450
// Atlas de 8 bits: cobertura dos glifos (FreeType) e um texel branco para retângulos sólidos.
layout(location = 0) in vec2 vUv;
layout(location = 1) in vec4 vColor;
layout(set = 2, binding = 0) uniform sampler2D uAtlas;
layout(location = 0) out vec4 outColor;
void main() {
  float a = texture(uAtlas, vUv).r;
  outColor = vec4(vColor.rgb, vColor.a * a);
}
