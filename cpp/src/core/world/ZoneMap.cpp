#include "core/world/ZoneMap.h"

#include <cmath>

#include "core/Math.h"

namespace rpg {

namespace {

constexpr std::array<std::string_view, 4> kZoneKeys{"protegida", "fronteira", "fullloot", "turbulenta"};

constexpr std::array<std::string_view, static_cast<std::size_t>(PlaceId::Count)> kPlaceKeys{
    "mercado", "vale", "alto", "acampamento", "observatorio", "mina", "caverna", "portal", "ermos", "canteiro",
    "ponteMenor", "passagem", "bosque", "estradaLonga", "rioLargo", "camposNorte", "camposVale", "colinasOeste",
    "encostasSerra", "turbulenta"};

double dist(double x, double z, const PlaceArea& l) { return jsHypot(x - l.x, z - l.z); }

}  // namespace

std::string_view zoneKey(ZoneKind z) { return kZoneKeys[static_cast<std::size_t>(z)]; }

std::optional<ZoneKind> zoneFromKey(std::string_view key) {
  for (std::size_t i = 0; i < kZoneKeys.size(); ++i)
    if (kZoneKeys[i] == key) return static_cast<ZoneKind>(i);
  return std::nullopt;
}

std::string_view placeKey(PlaceId p) { return kPlaceKeys[static_cast<std::size_t>(p)]; }

std::optional<PlaceId> placeFromKey(std::string_view key) {
  for (std::size_t i = 0; i < kPlaceKeys.size(); ++i)
    if (kPlaceKeys[i] == key) return static_cast<PlaceId>(i);
  return std::nullopt;
}

bool ZoneMap::inErmos(double x, double z) const {
  const PlaceArea& e = d_.ermos;
  const double ex = (x - e.x) / e.rx, ez = (z - e.z) / e.rz;
  return ex * ex + ez * ez < 1;
}

// layout.js zoneAt
ZoneKind ZoneMap::zoneAt(MapKind map, double x, double z) const {
  if (map == MapKind::Turbulent) return ZoneKind::Turbulenta;
  if (inErmos(x, z)) return ZoneKind::FullLoot;
  for (const Circle& p : d_.neverProtected)
    if (jsHypot(x - p.x, z - p.z) < p.r) return ZoneKind::Fronteira;
  for (const Circle& p : d_.protectedPoints)
    if (jsHypot(x - p.x, z - p.z) < p.r) return ZoneKind::Protegida;
  if (distToPolyline(x, z, d_.eastRoad) < 12) return ZoneKind::Protegida;
  return ZoneKind::Fronteira;
}

// layout.js placeAt, na mesma ordem de decisão.
PlaceId ZoneMap::placeAt(MapKind map, double x, double z, const River& river) const {
  if (map == MapKind::Turbulent) return PlaceId::Turbulenta;
  const Data& L = d_;
  if (dist(x, z, L.mercado) < L.mercado.r) return PlaceId::Mercado;
  if (dist(x, z, L.vale) < L.vale.r) return PlaceId::Vale;
  if (dist(x, z, L.alto) < L.alto.r) return PlaceId::Alto;
  if (dist(x, z, L.acampamento) < L.acampamento.r + 10) return PlaceId::Acampamento;
  if (dist(x, z, L.observatorio) < L.observatorio.r + 14) return PlaceId::Observatorio;
  if (dist(x, z, L.mina) < L.mina.r + 10) return PlaceId::Mina;
  if (dist(x, z, L.caverna) < L.caverna.r + 10) return PlaceId::Caverna;
  if (dist(x, z, L.portal) < L.portal.r + 10) return PlaceId::Portal;
  if (inErmos(x, z)) return PlaceId::Ermos;
  if (dist(x, z, L.canteiro) < L.canteiro.r + 8) return PlaceId::Canteiro;
  if (dist(x, z, L.ponteMenor) < L.ponteMenor.r + 8) return PlaceId::PonteMenor;
  if (dist(x, z, L.passagem) < L.passagem.r) return PlaceId::Passagem;
  if (dist(x, z, L.bosque) < L.bosque.r) return PlaceId::Bosque;
  if (distToPolyline(x, z, L.eastRoad) < 24) return PlaceId::EstradaLonga;
  if (std::fabs(z - river.centerAt(x)) < 26) return PlaceId::RioLargo;
  if (distToPolyline(x, z, L.mainRoad) < 24) return PlaceId::Passagem;
  if (z < -200) return PlaceId::CamposNorte;
  if (z > 240) return PlaceId::CamposVale;
  if (x < -180) return PlaceId::ColinasOeste;
  return PlaceId::EncostasSerra;
}

}  // namespace rpg
