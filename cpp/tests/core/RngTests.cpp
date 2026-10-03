#include <array>
#include <set>

#include <catch2/catch_test_macros.hpp>

#include "core/Rng.h"

namespace {

// Primeiros valores de `mulberry(seed)` (sky.js / world.js / noise.js) calculados no Node.
void checkSequence(std::int32_t seed, const std::array<double, 5>& expected) {
  rpg::Mulberry32 r(seed);
  for (double e : expected) CHECK(r.next() == e);
}

}  // namespace

TEST_CASE("Mulberry32 é idêntico ao do JS, bit a bit", "[core][rng]") {
  checkSequence(42, {0.60110375192016363, 0.44829055899754167, 0.85246579349040985, 0.66973404143936932,
                     0.17481389874592423});
  checkSequence(1337, {0.18441183259710670, 0.18998925131745636, 0.81047199224121869, 0.64374882215633988,
                       0.43077461561188102});
  checkSequence(0, {0.26642920868471265, 0.00032974570058286190, 0.22327202744781971, 0.14620214793831110,
                    0.46732782293111086});
  checkSequence(-7, {0.43306733411736786, 0.32539576734416187, 0.54426950030028820, 0.48018999374471605,
                     0.048719558166339993});
}

TEST_CASE("Pcg32 é determinístico e restaurável", "[core][rng]") {
  rpg::Pcg32 a(123), b(123), c(124);
  bool differs = false;
  for (int i = 0; i < 100; ++i) {
    const auto x = a.nextU32();
    CHECK(x == b.nextU32());
    differs |= x != c.nextU32();
  }
  CHECK(differs);

  const auto saved = a.save();
  const double next = a.next();
  a.next();
  a.restore(saved);
  CHECK(a.next() == next);
}

TEST_CASE("Pcg32: faixas do rand/randi/pick do state.js", "[core][rng]") {
  rpg::Pcg32 r(7);
  std::set<int> seen;
  for (int i = 0; i < 2000; ++i) {
    const double x = r.range(2.0, 5.0);
    REQUIRE(x >= 2.0);
    REQUIRE(x < 5.0);
    const int k = r.rangeInt(4, 9);  // randi inclui os dois extremos
    REQUIRE(k >= 4);
    REQUIRE(k <= 9);
    seen.insert(k);
    REQUIRE(r.index(3) < 3);
  }
  CHECK(seen == std::set<int>{4, 5, 6, 7, 8, 9});
}
