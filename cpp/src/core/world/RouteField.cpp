#include "core/world/RouteField.h"

#include <cmath>
#include <limits>

#include "core/Assert.h"
#include "core/Math.h"

namespace rpg {

double distToPolyline(double x, double z, std::span<const XZ> pts) {
  double best = std::numeric_limits<double>::infinity();
  for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
    const double ax = pts[i].x, az = pts[i].z;
    const double dx = pts[i + 1].x - ax, dz = pts[i + 1].z - az;
    const double l2 = dx * dx + dz * dz;
    double t = l2 != 0.0 ? ((x - ax) * dx + (z - az) * dz) / l2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    const double px = ax + dx * t - x, pz = az + dz * t - z;
    const double d = px * px + pz * pz;
    if (d < best) best = d;
  }
  return std::sqrt(best);
}

RouteField::RouteField(std::vector<std::uint8_t> data, int n, double step, double origin)
    : data_(std::move(data)), n_(n), step_(step), origin_(origin) {
  RPG_ASSERT(data_.size() == static_cast<std::size_t>(n_) * static_cast<std::size_t>(n_), "campo de rotas com tamanho errado");
}

// JS: fi = (x + 512) / RF_STEP; i = fi < 0 ? 0 : fi > N − 1 ? N − 1 : Math.round(fi)
double RouteField::distance(double x, double z) const {
  const double fi = (x - origin_) / step_, fj = (z - origin_) / step_;
  const double i = fi < 0 ? 0 : fi > n_ - 1 ? n_ - 1 : jsRound(fi);
  const double j = fj < 0 ? 0 : fj > n_ - 1 ? n_ - 1 : jsRound(fj);
  return data_[static_cast<std::size_t>(j) * static_cast<std::size_t>(n_) + static_cast<std::size_t>(i)] / 10.0;
}

}  // namespace rpg
