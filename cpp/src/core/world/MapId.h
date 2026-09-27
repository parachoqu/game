#pragma once
// Mapas do mundo. Substituem o teste `isTurbulentSpace(x)` da demo: cada entidade sabe em que mapa
// está, e cada mapa tem seu relevo, seus colisores e suas regras.
//
// Coordenadas: cada mapa usa o mesmo referencial da demo (a Turbulenta continua centrada em
// x = 1.400 m). Isso mantém a paridade bit a bit com o JS — subtrair o deslocamento mudaria o
// arredondamento das contas no último bit. Como o mapa vem da entidade, nenhuma regra depende mais
// de "x > 1.000"; o referencial é só um detalhe do mapa. Várias incursões na Turbulenta são várias
// instâncias do mesmo mapa estático, separadas por MapInstanceId (fase 6).
#include <cstdint>
#include <string_view>

namespace rpg {

enum class MapKind : std::uint8_t { Region, Turbulent };
inline constexpr int kMapCount = 2;

constexpr std::string_view mapKey(MapKind m) { return m == MapKind::Region ? "region" : "turbulent"; }

struct MapInstanceId {
  std::uint32_t value = 0;
  constexpr auto operator<=>(const MapInstanceId&) const = default;
};

}  // namespace rpg
