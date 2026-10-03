#pragma once
// Rio principal já escavado no vale (world/river.js): nível da lâmina, eixo e meia-largura por
// coluna de 1 m. O rio atravessa a região de oeste a leste, sem voltar sobre si.
#include <vector>

namespace rpg {

class River {
 public:
  River() = default;
  River(double x0, std::vector<float> level, std::vector<float> center, std::vector<float> half);

  double levelAt(double x) const { return sample(level_, x); }       // heightfield.js waterLevelAt
  double centerAt(double x) const { return sample(center_, x); }     // riverCenterAt (layout.js riverZ)
  double halfWidthAt(double x) const { return sample(half_, x); }    // riverHalfWidthAt

 private:
  double sample(const std::vector<float>& arr, double x) const;
  double x0_ = 0.0;
  int n_ = 0;
  std::vector<float> level_, center_, half_;  // Float32Array no JS: guardados em float, lidos em double
};

}  // namespace rpg
