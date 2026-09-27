#pragma once
// PNG sem dependências (capturas de tela automáticas): RGBA 8 bits, deflate em blocos sem compressão.
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace rpg::client {

std::vector<std::uint8_t> encodePng(std::span<const std::uint8_t> rgba, int width, int height);
bool writePng(const std::filesystem::path& file, std::span<const std::uint8_t> rgba, int width, int height);

}  // namespace rpg::client
