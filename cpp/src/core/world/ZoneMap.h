#pragma once
// Zonas de risco e nomes de lugar (game/layout.js `zoneAt` / `placeAt`).
//
// A simulação devolve ids; o nome escrito ("Mercado do Vale") é texto de interface e fica em
// data/text/pt-BR/places.json, lido só pelo cliente.
#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "core/Types.h"
#include "core/world/MapId.h"
#include "core/world/River.h"
#include "core/world/RouteField.h"

namespace rpg {

// As chaves são as de ZONES (config.js) e de data/zones.json.
enum class ZoneKind : std::uint8_t { Protegida, Fronteira, FullLoot, Turbulenta };
std::string_view zoneKey(ZoneKind z);

// Na ordem de decisão de `placeAt`. As chaves são as de data/text/pt-BR/places.json.
enum class PlaceId : std::uint8_t {
  Mercado, Vale, Alto, Acampamento, Observatorio, Mina, Caverna, Portal, Ermos, Canteiro, PonteMenor,
  Passagem, Bosque, EstradaLonga, RioLargo, CamposNorte, CamposVale, ColinasOeste, EncostasSerra, Turbulenta,
  Count,
};
std::string_view placeKey(PlaceId p);
std::optional<PlaceId> placeFromKey(std::string_view key);

// Um lugar de LOC: centro e raio (e os semieixos da elipse, nos Ermos).
struct PlaceArea {
  double x = 0, z = 0, r = 0;
  double rx = 0, rz = 0;
};

struct Circle {
  double x = 0, z = 0, r = 0;
};

class ZoneMap {
 public:
  struct Data {
    // LOC de layout.js, pelas chaves de lá
    PlaceArea vale, mercado, alto, canteiro, passagem, acampamento, observatorio, mina, caverna, portal, ponteMenor,
        ermos, bosque;
    std::vector<Circle> protectedPoints;  // entrepostos e mercado: sempre protegidos
    std::vector<Circle> neverProtected;   // acampamento e portal: sempre Fronteira
    std::vector<XZ> eastRoad;             // Estrada Longa (rota segura): protegida numa faixa de 12 m
    std::vector<XZ> mainRoad;             // rota curta pela garganta
  };

  ZoneMap() = default;
  explicit ZoneMap(Data d) : d_(std::move(d)) {}

  ZoneKind zoneAt(MapKind map, double x, double z) const;
  // `river` dá o eixo do Rio Largo (layout.js `riverZ`).
  PlaceId placeAt(MapKind map, double x, double z, const River& river) const;

  const Data& data() const { return d_; }

 private:
  bool inErmos(double x, double z) const;
  Data d_;
};

}  // namespace rpg
