#include "client/assets/Image.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include <webp/decode.h>

#include "core/Paths.h"

namespace rpg::client {

void ImageRGBA::flipRows() {
  const std::size_t row = static_cast<std::size_t>(w) * 4;
  for (int y = 0; y < h / 2; ++y)
    std::swap_ranges(pixels.begin() + static_cast<std::ptrdiff_t>(y * row), pixels.begin() + static_cast<std::ptrdiff_t>((y + 1) * row),
                     pixels.begin() + static_cast<std::ptrdiff_t>((h - 1 - y) * row));
}

ImageRGBA decodeWebp(std::span<const std::uint8_t> bytes) {
  ImageRGBA img;
  if (!WebPGetInfo(bytes.data(), bytes.size(), &img.w, &img.h)) throw std::runtime_error("imagem WebP inválida");
  img.pixels.resize(static_cast<std::size_t>(img.w) * static_cast<std::size_t>(img.h) * 4);
  if (!WebPDecodeRGBAInto(bytes.data(), bytes.size(), img.pixels.data(), img.pixels.size(), img.w * 4))
    throw std::runtime_error("falha ao decodificar WebP");
  return img;
}

ImageRGBA loadWebp(const std::filesystem::path& file) {
  std::ifstream f(file, std::ios::binary);
  if (!f) throw std::runtime_error("textura não encontrada: " + pathToUtf8(file));
  const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  return decodeWebp(bytes);
}

}  // namespace rpg::client
