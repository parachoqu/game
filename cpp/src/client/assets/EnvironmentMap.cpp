#include "client/assets/EnvironmentMap.h"

#include <algorithm>
#include <cmath>

namespace rpg::client {

namespace {

constexpr double kPi = 3.141592653589793;

// Base real dos harmônicos (mesma ordem e constantes em shaders/src/ibl.glsl).
std::array<float, 9> shBasis(glm::vec3 n) {
  return {0.282095f,
          0.488603f * n.y,
          0.488603f * n.z,
          0.488603f * n.x,
          1.092548f * n.x * n.y,
          1.092548f * n.y * n.z,
          0.315392f * (3.0f * n.z * n.z - 1.0f),
          1.092548f * n.x * n.z,
          0.546274f * (n.x * n.x - n.y * n.y)};
}

}  // namespace

EnvironmentMap decodeRgbe(const ImageRGBA& img) {
  EnvironmentMap e;
  e.w = img.w;
  e.h = img.h;
  e.rgb.resize(static_cast<std::size_t>(img.w) * static_cast<std::size_t>(img.h) * 3);
  for (std::size_t i = 0, n = static_cast<std::size_t>(img.w) * static_cast<std::size_t>(img.h); i < n; ++i) {
    const std::uint8_t ex = img.pixels[i * 4 + 3];
    const double f = ex ? std::exp2(static_cast<double>(ex) - 128.0) / 255.0 : 0.0;
    for (std::size_t k = 0; k < 3; ++k) e.rgb[i * 3 + k] = static_cast<float>(img.pixels[i * 4 + k] * f);
  }
  return e;
}

glm::vec3 equirectDirection(double u, double v) {
  const double phi = (u - 0.5) * 2.0 * kPi, theta = (v - 0.5) * kPi;
  return {static_cast<float>(std::cos(theta) * std::cos(phi)), static_cast<float>(std::sin(theta)),
          static_cast<float>(std::cos(theta) * std::sin(phi))};
}

// Porte de Sky.SkyShader (vertex + fragment) para um ponto da esfera, sem nuvens nem disco.
glm::vec3 preethamRadiance(glm::vec3 dirIn, glm::vec3 sunDirIn) {
  using glm::dvec3;
  constexpr double turbidity = 4.5, rayleigh = 1.5, mieCoefficient = 0.004, mieDirectionalG = 0.86, exposure = 0.32;
  const dvec3 up(0, 1, 0);
  const dvec3 totalRayleigh(5.804542996261093e-6, 1.3562911419845635e-5, 3.0265902468824876e-5);
  const dvec3 mieConst(1.8399918514433978e14, 2.7798023919660528e14, 4.0790479543861094e14);
  constexpr double cutoffAngle = 1.6110731556870734, steepness = 1.5, EE = 1000.0;
  const dvec3 sunDir = glm::normalize(dvec3(sunDirIn));
  const dvec3 dir = glm::normalize(dvec3(dirIn));

  const double zc = std::clamp(glm::dot(sunDir, up), -1.0, 1.0);
  const double sunE = EE * std::max(0.0, 1.0 - std::exp(-((cutoffAngle - std::acos(zc)) / steepness)));
  const double sunfade = 1.0 - std::clamp(1.0 - std::exp(sunDir.y / 450000.0), 0.0, 1.0);
  const double rayleighCoefficient = rayleigh - (1.0 - sunfade);
  const dvec3 betaR = totalRayleigh * rayleighCoefficient;
  const dvec3 betaM = 0.434 * (0.2 * turbidity * 10e-18) * mieConst * mieCoefficient;

  const double zenithAngle = std::acos(std::max(0.0, glm::dot(up, dir)));
  const double inv = 1.0 / (std::cos(zenithAngle) + 0.15 * std::pow(93.885 - zenithAngle * 180.0 / kPi, -1.253));
  const double sR = 8.4e3 * inv, sM = 1.25e3 * inv;
  const dvec3 Fex = glm::exp(-(betaR * sR + betaM * sM));
  const double cosTheta = glm::dot(dir, sunDir);
  const double rc = cosTheta * 0.5 + 0.5;
  const double rPhase = 0.05968310365946075 * (1.0 + rc * rc);
  const dvec3 betaRTheta = betaR * rPhase;
  const double g2 = mieDirectionalG * mieDirectionalG;
  const double mPhase = 0.07957747154594767 * ((1.0 - g2) / std::pow(1.0 - 2.0 * mieDirectionalG * cosTheta + g2, 1.5));
  const dvec3 betaMTheta = betaM * mPhase;
  const dvec3 ratio = (betaRTheta + betaMTheta) / (betaR + betaM);
  dvec3 Lin = glm::pow(sunE * ratio * (1.0 - Fex), dvec3(1.5));
  const double k = std::clamp(std::pow(1.0 - glm::dot(up, sunDir), 5.0), 0.0, 1.0);
  Lin *= glm::mix(dvec3(1.0), glm::pow(sunE * ratio * Fex, dvec3(0.5)), k);
  const dvec3 L0 = 0.1 * Fex;
  const dvec3 tex = (Lin + L0) * 0.04 + dvec3(0.0, 0.0003, 0.00075);
  return glm::vec3(tex * exposure);
}

EnvironmentMap preethamEnvironment(glm::vec3 sunDir, int w, int h) {
  EnvironmentMap e;
  e.w = w;
  e.h = h;
  e.rgb.resize(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const glm::vec3 c = preethamRadiance(equirectDirection((x + 0.5) / w, (y + 0.5) / h), sunDir);
      const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) * 3;
      e.rgb[i] = c.r;
      e.rgb[i + 1] = c.g;
      e.rgb[i + 2] = c.b;
    }
  return e;
}

IrradianceSH irradianceSH(const EnvironmentMap& env) {
  std::array<glm::dvec3, 9> L{};
  for (int y = 0; y < env.h; ++y) {
    const double v = (y + 0.5) / env.h;
    const double theta = (v - 0.5) * kPi;
    const double dOmega = (2.0 * kPi / env.w) * (kPi / env.h) * std::cos(theta);
    for (int x = 0; x < env.w; ++x) {
      const glm::vec3 d = equirectDirection((x + 0.5) / env.w, v);
      const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(env.w) + static_cast<std::size_t>(x)) * 3;
      const glm::dvec3 c(env.rgb[i], env.rgb[i + 1], env.rgb[i + 2]);
      const auto b = shBasis(d);
      for (std::size_t k = 0; k < 9; ++k) L[k] += c * static_cast<double>(b[k]) * dOmega;
    }
  }
  // convolução com o cosseno (Ramamoorthi e Hanrahan): A0 = π, A1 = 2π/3, A2 = π/4
  const double A[9] = {kPi, 2 * kPi / 3, 2 * kPi / 3, 2 * kPi / 3, kPi / 4, kPi / 4, kPi / 4, kPi / 4, kPi / 4};
  IrradianceSH out{};
  for (std::size_t k = 0; k < 9; ++k) out[k] = glm::vec3(L[k] * A[k]);
  return out;
}

glm::vec3 evalIrradiance(const IrradianceSH& sh, glm::vec3 n) {
  const auto b = shBasis(n);
  glm::vec3 e(0.0f);
  for (std::size_t k = 0; k < 9; ++k) e += sh[k] * b[k];
  return glm::max(e, glm::vec3(0.0f));
}

void EnvironmentSelector::setKit(EnvironmentMap kit) {
  kitSh_ = irradianceSH(kit);
  kit_ = std::move(kit);
  key_.clear();
}

bool EnvironmentSelector::update(glm::vec3 sunDir, bool turbulent, double day) {
  const bool useKit = !turbulent && kit_ && day > 0.55;
  const auto r = [](float v) { return std::lround(v * 30.0f); };
  const std::string key = turbulent ? "turb" : useKit ? "kit" : std::to_string(r(sunDir.x)) + "," + std::to_string(r(sunDir.y)) + "," + std::to_string(r(sunDir.z));
  if (key == key_) return false;
  key_ = key;
  if (turbulent) {
    mode_ = 0;
    sh_ = {};
  } else if (useKit) {
    mode_ = 1;
    sh_ = kitSh_;
  } else {
    mode_ = 2;
    atmosphere_ = preethamEnvironment(sunDir);
    sh_ = irradianceSH(atmosphere_);
  }
  return true;
}

const EnvironmentMap* EnvironmentSelector::current() const {
  if (mode_ == 1) return &*kit_;
  if (mode_ == 2) return &atmosphere_;
  return nullptr;
}

float EnvironmentSelector::maxMip() const {
  const EnvironmentMap* e = current();
  return e ? std::log2(static_cast<float>(e->w) / 4.0f) : -1.0f;
}

}  // namespace rpg::client
