#pragma once
// Atlas de glifos (FreeType): cada (fonte, tamanho, caractere) é rasterizado uma vez numa textura de
// 8 bits (cobertura). O texel (0,0)–(1,1) é branco, para retângulos sólidos no mesmo desenho.
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rpg::client {

struct Glyph {
  float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
  int w = 0, h = 0;
  int bearingX = 0, bearingY = 0;  // do ponto da linha de base ao canto superior esquerdo
  float advance = 0;
};

class FontAtlas {
 public:
  explicit FontAtlas(int size = 2048);
  ~FontAtlas();
  FontAtlas(const FontAtlas&) = delete;
  FontAtlas& operator=(const FontAtlas&) = delete;

  // Carrega uma fonte TrueType/OpenType; devolve o índice.
  int addFace(const std::filesystem::path& file);

  const Glyph& glyph(int face, int px, char32_t cp);
  float kerning(int face, int px, char32_t left, char32_t right);
  float ascender(int face, int px);
  float lineHeight(int face, int px);

  int size() const { return size_; }
  const std::vector<std::uint8_t>& pixels() const { return pixels_; }
  // Mudou desde a última marcação (o renderizador reenvia a textura).
  bool dirty() const { return dirty_; }
  void markClean() { dirty_ = false; }
  // Incrementa quando o atlas enche e é esvaziado (os desenhos do quadro precisam ser refeitos).
  std::uint32_t generation() const { return generation_; }

  float whiteU() const { return 1.0f / static_cast<float>(size_); }
  float whiteV() const { return 1.0f / static_cast<float>(size_); }

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  int size_;
  std::vector<std::uint8_t> pixels_;
  bool dirty_ = true;
  std::uint32_t generation_ = 0;
  int penX_ = 4, penY_ = 4, rowH_ = 0;
  std::unordered_map<std::uint64_t, Glyph> cache_;

  void reset();
  void setSize(int face, int px);
};

// Decodifica o próximo código UTF-8 de `s` a partir de `i` (avança `i`). Inválido vira U+FFFD.
char32_t nextCodepoint(std::string_view s, std::size_t& i);

}  // namespace rpg::client
