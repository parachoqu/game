#version 450
layout(location = 0) in vec2 vCoord;
layout(location = 1) in float vA;
layout(location = 0) out vec4 outColor;
void main() {
  vec2 d = vCoord - 0.5;
  float r = length(d);
  float a = smoothstep(0.5, 0.05, r);
  if (a * vA < 0.01) discard;
  outColor = vec4(1.0, 0.96, 0.88, a * vA);
}
