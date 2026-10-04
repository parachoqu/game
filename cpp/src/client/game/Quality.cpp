#include "client/game/Quality.h"

#include <algorithm>

namespace rpg::client {

const std::array<QualityLevel, 3>& qualityLevels() {
  static const std::array<QualityLevel, 3> kLevels = [] {
    std::array<QualityLevel, 3> l{};
    // alta
    l[0].key = "alta";
    l[0].label = "Alta";
    l[0].dpr = 1.5;
    l[0].post = l[0].gtao = l[0].bloom = l[0].smaa = true;
    l[0].shadow = 4096;
    l[0].grass = 1;
    l[0].grassFar = 80;
    l[0].vegetation = 1;
    l[0].secondary = 1;
    l[0].vegShadow = true;
    l[0].tileNear = 300;
    l[0].blockFar = 1600;
    l[0].shadowSpan = 70;
    l[0].shadowFar = 340;
    l[0].turbFx = 1;
    l[0].natureFar = 360;
    l[0].canopyFar = 700;
    l[0].groundFar = 140;
    l[0].extraFar = 900;
    l[0].treeLod = {28, 65, 150, 700};
    l[0].shrubLod = {22, 50, 360};
    l[0].vegShadowFar = 55;
    l[0].coverRadius = 84;
    l[0].coverDensity = 1;
    // média
    l[1].key = "media";
    l[1].label = "Média";
    l[1].dpr = 1.25;
    l[1].post = true;
    l[1].bloom = l[1].smaa = true;
    l[1].shadow = 2048;
    l[1].grass = 0.45;
    l[1].grassFar = 64;
    l[1].vegetation = 0.72;
    l[1].secondary = 0.85;
    l[1].vegShadow = true;
    l[1].tileNear = 200;
    l[1].blockFar = 1100;
    l[1].shadowSpan = 58;
    l[1].shadowFar = 280;
    l[1].turbFx = 0.7;
    l[1].natureFar = 260;
    l[1].canopyFar = 500;
    l[1].groundFar = 100;
    l[1].extraFar = 650;
    l[1].treeLod = {0, 45, 120, 520};
    l[1].shrubLod = {0, 40, 260};
    l[1].vegShadowFar = 45;
    l[1].coverRadius = 68;
    l[1].coverDensity = 0.6;
    // baixa
    l[2].key = "baixa";
    l[2].label = "Baixa";
    l[2].dpr = 1;
    l[2].shadow = 1024;
    l[2].grass = 0.2;
    l[2].grassFar = 45;
    l[2].vegetation = 0.45;
    l[2].secondary = 0.65;
    l[2].vegShadow = false;
    l[2].tileNear = 130;
    l[2].blockFar = 700;
    l[2].shadowSpan = 46;
    l[2].shadowFar = 220;
    l[2].turbFx = 0.4;
    l[2].natureFar = 180;
    l[2].canopyFar = 350;
    l[2].groundFar = 70;
    l[2].extraFar = 450;
    l[2].treeLod = {0, 0, 70, 380};
    l[2].shrubLod = {0, 0, 180};
    l[2].vegShadowFar = 0;
    l[2].coverRadius = 48;
    l[2].coverDensity = 0.35;
    return l;
  }();
  return kLevels;
}

const QualityLevel& qualityLevel(std::string_view key) {
  for (const QualityLevel& q : qualityLevels())
    if (q.key == key) return q;
  return qualityLevels()[0];
}

std::optional<std::string_view> lowerQuality(std::string_view key) {
  const auto& l = qualityLevels();
  for (std::size_t i = 0; i + 1 < l.size(); ++i)
    if (l[i].key == key) return l[i + 1].key;
  return std::nullopt;
}

WorldDetail detailFor(const QualityLevel& q) {
  WorldDetail d;
  d.tileNear = static_cast<float>(q.tileNear);
  d.blockFar = static_cast<float>(q.blockFar);
  d.extraFar = static_cast<float>(q.extraFar);
  d.treeBands.assign(q.treeLod.begin(), q.treeLod.end());
  d.shrubBands.assign(q.shrubLod.begin(), q.shrubLod.end());
  d.secondaryDensity = static_cast<float>(q.secondary);
  d.coverRadius = static_cast<float>(q.coverRadius);
  d.coverDensity = static_cast<float>(q.coverDensity);
  return d;
}

std::optional<std::string_view> AutoQuality::sample(double ms, std::string_view current, bool visible) {
  if (done_ || steps_ >= 2) return std::nullopt;
  // aba oculta ou travada isolada não conta como lentidão
  if (!visible || ms > 250) {
    warm_ = std::min(warm_, 60);
    return std::nullopt;
  }
  if (warm_ < 90) {
    ++warm_;
    return std::nullopt;
  }
  samples_.push_back(ms);
  if (samples_.size() < 120) return std::nullopt;
  std::sort(samples_.begin(), samples_.end());
  const double med = samples_[samples_.size() >> 1];
  samples_.clear();
  warm_ = 30;
  const auto next = lowerQuality(current);
  if (med > 30 && next) {
    ++steps_;
    return next;
  }
  steps_ = 2;
  return std::nullopt;
}

}  // namespace rpg::client
