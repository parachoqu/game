#pragma once
// Mapa pré-renderizado da região (ui/map.js): o fundo sai do próprio mundo — relevo do heightfield,
// água pela máscara do rio, terreno pelos biomas, tinta das zonas e estradas pelas polilinhas. Em
// cima, os marcadores (jogador com o cone da câmera, carga própria, portal, montaria). Desenha em
// RGBA pré-multiplicado, pronto para virar a textura `dyn:minimap` / `dyn:fullmap` do RmlUi.
// Os nomes dos lugares não são rasterizados aqui: o documento os põe como texto por cima.
#include <cstdint>
#include <string>
#include <vector>

#include "core/world/StaticWorld.h"

namespace rpg::client {

struct Presentation;

// Tela RGBA8 pré-multiplicada com as primitivas que map.js usa no canvas 2D.
class Raster {
 public:
  Raster() = default;
  Raster(int w, int h) : w_(w), h_(h), px_(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4, 0) {}

  int width() const { return w_; }
  int height() const { return h_; }
  const std::vector<std::uint8_t>& pixels() const { return px_; }
  std::vector<std::uint8_t>& pixels() { return px_; }

  void clear() { std::fill(px_.begin(), px_.end(), std::uint8_t{0}); }
  // Mistura uma cor (sem pré-multiplicar, alfa 0–1) com cobertura 0–1 no pixel.
  void blend(int x, int y, std::uint32_t rgb, double alpha, double coverage);

  void fillCircle(double cx, double cy, double r, std::uint32_t rgb, double alpha);
  void strokeCircle(double cx, double cy, double r, double width, std::uint32_t rgb, double alpha);
  void fillRect(double x, double y, double w, double h, std::uint32_t rgb, double alpha);
  // Polígono (regra não-nula) e o contorno dele, com 4×4 amostras por pixel.
  void fillPolygon(const std::vector<std::pair<double, double>>& pts, std::uint32_t rgb, double alpha);
  void strokePolygon(const std::vector<std::pair<double, double>>& pts, double width, std::uint32_t rgb, double alpha);
  // Setor de círculo (o cone da câmera).
  void fillPie(double cx, double cy, double r, double a0, double a1, std::uint32_t rgb, double alpha);
  // Polilinha grossa com junções e pontas redondas (estradas): uma só mistura por pixel.
  void strokePolyline(const std::vector<std::pair<double, double>>& pts, double width, std::uint32_t rgb, double alpha);

 private:
  int w_ = 0, h_ = 0;
  std::vector<std::uint8_t> px_;
};

// O que map.js lê de G para os marcadores.
struct MapMarkers {
  double playerX = 0, playerZ = 0, playerYaw = 0, camYaw = 0;
  bool turbulent = false;  // o jogador está na Turbulenta: "sem referência"
  std::vector<std::pair<double, double>> ownBags;
  bool portal = false;
  double portalX = 0, portalZ = 0;
  bool mount = false;  // montaria presente, sem o jogador montado
  double mountX = 0, mountZ = 0;
  double time = 0;
};

// Nome de lugar sobre o mapa (map.js PLACES), em pixels da imagem.
struct MapLabel {
  double x = 0, y = 0;
  std::string key;  // chave do lugar em LOC (vale, mercado…)
  bool known = false;
};

class WorldMap {
 public:
  static constexpr int kSize = 512;  // 2 m por pixel na região de 1.024 m

  // buildBaseMap: o fundo da região.
  void build(const StaticWorld& world, const Presentation& look);
  bool built() const { return built_; }
  const Raster& base() const { return base_; }

  // drawMinimap: recorte redondo em volta do jogador; `big` mostra mais área.
  void drawMinimap(Raster& out, const MapMarkers& m, bool big) const;
  // drawFullMap: a região inteira em `out` (quadrado).
  void drawFull(Raster& out, const MapMarkers& m) const;

  // Lugares (chave, posição na imagem) para os nomes por cima; `discovered(key)` diz se é conhecido.
  template <class F>
  std::vector<MapLabel> labels(double scale, double ox, double oz, F&& discovered) const {
    std::vector<MapLabel> out;
    for (const auto& [key, x, z] : places_) {
      MapLabel l;
      l.x = (px(x) - ox) * scale;
      l.y = (px(z) - oz) * scale;
      l.key = key;
      l.known = key == "vale" || key == "mercado" || discovered(key);
      out.push_back(std::move(l));
    }
    return out;
  }
  // Origem e escala do minimapa para o jogador em (x, z) (as mesmas de drawMinimap).
  void minimapFrame(double x, double z, int size, bool big, double& scale, double& ox, double& oz) const;

  double px(double v) const { return (v + half_) / (half_ * 2) * kSize; }

 private:
  void markers(Raster& c, double s, double ox, double oz, const MapMarkers& m) const;
  void sampleBase(Raster& out, double scale, double ox, double oz, bool clipCircle) const;

  Raster base_;
  double half_ = 512;
  bool built_ = false;
  std::vector<std::tuple<std::string, double, double>> places_;
};

}  // namespace rpg::client
