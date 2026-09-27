#include "core/world/SkyLayout.h"

#include <cmath>

#include "core/JsMath.h"
#include "core/Math.h"
#include "core/Rng.h"

namespace rpg {
namespace {

std::array<double, 3> dirFrom(double azDeg, double elDeg) {
  const double az = azDeg * kPi / 180, el = elDeg * kPi / 180;
  return {js::sin(az) * js::cos(el), js::sin(el), -js::cos(az) * js::cos(el)};
}

}  // namespace

std::array<double, 3> SkyLayout::dir(int index) const {
  const Star& s = stars[static_cast<std::size_t>(index)];
  const double l = std::sqrt(s.x * s.x + s.y * s.y + s.z * s.z);
  return {s.x / l, s.y / l, s.z / l};
}

SkyLayout SkyLayout::generate() {
  SkyLayout L;
  L.constellations = {
      {"lanterna", 352, 20, {{0, 0}, {3, 4}, {6.5, 1.5}, {5, -3}, {1, -3.5}, {3.2, 0.4}},
       {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 0}, {1, 5}, {5, 3}}, {}},
      {"ancora", 24, 44, {{0, 0}, {0, 5}, {0, 10}, {-4.5, 1.5}, {4.5, 1.5}, {-2.5, -1.8}, {2.5, -1.8}},
       {{0, 1}, {1, 2}, {0, 5}, {5, 3}, {0, 6}, {6, 4}}, {}},
      {"cervo", 82, 28, {{0, 0}, {4, 1}, {8, 0}, {9, 4}, {11, 7}, {7, 5}, {2, -3}, {6, -3}},
       {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {3, 5}, {0, 6}, {2, 7}}, {}},
      {"balanca", 300, 52, {{0, 0}, {-5, -1}, {5, -1}, {-6, -4}, {6, -4}, {0, 4}},
       {{0, 1}, {0, 2}, {1, 3}, {2, 4}, {0, 5}}, {}},
  };
  Mulberry32 rnd(42);
  const auto addStar = [&](const std::array<double, 3>& v, double size, double bright) {
    Star s;
    s.x = v[0] * kRadius;
    s.y = v[1] * kRadius;
    s.z = v[2] * kRadius;
    s.size = size;
    s.bright = bright;
    s.phase = rnd.next();
    L.stars.push_back(s);
    return static_cast<int>(L.stars.size()) - 1;
  };
  for (int i = 0; i < 1700; ++i) {
    // a ordem das chamadas de rnd() é a do JS: y, a, depois tamanho e brilho, depois a fase
    const double y = -0.08 + rnd.next() * 1.08;
    const double a = rnd.next() * kPi * 2;
    const double r = std::sqrt(1 - y * y);
    const std::array<double, 3> v{js::cos(a) * r, y, js::sin(a) * r};
    const double size = 1 + rnd.next() * 1.6;
    const double bright = 0.35 + rnd.next() * 0.5;
    addStar(v, size, bright);
  }
  for (Constellation& c : L.constellations) {
    for (std::size_t k = 0; k < c.pts.size(); ++k) {
      c.indices.push_back(addStar(dirFrom(c.az + c.pts[k][0], c.el + c.pts[k][1]), 3.6 + static_cast<double>(k % 3) * 0.5, 1));
    }
  }
  L.titleStar = addStar(dirFrom(346, 30), 4.4, 1);
  for (int i = 0; i < 12; ++i) {
    const double az = rnd.next() * 360;
    const double el = 12 + rnd.next() * 55;
    L.isolated.push_back(addStar(dirFrom(az, el), 3.4, 1));
  }
  constexpr std::array<std::array<int, 2>, 18> kPick = {{{0, 3}, {-1, 0}, {1, 4}, {-1, 1}, {0, 1}, {-1, 2},
                                                         {2, 2}, {-1, 3}, {3, 0}, {1, 6}, {0, 5}, {-1, 4},
                                                         {2, 5}, {3, 3}, {-1, 5}, {1, 2}, {2, 7}, {0, 0}}};
  for (const auto& [ci, k] : kPick) {
    if (ci < 0) L.order.push_back({L.isolated[static_cast<std::size_t>(k)], -1});
    else L.order.push_back({L.constellations[static_cast<std::size_t>(ci)].indices[static_cast<std::size_t>(k)], ci});
  }
  return L;
}

}  // namespace rpg
