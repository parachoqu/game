#include "client/game/Lighting.h"

#include <cmath>

namespace rpg::client {

namespace {

float toLinear(float c) { return c < 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }

}  // namespace

glm::vec3 srgbHexToLinear(std::uint32_t rgb) {
  return {toLinear(static_cast<float>((rgb >> 16) & 255) / 255.0f), toLinear(static_cast<float>((rgb >> 8) & 255) / 255.0f),
          toLinear(static_cast<float>(rgb & 255) / 255.0f)};
}

SceneLighting lightingAt(double hour, bool turbulent) {
  SceneLighting L;
  const double a = (hour - 6) / 24 * 6.283185307179586;
  const Daylight d = daylightAt(hour);
  L.daylight = d;
  L.sunDir = glm::normalize(glm::vec3(static_cast<float>(std::cos(a) * 0.75), static_cast<float>(std::sin(a)), -0.45f));
  const float day = static_cast<float>(d.day), dusk = static_cast<float>(d.dusk), night = static_cast<float>(d.night);

  glm::vec3 top = glm::mix(srgbHexToLinear(0x070c1a), srgbHexToLinear(0x4f86c0), day);
  glm::vec3 hor = glm::mix(srgbHexToLinear(0x1b2940), srgbHexToLinear(0xbccbd6), day);
  top = glm::mix(top, srgbHexToLinear(0x3a4f7a), dusk * 0.5f);
  hor = glm::mix(hor, srgbHexToLinear(0xd8a484), dusk * 0.6f);
  if (turbulent) {
    top = srgbHexToLinear(0x150a24);
    hor = srgbHexToLinear(0x4a2c6a);
  }
  L.skyTop = top;
  L.skyHorizon = hor;
  L.sunAmount = turbulent ? 0.0f : day;
  L.lightDir = d.elev > -0.05 ? L.sunDir : -L.sunDir;
  L.sunColor = srgbHexToLinear(turbulent ? 0xb89aff : day > 0.5f ? 0xfff0da : dusk > 0.3f ? 0xffbf85 : 0xa9bde8);
  L.sunIntensity = turbulent ? 1.5f : 0.95f + day * 1.35f;
  L.hemiIntensity = turbulent ? 1.1f : 0.75f - day * 0.25f;
  L.hemiSky = srgbHexToLinear(turbulent ? 0xb8a4e8 : day > 0.4f ? 0xd4e6ff : 0x8497c8);
  L.hemiGround = srgbHexToLinear(turbulent ? 0x3a2a4a : 0x4d4234);
  L.fogColor = hor;
  L.fogDensity = turbulent ? 0.012f : 0.0011f + night * 0.0005f;
  L.envIntensity = turbulent ? 0.0f : 0.25f + day * 0.4f;
  return L;
}

}  // namespace rpg::client
