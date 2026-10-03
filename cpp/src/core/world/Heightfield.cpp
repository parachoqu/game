#include "core/world/Heightfield.h"

#include <cmath>

#include "core/Assert.h"
#include "core/Math.h"

namespace rpg {

Heightfield::Heightfield(std::vector<std::uint16_t> data, const Meta& meta) : data_(std::move(data)), meta_(meta) {
  RPG_ASSERT(meta_.w >= 2 && meta_.h >= 2, "heightfield menor que 2×2");
  RPG_ASSERT(data_.size() == static_cast<std::size_t>(meta_.w) * static_cast<std::size_t>(meta_.h),
             "heightfield com quantidade de amostras errada");
}

// heightfield.js `sampleField`, linha a linha.
double Heightfield::sample(double px, double py) const {
  const Meta& f = meta_;
  const double fx = (px - f.x0) / f.step;
  const double fy = (py - f.y0) / f.step;
  const double i = fx < 0 ? 0 : fx > f.w - 2 ? f.w - 2 : std::floor(fx);
  const double j = fy < 0 ? 0 : fy > f.h - 2 ? f.h - 2 : std::floor(fy);
  double tx = fx - i, ty = fy - j;
  tx = tx < 0 ? 0 : tx > 1 ? 1 : tx;
  ty = ty < 0 ? 0 : ty > 1 ? 1 : ty;
  const auto k = static_cast<std::size_t>(j) * static_cast<std::size_t>(f.w) + static_cast<std::size_t>(i);
  const double a = data_[k], b = data_[k + 1];
  const double c = data_[k + static_cast<std::size_t>(f.w)], d = data_[k + static_cast<std::size_t>(f.w) + 1];
  const double u = tx > ty ? a + (b - a) * tx + (d - b) * ty : a + (d - c) * tx + (c - a) * ty;
  return f.min + u * f.scale;
}

double Heightfield::at(int i, int j) const {
  const int ci = i < 0 ? 0 : i > meta_.w - 1 ? meta_.w - 1 : i;
  const int cj = j < 0 ? 0 : j > meta_.h - 1 ? meta_.h - 1 : j;
  return meta_.min + data_[static_cast<std::size_t>(cj) * static_cast<std::size_t>(meta_.w) + static_cast<std::size_t>(ci)] * meta_.scale;
}

BiomeMask::BiomeMask(std::vector<std::uint8_t> bytes, int w, double x0, double y0)
    : bytes_(std::move(bytes)), w_(w), x0_(x0), y0_(y0) {
  RPG_ASSERT(bytes_.size() * 2 >= static_cast<std::size_t>(w_) * static_cast<std::size_t>(w_), "máscara pequena demais");
}

// heightfield.js `maskNibble` (com o Math.round do JS: empate para +∞).
int BiomeMask::nibble(double px, double py) const {
  const double i = jsRound(px - x0_);
  const double j = jsRound(py - y0_);
  if (i < 0 || j < 0 || i >= w_ || j >= w_) return 0;
  const auto k = static_cast<std::size_t>(j) * static_cast<std::size_t>(w_) + static_cast<std::size_t>(i);
  const std::uint8_t byte = bytes_[k >> 1];
  return (k & 1) ? (byte >> 4) & 0x0f : byte & 0x0f;
}

}  // namespace rpg
