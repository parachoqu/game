#include "core/GameClock.h"

#include <cmath>

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
