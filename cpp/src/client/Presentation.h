#pragma once
// Dados só de apresentação (data/presentation): cores de origem, ícones e cores de itens, cores e
// marcas das zonas.
#include <cstdint>
#include <filesystem>
#include <map>
#include <string>

#include "core/world/ZoneMap.h"

namespace rpg::client {

struct OriginLook {
  std::uint32_t skin = 0xc99a78, cloth = 0x3f6f8f, trim = 0xc9a25a;
};
struct ItemLook {
  std::string glyph;
  std::uint32_t color = 0xcccccc;
};
struct ZoneLook {
  std::uint32_t color = 0xffffff;
  std::string mark;
};

struct Presentation {
  std::map<std::string, OriginLook, std::less<>> origins;
  std::map<std::string, ItemLook, std::less<>> items;
  std::map<std::string, ZoneLook, std::less<>> zones;

  static Presentation load(const std::filesystem::path& dir);
  const OriginLook& origin(std::string_view key) const;
  const ItemLook& item(std::string_view key) const;
  const ZoneLook& zone(ZoneKind z) const;
};

std::uint32_t parseHexColor(std::string_view s);

}  // namespace rpg::client
