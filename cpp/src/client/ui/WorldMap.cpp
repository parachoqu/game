#include "client/ui/WorldMap.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "client/Presentation.h"

namespace rpg::client {

namespace {

constexpr double kPi = 3.14159265358979323846;

std::uint8_t clampByte(double v) { return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0, 255.0))); }

double cross(std::pair<double, double> a, std::pair<double, double> b, double x, double y) {
  return (b.first - a.first) * (y - a.second) - (b.second - a.second) * (x - a.first);
}

double segDist(double x, double y, double ax, double ay, double bx, double by) {
  const double dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
  const double t = l2 > 0 ? std::clamp(((x - ax) * dx + (y - ay) * dy) / l2, 0.0, 1.0) : 0.0;
  return std::hypot(x - (ax + dx * t), y - (ay + dy * t));
}

// cor base por bioma da fonte (0 campo · 1 encosta · 2 rocha · 3 mata · 4 planalto seco · 5 água)
constexpr double kBiome[6][3] = {{96, 118, 70}, {104, 112, 66}, {128, 124, 116}, {64, 92, 58}, {140, 126, 84}, {46, 92, 112}};

}  // namespace

// ---------------------------------------------------------------- Raster

void Raster::blend(int x, int y, std::uint32_t rgb, double alpha, double coverage) {
  if (x < 0 || y < 0 || x >= w_ || y >= h_) return;
  const double a = std::clamp(alpha * coverage, 0.0, 1.0);
  if (a <= 0) return;
  std::uint8_t* p = &px_[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w_) + static_cast<std::size_t>(x)) * 4];
  const double r = ((rgb >> 16) & 255) * a, g = ((rgb >> 8) & 255) * a, b = (rgb & 255) * a;
  p[0] = clampByte(r + p[0] * (1 - a));
  p[1] = clampByte(g + p[1] * (1 - a));
  p[2] = clampByte(b + p[2] * (1 - a));
  p[3] = clampByte(a * 255 + p[3] * (1 - a));
}

void Raster::fillCircle(double cx, double cy, double r, std::uint32_t rgb, double alpha) {
  const int x0 = static_cast<int>(std::floor(cx - r - 1)), x1 = static_cast<int>(std::ceil(cx + r + 1));
  const int y0 = static_cast<int>(std::floor(cy - r - 1)), y1 = static_cast<int>(std::ceil(cy + r + 1));
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      const double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
      blend(x, y, rgb, alpha, std::clamp(r + 0.5 - d, 0.0, 1.0));
    }
}

void Raster::strokeCircle(double cx, double cy, double r, double width, std::uint32_t rgb, double alpha) {
  const double R = r + width;
  const int x0 = static_cast<int>(std::floor(cx - R - 1)), x1 = static_cast<int>(std::ceil(cx + R + 1));
  const int y0 = static_cast<int>(std::floor(cy - R - 1)), y1 = static_cast<int>(std::ceil(cy + R + 1));
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      const double d = std::abs(std::hypot(x + 0.5 - cx, y + 0.5 - cy) - r);
      blend(x, y, rgb, alpha, std::clamp(width / 2 + 0.5 - d, 0.0, 1.0));
    }
}

void Raster::fillRect(double x, double y, double w, double h, std::uint32_t rgb, double alpha) {
  for (int py = static_cast<int>(std::floor(y)); py < static_cast<int>(std::ceil(y + h)); ++py)
    for (int px = static_cast<int>(std::floor(x)); px < static_cast<int>(std::ceil(x + w)); ++px) {
      const double cx = std::clamp(std::min(px + 1.0, x + w) - std::max(static_cast<double>(px), x), 0.0, 1.0);
      const double cy = std::clamp(std::min(py + 1.0, y + h) - std::max(static_cast<double>(py), y), 0.0, 1.0);
      blend(px, py, rgb, alpha, cx * cy);
    }
}

void Raster::fillPolygon(const std::vector<std::pair<double, double>>& pts, std::uint32_t rgb, double alpha) {
  if (pts.size() < 3) return;
  double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
  for (const auto& [x, y] : pts) minX = std::min(minX, x), minY = std::min(minY, y), maxX = std::max(maxX, x), maxY = std::max(maxY, y);
  const auto inside = [&](double x, double y) {
    int wn = 0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
      const auto& a = pts[i];
      const auto& b = pts[(i + 1) % pts.size()];
      if (a.second <= y) {
        if (b.second > y && cross(a, b, x, y) > 0) ++wn;
      } else if (b.second <= y && cross(a, b, x, y) < 0) {
        --wn;
      }
    }
    return wn != 0;
  };
  for (int y = static_cast<int>(std::floor(minY)); y <= static_cast<int>(std::ceil(maxY)); ++y)
    for (int x = static_cast<int>(std::floor(minX)); x <= static_cast<int>(std::ceil(maxX)); ++x) {
      int n = 0;
      for (int sy = 0; sy < 4; ++sy)
        for (int sx = 0; sx < 4; ++sx) n += inside(x + (sx + 0.5) / 4, y + (sy + 0.5) / 4) ? 1 : 0;
      if (n) blend(x, y, rgb, alpha, n / 16.0);
    }
}

void Raster::strokePolygon(const std::vector<std::pair<double, double>>& pts, double width, std::uint32_t rgb, double alpha) {
  std::vector<std::pair<double, double>> closed = pts;
  if (!pts.empty()) closed.push_back(pts.front());
  strokePolyline(closed, width, rgb, alpha);
}

void Raster::fillPie(double cx, double cy, double r, double a0, double a1, std::uint32_t rgb, double alpha) {
  const int x0 = static_cast<int>(std::floor(cx - r - 1)), x1 = static_cast<int>(std::ceil(cx + r + 1));
  const int y0 = static_cast<int>(std::floor(cy - r - 1)), y1 = static_cast<int>(std::ceil(cy + r + 1));
  const double mid = (a0 + a1) / 2, half = (a1 - a0) / 2;
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      int n = 0;
      for (int sy = 0; sy < 4; ++sy)
        for (int sx = 0; sx < 4; ++sx) {
          const double dx = x + (sx + 0.5) / 4 - cx, dy = y + (sy + 0.5) / 4 - cy;
          if (dx * dx + dy * dy > r * r) continue;
          const double a = std::remainder(std::atan2(dy, dx) - mid, 2 * kPi);
          if (std::abs(a) <= half) ++n;
        }
      if (n) blend(x, y, rgb, alpha, n / 16.0);
    }
}

void Raster::strokePolyline(const std::vector<std::pair<double, double>>& pts, double width, std::uint32_t rgb, double alpha) {
  if (pts.size() < 2) return;
  const double hw = width / 2;
  double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
  for (const auto& [x, y] : pts) minX = std::min(minX, x), minY = std::min(minY, y), maxX = std::max(maxX, x), maxY = std::max(maxY, y);
  const int x0 = std::max(0, static_cast<int>(std::floor(minX - hw - 1))), x1 = std::min(w_ - 1, static_cast<int>(std::ceil(maxX + hw + 1)));
  const int y0 = std::max(0, static_cast<int>(std::floor(minY - hw - 1))), y1 = std::min(h_ - 1, static_cast<int>(std::ceil(maxY + hw + 1)));
  if (x1 < x0 || y1 < y0) return;
  const int bw = x1 - x0 + 1, bh = y1 - y0 + 1;
  std::vector<float> dist(static_cast<std::size_t>(bw) * static_cast<std::size_t>(bh), std::numeric_limits<float>::infinity());
  for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
    const auto [ax, ay] = pts[i];
    const auto [bx, by] = pts[i + 1];
    const int sx0 = std::max(x0, static_cast<int>(std::floor(std::min(ax, bx) - hw - 1)));
    const int sx1 = std::min(x1, static_cast<int>(std::ceil(std::max(ax, bx) + hw + 1)));
    const int sy0 = std::max(y0, static_cast<int>(std::floor(std::min(ay, by) - hw - 1)));
    const int sy1 = std::min(y1, static_cast<int>(std::ceil(std::max(ay, by) + hw + 1)));
    for (int y = sy0; y <= sy1; ++y)
      for (int x = sx0; x <= sx1; ++x) {
        float& d = dist[static_cast<std::size_t>(y - y0) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(x - x0)];
        d = std::min(d, static_cast<float>(segDist(x + 0.5, y + 0.5, ax, ay, bx, by)));
      }
  }
  for (int y = y0; y <= y1; ++y)
    for (int x = x0; x <= x1; ++x) {
      const double d = static_cast<double>(dist[static_cast<std::size_t>(y - y0) * static_cast<std::size_t>(bw) + static_cast<std::size_t>(x - x0)]);
      if (d < hw + 0.5) blend(x, y, rgb, alpha, std::clamp(hw + 0.5 - d, 0.0, 1.0));
    }
}

// ---------------------------------------------------------------- WorldMap

void WorldMap::build(const StaticWorld& world, const Presentation& look) {
  const StaticMap& region = world.map(MapKind::Region);
  const MapTerrain& T = region.terrain();
  half_ = region.half();
  base_ = Raster(kSize, kSize);
  const double span = half_ * 2 / kSize;
  const Heightfield::Meta& hm = T.heightfield().meta();
  const double lo = hm.min, hi = hm.min + hm.scale * 65535;
  for (int j = 0; j < kSize; ++j)
    for (int i = 0; i < kSize; ++i) {
      const double x = i * span - half_, z = j * span - half_;
      const double h = T.terrainHeight(x, z);
      const double slope = T.terrainHeight(x + span, z) - h;
      double r, g, b;
      if (T.isWaterCell(x, z)) {
        r = kBiome[5][0], g = kBiome[5][1], b = kBiome[5][2];
      } else {
        const int biome = T.biomeAt(x, z);
        const auto& rgb = kBiome[biome >= 0 && biome < 6 ? biome : 0];
        // altitude clareia, encosta virada para o oeste escurece
        const double t = std::clamp((h - lo) / (hi - lo), 0.0, 1.0);
        const double lift = 0.78 + t * 0.55;
        const double shade = std::clamp(-slope * 9, -34.0, 34.0);
        r = rgb[0] * lift + shade, g = rgb[1] * lift + shade, b = rgb[2] * lift + shade;
        const ZoneKind zone = world.zoneAt(MapKind::Region, x, z);
        if (zone != ZoneKind::Protegida) {
          const std::uint32_t zc = look.zone(zone).color;
          r = r * 0.8 + ((zc >> 16) & 255) * 0.2;
          g = g * 0.8 + ((zc >> 8) & 255) * 0.2;
          b = b * 0.8 + (zc & 255) * 0.2;
        }
      }
      std::uint8_t* p = &base_.pixels()[(static_cast<std::size_t>(j) * kSize + static_cast<std::size_t>(i)) * 4];
      // o canvas 2D trunca (Uint8ClampedArray arredonda; aqui como lá)
      p[0] = clampByte(r), p[1] = clampByte(g), p[2] = clampByte(b), p[3] = 255;
    }
  // estradas pela largura declarada de cada rota
  for (const Polyline& r : region.routes()) {
    std::vector<std::pair<double, double>> pts;
    for (const XZ& q : r.pts) pts.emplace_back(px(q.x), px(q.z));
    const double w = std::max(1.0, r.width / span);
    base_.strokePolyline(pts, w + 1.2, 0x3c2e20, 0.35);
    base_.strokePolyline(pts, w, r.risk == "alto" ? 0xc08a5a : 0xd3a76c, 1.0);
  }
  // assentamentos
  const ZoneMap::Data& L = world.zones().data();
  for (const PlaceArea* l : {&L.vale, &L.alto, &L.mercado}) base_.fillCircle(px(l->x), px(l->z), 6, 0xece4d2, 0.85);

  // lugares do mapa completo (map.js PLACES); a ruína de vigia sai como em layout.js
  double vx = 0, vz = 0;
  {
    const double mx = (L.canteiro.x + L.passagem.x) / 2, mz = (L.canteiro.z + L.passagem.z) / 2;
    double best = 1e18;
    for (const XZ& q : L.mainRoad)
      if (const double d = std::hypot(q.x - mx, q.z - mz); d < best) best = d, vx = q.x + 14, vz = q.z;
  }
  places_ = {{"vale", L.vale.x, L.vale.z},
             {"mercado", L.mercado.x, L.mercado.z},
             {"alto", L.alto.x, L.alto.z},
             {"canteiro", L.canteiro.x, L.canteiro.z},
             {"passagem", L.passagem.x, L.passagem.z},
             {"bosque", L.bosque.x, L.bosque.z},
             {"acampamento", L.acampamento.x, L.acampamento.z},
             {"observatorio", L.observatorio.x, L.observatorio.z},
             {"ermos", L.ermos.x, L.ermos.z},
             {"vigia", vx, vz},
             {"mina", L.mina.x, L.mina.z},
             {"caverna", L.caverna.x, L.caverna.z},
             {"portal", L.portal.x, L.portal.z}};
  built_ = true;
}

void WorldMap::sampleBase(Raster& out, double scale, double ox, double oz, bool clipCircle) const {
  const int W = out.width();
  const double R = W / 2.0;
  for (int y = 0; y < out.height(); ++y)
    for (int x = 0; x < W; ++x) {
      double cover = 1;
      if (clipCircle) cover = std::clamp(R + 0.5 - std::hypot(x + 0.5 - R, y + 0.5 - R), 0.0, 1.0);
      if (cover <= 0) continue;
      // drawImage com suavização: bilinear sobre o fundo (bordas repetidas)
      const double u = ox + (x + 0.5) / scale - 0.5, v = oz + (y + 0.5) / scale - 0.5;
      const int u0 = static_cast<int>(std::floor(u)), v0 = static_cast<int>(std::floor(v));
      const double fu = u - u0, fv = v - v0;
      double c[3] = {0, 0, 0};
      for (int k = 0; k < 4; ++k) {
        const int su = std::clamp(u0 + (k & 1), 0, kSize - 1), sv = std::clamp(v0 + (k >> 1), 0, kSize - 1);
        const double wgt = (k & 1 ? fu : 1 - fu) * (k >> 1 ? fv : 1 - fv);
        const std::uint8_t* p = &base_.pixels()[(static_cast<std::size_t>(sv) * kSize + static_cast<std::size_t>(su)) * 4];
        for (int ch = 0; ch < 3; ++ch) c[ch] += p[ch] * wgt;
      }
      const auto rgb = (static_cast<std::uint32_t>(clampByte(c[0])) << 16) | (static_cast<std::uint32_t>(clampByte(c[1])) << 8) | clampByte(c[2]);
      out.blend(x, y, rgb, 1.0, cover);
    }
}

void WorldMap::markers(Raster& c, double s, double ox, double oz, const MapMarkers& m) const {
  const auto tr = [&](double x, double z) { return std::pair<double, double>{(px(x) - ox) * s, (px(z) - oz) * s}; };
  // saco da própria carga
  for (const auto& [bx, bz] : m.ownBags) {
    const auto [x, y] = tr(bx, bz);
    const std::vector<std::pair<double, double>> d = {{x, y - 5}, {x + 4, y}, {x, y + 5}, {x - 4, y}};
    c.fillPolygon(d, 0xf2c86a, 1);
    c.strokePolygon(d, 1.5, 0x000000, 1);
  }
  if (m.portal) {
    const auto [x, y] = tr(m.portalX, m.portalZ);
    c.strokeCircle(x, y, 5 + std::sin(m.time * 4) * 1.5, 2, 0xc9a8ff, 1);
  }
  if (m.mount) {
    const auto [x, y] = tr(m.mountX, m.mountZ);
    c.fillRect(x - 3, y - 3, 6, 6, 0xc8b49a, 1);
  }
  // jogador (com o cone de visão da câmera, que pode estar girada)
  const auto [x, y] = tr(m.playerX, m.playerZ);
  const double lookA = std::atan2(-std::cos(m.camYaw), -std::sin(m.camYaw));
  c.fillPie(x, y, 34 * std::min(1.6, s), lookA - 0.5, lookA + 0.5, 0xece4d2, 0.32);
  const double rot = -m.playerYaw + kPi, cr = std::cos(rot), sr = std::sin(rot);
  std::vector<std::pair<double, double>> arrow;
  for (const auto& [ax, ay] : std::vector<std::pair<double, double>>{{0, -7}, {5, 5}, {0, 2.5}, {-5, 5}})
    arrow.emplace_back(x + ax * cr - ay * sr, y + ax * sr + ay * cr);
  c.fillPolygon(arrow, 0xffffff, 1);
  c.strokePolygon(arrow, 1.5, 0x0d141b, 1);
}

void WorldMap::minimapFrame(double x, double z, int size, bool big, double& scale, double& ox, double& oz) const {
  // o mundo tem 2,5× a largura do recorte antigo: a escala acompanha para o enquadramento cobrir a
  // mesma distância em metros
  scale = big ? 1.4 : 2.2;
  ox = px(x) - size / 2.0 / scale;
  oz = px(z) - size / 2.0 / scale;
}

void WorldMap::drawMinimap(Raster& out, const MapMarkers& m, bool big) const {
  out.clear();
  if (!built_) return;
  const int W = out.width();
  if (m.turbulent) {
    // fundo violeta; o "sem referência" é texto do documento
    for (int y = 0; y < W; ++y)
      for (int x = 0; x < W; ++x) out.blend(x, y, 0x1a0f28, 1, std::clamp(W / 2.0 + 0.5 - std::hypot(x + 0.5 - W / 2.0, y + 0.5 - W / 2.0), 0.0, 1.0));
    return;
  }
  double scale = 1, ox = 0, oz = 0;
  minimapFrame(m.playerX, m.playerZ, W, big, scale, ox, oz);
  sampleBase(out, scale, ox, oz, true);
  // os marcadores também ficam dentro do círculo
  Raster layer(W, W);
  markers(layer, scale, ox, oz, m);
  const double R = W / 2.0;
  auto& dst = out.pixels();
  const auto& src = layer.pixels();
  for (int y = 0; y < W; ++y)
    for (int x = 0; x < W; ++x) {
      const double cover = std::clamp(R + 0.5 - std::hypot(x + 0.5 - R, y + 0.5 - R), 0.0, 1.0);
      const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(W) + static_cast<std::size_t>(x)) * 4;
      const double a = src[i + 3] / 255.0 * cover;
      if (a <= 0) continue;
      for (int ch = 0; ch < 3; ++ch) dst[i + static_cast<std::size_t>(ch)] = clampByte(src[i + static_cast<std::size_t>(ch)] * cover + dst[i + static_cast<std::size_t>(ch)] * (1 - a));
      dst[i + 3] = clampByte(a * 255 + dst[i + 3] * (1 - a));
    }
}

void WorldMap::drawFull(Raster& out, const MapMarkers& m) const {
  out.clear();
  if (!built_) return;
  const double s = static_cast<double>(out.width()) / kSize;
  sampleBase(out, s, 0, 0, false);
  if (!m.turbulent) markers(out, s, 0, 0, m);
}

}  // namespace rpg::client
