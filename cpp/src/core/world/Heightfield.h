#pragma once
// Heightfield determinístico e máscara de biomas (world/heightfield.js).
//
// A amostragem é bilinear com a mesma diagonal (a–d) da malha do terreno, para o chão calculado e o
// chão desenhado coincidirem vértice a vértice. Fora dos limites vale a borda (nunca NaN).
#include <cstdint>
#include <vector>

namespace rpg {

class Heightfield {
 public:
  struct Meta {
    int w = 0, h = 0;
    double step = 1.0;
    double x0 = 0.0, y0 = 0.0;  // origem no plano do mundo (x, y do Blender)
    double min = 0.0, scale = 0.0;
  };

  Heightfield() = default;
  Heightfield(std::vector<std::uint16_t> data, const Meta& meta);

  // Altura em coordenadas do plano (px, py), como `sampleField` do JS.
  double sample(double px, double py) const;
  // Altura exata num nó da grade (`fieldAt`).
  double at(int i, int j) const;

  const Meta& meta() const { return meta_; }

 private:
  std::vector<std::uint16_t> data_;
  Meta meta_;
};

// Máscara de 4 bits por amostra (nibble baixo primeiro): bits 0–2 bioma, bit 3 água.
class BiomeMask {
 public:
  BiomeMask() = default;
  BiomeMask(std::vector<std::uint8_t> bytes, int w, double x0, double y0);

  // Coordenadas do plano; fora da máscara devolve 0.
  int nibble(double px, double py) const;

 private:
  std::vector<std::uint8_t> bytes_;
  int w_ = 0;
  double x0_ = 0.0, y0_ = 0.0;
};

}  // namespace rpg
