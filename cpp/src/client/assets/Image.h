#pragma once
// Decodificação de imagens do pacote e dos GLB (WebP pela libwebp). RGBA8, linhas de cima para baixo.
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace rpg::client {

struct ImageRGBA {
  int w = 0, h = 0;
  std::vector<std::uint8_t> pixels;  // w × h × 4
  void flipRows();
};

// Lança std::runtime_error se o formato não for WebP ou estiver corrompido.
ImageRGBA decodeWebp(std::span<const std::uint8_t> bytes);
ImageRGBA loadWebp(const std::filesystem::path& file);

}  // namespace rpg::client
