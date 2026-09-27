#pragma once
// Ruído determinístico da demo (world/noise.js), bit a bit: mesmo par (x, z) e semente → mesmo valor
// em qualquer máquina. Usado pela distribuição da vegetação e da cobertura do chão.
#include <cstdint>

#include "core/Types.h"

namespace rpg {

// ToInt32 do JS (`v | 0`): trunca e reduz módulo 2^32.
std::int32_t jsToInt32(double v);

// hash inteiro → [0, 1)
double hash2(double x, double z, double seed = 0);
// valor 2D em [0, 1]
double vnoise(double x, double z, double seed = 0);
// fbm 2D em [0, 1], normalizado
double fbm2(double x, double z, int octaves = 3, double seed = 0);

}  // namespace rpg
