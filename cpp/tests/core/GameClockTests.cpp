#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/GameClock.h"

using Catch::Approx;

TEST_CASE("clockAt reproduz o clock() do state.js", "[core][clock]") {
  const rpg::ClockConfig cfg{420.0, 9.0};
  struct Case {
    double time;
    int day;
    double hour;
  };
  // Referência: clock(t) do JS com DAY_LENGTH = 420 e START_HOUR = 9.
  const Case cases[] = {
      {0, 1, 9.0},
      {1, 1, 9.0571428571428569},
      {157.5, 1, 18.0},
      {262.5, 2, 0.0},
      {419.99, 2, 8.9994285714285720},
      {1000, 3, 18.142857142857142},
      {5000.25, 13, 6.7285714285714286},
      {-10, 1, 8.4285714285714270},
  };
  for (const Case& c : cases) {
    const rpg::ClockTime t = rpg::clockAt(c.time, cfg);
    INFO("tempo " << c.time);
    CHECK(t.day == c.day);
    CHECK(t.hour == Approx(c.hour).margin(1e-12));
  }
}

TEST_CASE("secondsUntilHour: o atalho `night` da demo", "[core][clock]") {
  const rpg::ClockConfig cfg{420.0, 9.0};
  const double dt = rpg::secondsUntilHour(0.0, 22.0, cfg);  // das 9h às 22h: 13 h de 24
  CHECK(dt == Approx(13.0 / 24.0 * 420.0));
  CHECK(rpg::clockAt(dt, cfg).hour == Approx(22.0));
}
