#include "client/Presentation.h"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "core/Paths.h"

namespace rpg::client {

std::uint32_t parseHexColor(std::string_view s) {
  if (!s.empty() && s[0] == '#') s.remove_prefix(1);
  std::uint32_t v = 0;
  for (char c : s.substr(0, 6)) {
    v <<= 4;
    if (c >= '0' && c <= '9') v |= static_cast<std::uint32_t>(c - '0');
    else if (c >= 'a' && c <= 'f') v |= static_cast<std::uint32_t>(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') v |= static_cast<std::uint32_t>(c - 'A' + 10);
  }
  return v;
}

namespace {

nlohmann::json readJson(const std::filesystem::path& p) {
  std::ifstream f(p);
  if (!f) throw std::runtime_error("não foi possível abrir " + pathToUtf8(p));
  return nlohmann::json::parse(f);
}

}  // namespace

Presentation Presentation::load(const std::filesystem::path& dir) {
  Presentation p;
  const nlohmann::json origins = readJson(dir / "origins.json");
  for (const auto& [k, v] : origins.items())
    p.origins[k] = {parseHexColor(v.value("skin", "#c99a78")), parseHexColor(v.value("cloth", "#3f6f8f")),
                    parseHexColor(v.value("trim", "#c9a25a"))};
  const nlohmann::json items = readJson(dir / "items.json");
  for (const auto& [k, v] : items.items()) p.items[k] = {v.value("glyph", ""), parseHexColor(v.value("color", "#cccccc"))};
  const nlohmann::json zones = readJson(dir / "zones.json");
  for (const auto& [k, v] : zones.items()) p.zones[k] = {parseHexColor(v.value("color", "#ffffff")), v.value("mark", "")};
  return p;
}

const OriginLook& Presentation::origin(std::string_view key) const {
  static const OriginLook fallback;
  const auto it = origins.find(key);
  return it == origins.end() ? fallback : it->second;
}

const ItemLook& Presentation::item(std::string_view key) const {
  static const ItemLook fallback;
  const auto it = items.find(key);
  return it == items.end() ? fallback : it->second;
}

const ZoneLook& Presentation::zone(ZoneKind z) const {
  static const ZoneLook fallback;
  const auto it = zones.find(zoneKey(z));
  return it == zones.end() ? fallback : it->second;
}

}  // namespace rpg::client
