#version 450
// UnrealBloomPass, passa-alta de luminância (LuminosityHighPassShader): limiar 1,15 com 0,01 de
// transição e o teto de 3,0 da demo (o disco do sol não vira um véu branco). A oclusão já entra aqui:
// no three o bloom lê a cena depois do GTAO.
layout(location = 0) in vec2 vUv;
layout(set = 2, binding = 0) uniform sampler2D tScene;
layout(set = 2, binding = 1) uniform sampler2D tAo;
layout(std140, set = 3, binding = 0) uniform Bright {
  vec4 params;  // x: limiar; y: transição; z: teto; w: mistura da oclusão
} B;
layout(location = 0) out vec4 outColor;
void main() {
  vec4 texel = texture(tScene, vUv);
  texel.rgb *= mix(1.0, texture(tAo, vUv).r, B.params.w);
  float v = dot(texel.rgb, vec3(0.299, 0.587, 0.114));
  float alpha = smoothstep(B.params.x, B.params.x + B.params.y, v);
  outColor = min(mix(vec4(0.0), texel, alpha), vec4(B.params.z));
}
