#include "core/GameClock.h"

#include <algorithm>
#include <cmath>

#include "core/JsMath.h"
#include "core/Math.h"

namespace rpg {

ClockTime clockAt(Seconds gameTime, const ClockConfig& cfg) {
  const double total = gameTime + (cfg.startHour / 24.0) * cfg.dayLength;
  ClockTime c;
  c.day = static_cast<int>(std::floor(total / cfg.dayLength)) + 1;
  c.hour = (std::fmod(total, cfg.dayLength) / cfg.dayLength) * 24.0;  // fmod tem o sinal do dividendo, como `%`
  return c;
}

Seconds secondsUntilHour(Seconds gameTime, double hour, const ClockConfig& cfg) {
  const double now = clockAt(gameTime, cfg).hour;
  return std::fmod(hour - now + 24.0, 24.0) / 24.0 * cfg.dayLength;
}

}  // namespace rpg

namespace rpg {
namespace {

// THREE.MathUtils.smoothstep(x, min, max)
double threeSmoothstep(double x, double min, double max) {
  if (x <= min) return 0;
  if (x >= max) return 1;
  x = (x - min) / (max - min);
  return x * x * (3 - 2 * x);
}

}  // namespace

Daylight daylightAt(double hour) {
  Daylight d;
  const double a = (hour - 6) / 24 * kPi * 2;
  d.elev = js::sin(a);
  d.day = threeSmoothstep(d.elev, -0.12, 0.25);
  d.dusk = std::max(0.0, 1 - std::fabs(d.elev) / 0.3) * (d.elev > -0.25 ? 1 : 0);
  d.night = 1 - threeSmoothstep(d.elev, -0.22, 0.02);
  return d;
}

}  // namespace rpg
