#include <cmath>
#include <limits>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/Math.h"
#include "core/world/CollisionGrid.h"

using Catch::Approx;

TEST_CASE("Círculo empurra o corpo para fora na direção do centro", "[core][collision]") {
  rpg::CollisionGrid g;
  g.addCircle(5, 5, 1.0);
  const rpg::XZ p = g.resolve(5.5, 5, 0.45);  // 0,5 m do centro, raio somado 1,45
  CHECK(p.x == Approx(5 + 1.45));
  CHECK(p.z == Approx(5));
  CHECK(g.testPoint(5.5, 5, 0.45));
  CHECK_FALSE(g.testPoint(7, 5, 0.45));
  // fora do alcance nada muda
  const rpg::XZ far = g.resolve(8, 5, 0.45);
  CHECK(far.x == 8);
  CHECK(far.z == 5);
}

TEST_CASE("Caixa: sai pela face mais próxima, também girada", "[core][collision]") {
  rpg::CollisionGrid g;
  g.addBox(0, 0, 2, 1, 0);
  // dentro da caixa, perto da face +z: sai por z = hd + r
  rpg::XZ p = g.resolve(0.2, 0.9, 0.5);
  CHECK(p.x == Approx(0.2));
  CHECK(p.z == Approx(1.5));
  // perto de uma quina, por fora: empurrado ao longo da diagonal
  p = g.resolve(2.2, 1.2, 0.5);
  CHECK(rpg::jsHypot(p.x - 2, p.z - 1) == Approx(0.5));

  rpg::CollisionGrid r;
  r.addBox(0, 0, 2, 1, rpg::kPi / 2);  // girada 90°: a extensão de 2 m passa para z
  CHECK(r.testPoint(0, 1.8, 0.1));
  CHECK_FALSE(r.testPoint(1.8, 0, 0.1));
}

TEST_CASE("A ordem de inserção decide a sequência de empurrões", "[core][collision]") {
  // Dois círculos sobrepostos: o resultado depende de quem empurra primeiro, como no JS.
  rpg::CollisionGrid a, b;
  a.addCircle(0, 0, 1);
  a.addCircle(1.2, 0, 1);
  b.addCircle(1.2, 0, 1);
  b.addCircle(0, 0, 1);
  const rpg::XZ pa = a.resolve(0.6, 0.1, 0.3);
  const rpg::XZ pb = b.resolve(0.6, 0.1, 0.3);
  CHECK((pa.x != pb.x || pa.z != pb.z));
}

TEST_CASE("Colisor na borda de célula entra em todas as células que toca", "[core][collision]") {
  rpg::CollisionGrid g(12.0);
  g.addCircle(11.8, 0, 1);  // atravessa x = 12
  CHECK(g.testPoint(12.3, 0, 0.1));
  CHECK(g.testPoint(11.3, 0, 0.1));
}

TEST_CASE("Colisor de tamanho NaN fica fora da grade, como no JS", "[core][collision]") {
  rpg::CollisionGrid g;
  rpg::Collider nan;
  nan.type = rpg::Collider::Type::Box;
  nan.x = 3;
  nan.z = 3;
  nan.hw = nan.hd = nan.r = std::numeric_limits<double>::quiet_NaN();
  g.add(nan);
  CHECK(g.size() == 1);
  CHECK_FALSE(g.testPoint(3, 3, 0.5));
  const rpg::XZ p = g.resolve(3, 3, 0.5);
  CHECK(p.x == 3);
  CHECK(p.z == 3);
}
