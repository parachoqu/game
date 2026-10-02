#pragma once
// Luz de ambiente (IBL) do mundo, como em engine/sky.js `updateEnvironment`:
//   - de dia (dia > 0,55): o HDRI reduzido do kit de natureza (nature-kit/sky-env.webp, RGBE);
//   - no resto do ciclo: a atmosfera de Preetham sem nuvens, refeita quando o sol anda ~2°;
//   - na Turbulenta: nenhuma.
// O mapa fica em equirretangular linear (float RGB) e vira duas coisas para a GPU: a textura (reflexo
// especular, com mips por rugosidade) e a irradiância em harmônicos esféricos (difuso).
//
// Orientação: igual à da demo. Lá o RGBE vira uma DataTexture com flipY = false, então a linha 0
// da imagem fica em v = 0, que o `equirectUv` do three associa a y = −1. Aqui vale o mesmo.
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "client/assets/Image.h"

namespace rpg::client {

struct EnvironmentMap {
  int w = 0, h = 0;
  std::vector<float> rgb;  // w × h × 3, linha 0 em v = 0
};

// Irradiância E(n) em 9 harmônicos esféricos (já multiplicados pelas constantes do cosseno).
using IrradianceSH = std::array<glm::vec3, 9>;

// RGBE em WebP (linear = rgb / 255 · 2^(a − 128)), como `loadKitEnvironment`.
EnvironmentMap decodeRgbe(const ImageRGBA& img);

// Atmosfera de Preetham do three (addon Sky) com os parâmetros de `makeAtmosphere`, sem nuvens e sem
// o disco do sol, já com a exposição do céu (0,32). `sunDir` é a direção do sol (normalizada).
glm::vec3 preethamRadiance(glm::vec3 dir, glm::vec3 sunDir);
EnvironmentMap preethamEnvironment(glm::vec3 sunDir, int w = 128, int h = 64);

// Direção do centro de um texel (convenção `equirectUv`).
glm::vec3 equirectDirection(double u, double v);

IrradianceSH irradianceSH(const EnvironmentMap& env);
glm::vec3 evalIrradiance(const IrradianceSH& sh, glm::vec3 n);

// sky.js `updateEnvironment`: escolhe o ambiente da hora e só troca quando a chave muda (o sol andou
// ~2°, o dia passou de 0,55, entrou ou saiu da Turbulenta).
class EnvironmentSelector {
 public:
  void setKit(EnvironmentMap kit);
  bool update(glm::vec3 sunDir, bool turbulent, double day);  // true = mudou
  const EnvironmentMap* current() const;                      // nullptr = sem ambiente
  const IrradianceSH& sh() const { return sh_; }
  float maxMip() const;  // log2(largura / 4) do mapa atual; −1 sem ambiente

 private:
  std::optional<EnvironmentMap> kit_;
  IrradianceSH kitSh_{};
  EnvironmentMap atmosphere_;
  IrradianceSH sh_{};
  std::string key_;
  int mode_ = 0;  // 0 nenhum, 1 kit, 2 atmosfera
};

}  // namespace rpg::client
