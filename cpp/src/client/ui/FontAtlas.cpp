#include "client/ui/FontAtlas.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "core/Paths.h"

namespace rpg::client {

struct FontAtlas::Impl {
  FT_Library lib = nullptr;
  std::vector<FT_Face> faces;
  std::vector<std::vector<std::uint8_t>> data;  // o FreeType lê a fonte da memória enquanto ela vive
  int curFace = -1, curPx = 0;
};

char32_t nextCodepoint(std::string_view s, std::size_t& i) {
  const auto c = static_cast<unsigned char>(s[i++]);
  if (c < 0x80) return c;
  int extra = 0;
  char32_t cp = 0;
  if ((c & 0xE0) == 0xC0) {
    extra = 1;
    cp = c & 0x1F;
  } else if ((c & 0xF0) == 0xE0) {
    extra = 2;
    cp = c & 0x0F;
  } else if ((c & 0xF8) == 0xF0) {
    extra = 3;
    cp = c & 0x07;
  } else {
    return 0xFFFD;
  }
  for (int k = 0; k < extra; ++k) {
    if (i >= s.size()) return 0xFFFD;
    const auto d = static_cast<unsigned char>(s[i]);
    if ((d & 0xC0) != 0x80) return 0xFFFD;
    cp = (cp << 6) | (d & 0x3F);
    ++i;
  }
  return cp;
}

FontAtlas::FontAtlas(int size) : impl_(std::make_unique<Impl>()), size_(size), pixels_(static_cast<std::size_t>(size) * size, 0) {
  if (FT_Init_FreeType(&impl_->lib) != 0) throw std::runtime_error("FreeType: falha ao iniciar");
  reset();
}

FontAtlas::~FontAtlas() {
  for (FT_Face f : impl_->faces) FT_Done_Face(f);
  if (impl_->lib) FT_Done_FreeType(impl_->lib);
}

void FontAtlas::reset() {
  std::fill(pixels_.begin(), pixels_.end(), std::uint8_t{0});
  for (int y = 0; y < 2; ++y)
    for (int x = 0; x < 2; ++x) pixels_[static_cast<std::size_t>(y) * size_ + x] = 255;
  penX_ = 4;
  penY_ = 4;
  rowH_ = 0;
  cache_.clear();
  dirty_ = true;
  ++generation_;
}

int FontAtlas::addFace(const std::filesystem::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) throw std::runtime_error("fonte não encontrada: " + pathToUtf8(file));
  std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  impl_->data.push_back(std::move(bytes));
  const auto& b = impl_->data.back();
  FT_Face face = nullptr;
  if (FT_New_Memory_Face(impl_->lib, b.data(), static_cast<FT_Long>(b.size()), 0, &face) != 0)
    throw std::runtime_error("fonte inválida: " + pathToUtf8(file));
  impl_->faces.push_back(face);
  return static_cast<int>(impl_->faces.size()) - 1;
}

void FontAtlas::setSize(int face, int px) {
  if (impl_->curFace == face && impl_->curPx == px) return;
  FT_Set_Pixel_Sizes(impl_->faces[static_cast<std::size_t>(face)], 0, static_cast<FT_UInt>(px));
  impl_->curFace = face;
  impl_->curPx = px;
}

const Glyph& FontAtlas::glyph(int face, int px, char32_t cp) {
  const std::uint64_t key = (static_cast<std::uint64_t>(face) << 56) | (static_cast<std::uint64_t>(px) << 40) | cp;
  if (const auto it = cache_.find(key); it != cache_.end()) return it->second;
  setSize(face, px);
  FT_Face f = impl_->faces[static_cast<std::size_t>(face)];
  Glyph g;
  if (FT_Load_Char(f, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT) == 0) {
    const FT_Bitmap& bm = f->glyph->bitmap;
    g.w = static_cast<int>(bm.width);
    g.h = static_cast<int>(bm.rows);
    g.bearingX = f->glyph->bitmap_left;
    g.bearingY = f->glyph->bitmap_top;
    g.advance = static_cast<float>(f->glyph->advance.x) / 64.0f;
    if (g.w > 0 && g.h > 0) {
      if (penX_ + g.w + 2 > size_) {
        penX_ = 4;
        penY_ += rowH_ + 2;
        rowH_ = 0;
      }
      if (penY_ + g.h + 2 > size_) {
        reset();
        return glyph(face, px, cp);
      }
      for (int y = 0; y < g.h; ++y)
        for (int x = 0; x < g.w; ++x)
          pixels_[static_cast<std::size_t>(penY_ + y) * size_ + static_cast<std::size_t>(penX_ + x)] =
              bm.buffer[static_cast<std::size_t>(y) * static_cast<std::size_t>(std::abs(bm.pitch)) + static_cast<std::size_t>(x)];
      const float s = static_cast<float>(size_);
      g.u0 = static_cast<float>(penX_) / s;
      g.v0 = static_cast<float>(penY_) / s;
      g.u1 = static_cast<float>(penX_ + g.w) / s;
      g.v1 = static_cast<float>(penY_ + g.h) / s;
      penX_ += g.w + 2;
      rowH_ = std::max(rowH_, g.h);
      dirty_ = true;
    }
  }
  return cache_.emplace(key, g).first->second;
}

float FontAtlas::kerning(int face, int px, char32_t left, char32_t right) {
  FT_Face f = impl_->faces[static_cast<std::size_t>(face)];
  if (!FT_HAS_KERNING(f)) return 0;
  setSize(face, px);
  FT_Vector k{};
  FT_Get_Kerning(f, FT_Get_Char_Index(f, left), FT_Get_Char_Index(f, right), FT_KERNING_DEFAULT, &k);
  return static_cast<float>(k.x) / 64.0f;
}

float FontAtlas::ascender(int face, int px) {
  setSize(face, px);
  return static_cast<float>(impl_->faces[static_cast<std::size_t>(face)]->size->metrics.ascender) / 64.0f;
}

float FontAtlas::lineHeight(int face, int px) {
  setSize(face, px);
  return static_cast<float>(impl_->faces[static_cast<std::size_t>(face)]->size->metrics.height) / 64.0f;
}

}  // namespace rpg::client
