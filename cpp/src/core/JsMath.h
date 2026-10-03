#pragma once
// Funções transcendentais iguais às do JavaScript (V8), bit a bit.
//
// O V8 implementa Math.sin, Math.cos, Math.atan2 e Math.exp com o fdlibm (src/base/ieee754.cc), e não
// com a libm do sistema. Comparando 2 milhões de argumentos no Node 22 com a glibc 2.39, 2,5% dos
// senos, 19% dos atan2 e 10% das exponenciais diferem no último bit. Na simulação essa diferença
// cresce a cada passo (colisões, IA) e o rastro se separa do da demo em poucos segundos.
//
// Além da paridade com a demo, isso dá determinismo entre plataformas: a mesma conta sai igual no
// servidor Linux (glibc), no cliente Windows (UCRT) e no macOS, o que a predição do cliente precisa.
// A simulação e o motor compartilhado usam só estas funções; o render pode usar as de <cmath>.
//
// Código derivado do fdlibm 5.3 (Sun Microsystems, domínio permissivo: "Permission to use, copy,
// modify, and distribute this software is freely granted, provided that this notice is preserved"),
// na forma usada pelo V8.
namespace rpg::js {

double sin(double x);
double cos(double x);
double atan(double x);
double atan2(double y, double x);
double exp(double x);

}  // namespace rpg::js
