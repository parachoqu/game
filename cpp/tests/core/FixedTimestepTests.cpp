#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/FixedTimestep.h"

using Catch::Approx;

TEST_CASE("FixedTimestep acumula e roda passos inteiros", "[core][timestep]") {
  rpg::FixedTimestep ts(0.1, 8);
  CHECK(ts.advance(0.05) == 0);
  CHECK(ts.alpha() == Approx(0.5));
  CHECK(ts.advance(0.06) == 1);
  CHECK(ts.alpha() == Approx(0.1).margin(1e-9));
  CHECK(ts.advance(0.35) == 3);
}

TEST_CASE("FixedTimestep descarta atraso grande em vez de disparar", "[core][timestep]") {
  rpg::FixedTimestep ts(0.1, 4);
  CHECK(ts.advance(10.0) == 4);  // no máximo maxSteps por chamada
  CHECK(ts.alpha() < 1.0);
  CHECK(ts.dropped() > 9.0);
  CHECK(ts.advance(0.0) == 0);   // não sobra fila para depois
}
