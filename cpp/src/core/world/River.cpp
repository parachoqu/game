#include "core/world/River.h"

#include <cmath>

#include "core/Assert.h"

namespace rpg {

River::River(double x0, std::vector<float> level, std::vector<float> center, std::vector<float> half)
    : x0_(x0), n_(static_cast<int>(level.size())), level_(std::move(level)), center_(std::move(center)), half_(std::move(half)) {
  RPG_ASSERT(n_ >= 2 && center_.size() == level_.size() && half_.size() == level_.size(), "rio com tabelas inconsistentes");
}

// heightfield.js `riverSample`
double River::sample(const std::vector<float>& arr, double x) const {
  const double t = x - x0_;
  if (t <= 0) return static_cast<double>(arr[0]);
  if (t >= n_ - 1) return static_cast<double>(arr[static_cast<std::size_t>(n_ - 1)]);
  const double i = std::floor(t);
  const auto k = static_cast<std::size_t>(i);
  const auto a = static_cast<double>(arr[k]), b = static_cast<double>(arr[k + 1]);
  return a + (b - a) * (t - i);
}

}  // namespace rpg
