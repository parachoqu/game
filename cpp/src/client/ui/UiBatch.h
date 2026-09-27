#pragma once
// Desenho imediato da interface 2D: retângulos e texto viram triângulos num único buffer, com o
// atlas de glifos como textura. Coordenadas em pixels lógicos, origem no canto superior esquerdo.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "client/ui/FontAtlas.h"

namespace rpg::client {

struct Rgba {
  std::uint8_t r = 255, g = 255, b = 255, a = 255;
  static Rgba hex(std::uint32_t rgb, float alpha = 1.0f) {
    return {static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8), static_cast<std::uint8_t>(rgb),
            static_cast<std::uint8_t>(alpha * 255.0f + 0.5f)};
  }
  // "#rrggbb"
  static Rgba css(std::string_view s, float alpha = 1.0f);
  Rgba withAlpha(float alpha) const { return {r, g, b, static_cast<std::uint8_t>(alpha * 255.0f + 0.5f)}; }
  std::uint32_t packed() const {
    return static_cast<std::uint32_t>(r) | (static_cast<std::uint32_t>(g) << 8) | (static_cast<std::uint32_t>(b) << 16) |
           (static_cast<std::uint32_t>(a) << 24);
  }
};

struct UiVertex {
  float x, y, u, v;
  std::uint32_t color;  // RGBA8 (r no byte baixo)
};

enum class Align : std::uint8_t { Left, Center, Right };

// Fontes da interface (índices no FontAtlas).
struct UiFonts {
  int display = 0;  // Fraunces (títulos)
  int body = 0;     // DejaVu Sans
  int bold = 0;     // DejaVu Sans Bold
};

class UiBatch {
 public:
  explicit UiBatch(FontAtlas& atlas) : atlas_(&atlas) {}

  void clear() { verts_.clear(); }
  const std::vector<UiVertex>& vertices() const { return verts_; }
  FontAtlas& atlas() { return *atlas_; }

  void rect(float x, float y, float w, float h, Rgba c);
  void frame(float x, float y, float w, float h, float t, Rgba c);  // contorno
  void quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Rgba c);

  // Texto numa linha; `y` é o topo da linha. Devolve a largura.
  float text(int face, int px, float x, float y, std::string_view s, Rgba c, Align align = Align::Left);
  // Texto com sombra (text-shadow: 0 1px 2px #000).
  float textShadow(int face, int px, float x, float y, std::string_view s, Rgba c, Align align = Align::Left);
  float measure(int face, int px, std::string_view s);
  // Quebra em linhas de até `maxW` pixels; desenha e devolve a altura usada.
  float paragraph(int face, int px, float x, float y, float maxW, std::string_view s, Rgba c, float lineGap = 1.3f);
  std::vector<std::string> wrap(int face, int px, float maxW, std::string_view s);

 private:
  FontAtlas* atlas_;
  std::vector<UiVertex> verts_;
};

}  // namespace rpg::client
