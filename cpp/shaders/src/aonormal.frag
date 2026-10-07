#version 450
// Passada de normais da oclusão de ambiente (GTAOPass do three): normal de mundo em [0, 1] e a
// profundidade no alvo de profundidade. Só entram malhas opacas sem alphaTest e não instanciadas, como
// o `_overrideVisibility` da demo (céu, água, folhagem, troncos e rochas instanciados ficam de fora).
// Face de trás (materiais de dois lados): a normal vira para a câmera, como o `faceDirection` do three.
layout(location = 1) in vec3 vNormal;
layout(location = 0) out vec4 outNormal;
void main() {
  vec3 n = normalize(vNormal);
  if (!gl_FrontFacing) n = -n;
  outNormal = vec4(n * 0.5 + 0.5, 1.0);
}
