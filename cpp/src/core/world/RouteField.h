#pragma once
// Rotas do mundo (world/world-routes.js): polilinhas e o campo de distância em grade.
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "core/Types.h"

namespace rpg {

struct Polyline {
  std::string id;
  double width = 0.0;
  std::vector<XZ> pts;
};

// world-routes.js `distToPolyline`: distância do ponto à polilinha (∞ se ela tiver menos de 2 pontos).
double distToPolyline(double x, double z, std::span<const XZ> pts);

// Grade de 2 m sobre a região com a distância até a rota mais próxima, em decímetros, saturada em
// 25,5 m. Uma consulta é uma leitura, sem percorrer as polilinhas.
class RouteField {
 public:
  RouteField() = default;
  RouteField(std::vector<std::uint8_t> data, int n, double step, double origin);

  // world-routes.js `routeDistance` (metros).
  double distance(double x, double z) const;

 private:
  std::vector<std::uint8_t> data_;
  int n_ = 0;
  double step_ = 2.0;
  double origin_ = -512.0;
};

}  // namespace rpg
