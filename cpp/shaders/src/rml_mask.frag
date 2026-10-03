#version 450
// mask-image: multiplica a camada pelo alfa da máscara salva.
layout(location = 0) in vec2 vUv;
layout(set = 2, binding = 0) uniform sampler2D uTex;
layout(set = 2, binding = 1) uniform sampler2D uMask;
layout(location = 0) out vec4 outColor;
void main() { outColor = texture(uTex, vUv) * texture(uMask, vUv).a; }
