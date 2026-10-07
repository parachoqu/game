#include <cmath>
#include <limits>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/Math.h"

using Catch::Approx;

TEST_CASE("angleDiff reproduz os laços do state.js", "[core][math]") {
  // Valores de referência calculados com a função do JS.
  CHECK(rpg::angleDiff(0, 0) == 0.0);
  CHECK(rpg::angleDiff(3, -3) == Approx(-0.28318530717958623).epsilon(1e-15));
  CHECK(rpg::angleDiff(10, 0) == Approx(-2.5663706143591725).epsilon(1e-15));
  CHECK(rpg::angleDiff(-10, 0) == Approx(2.5663706143591725).epsilon(1e-15));
  CHECK(rpg::angleDiff(rpg::kPi, -rpg::kPi) == 0.0);
  CHECK(rpg::angleDiff(1000, 1) == Approx(-0.026463841551908729).epsilon(1e-12));
}

TEST_CASE("clamp, lerp, smooth e lerpAngle", "[core][math]") {
  CHECK(rpg::clamp(-1, 0, 1) == 0.0);
  CHECK(rpg::clamp(2, 0, 1) == 1.0);
  CHECK(rpg::clamp(0.25, 0, 1) == 0.25);
  CHECK(rpg::lerp(2, 4, 0.5) == 3.0);
  CHECK(rpg::smooth(0, 1, 0.5) == 0.5);
  CHECK(rpg::smooth(0, 1, -3) == 0.0);
  CHECK(rpg::smooth(0, 1, 3) == 1.0);
  // lerpAngle limita t a 1 e vai pelo caminho mais curto (de 3 rad para -3 rad passa por π)
  CHECK(rpg::lerpAngle(3, -3, 5) == Approx(3 + rpg::angleDiff(-3, 3)));
  CHECK(rpg::lerpAngle(3, -3, 0.5) > 3.0);
}

TEST_CASE("expSmooth independe da taxa de quadros", "[core][math]") {
  // Dois passos de 1/60 equivalem a um de 1/30.
  const double k30 = rpg::expSmooth(12, 1.0 / 30);
  const double k60 = rpg::expSmooth(12, 1.0 / 60);
  CHECK(1 - (1 - k60) * (1 - k60) == Approx(k30));
}

TEST_CASE("jsRound e jsHypot reproduzem o Math do JavaScript", "[core][math]") {
  // Math.round: empate para +∞
  CHECK(rpg::jsRound(-2.5) == -2.0);
  CHECK(rpg::jsRound(-0.5) == 0.0);
  CHECK(rpg::jsRound(2.5) == 3.0);
  CHECK(rpg::jsRound(0.49999999999999994) == 0.0);
  // Math.hypot do V8 (valores conferidos no Node): difere de √(a² + b²) no último bit
  CHECK(rpg::jsHypot(3, 4) == 5.0);
  CHECK(rpg::jsHypot(0, 0) == 0.0);
  CHECK(rpg::jsHypot(-1e308, 1e308) > 1e308);  // sem estouro para ∞
  CHECK(std::isinf(rpg::jsHypot(std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN())));
  CHECK(rpg::signOr1(0.0) == 1.0);
  CHECK(rpg::signOr1(-0.0) == 1.0);
  CHECK(rpg::signOr1(-3.0) == -1.0);
}
