#pragma once
// Chão de um mapa (engine/terrain.js): relevo, pisos elevados das pontes com rampa e água.
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "core/world/Heightfield.h"
#include "core/world/MapId.h"
#include "core/world/River.h"

namespace rpg {

// Piso elevado atravessável. Na demo todas as pontes têm rot = 0; c/s são o cos/sen de −rot
// calculados pelo próprio JS (o pacote os traz prontos para não depender da trigonometria da libm).
struct Bridge {
  std::string name;
  double x = 0, z = 0, hw = 0, hd = 0, y = 0;
  double rot = 0, c = 1, s = 0;
  double apron = 0, apronSide = 0;  // rampas ao longo da travessia e para os lados
};

class MapTerrain {
 public:
  static constexpr double kNoWater = -std::numeric_limits<double>::infinity();

  MapTerrain() = default;
  // `origin`: deslocamento do referencial do mapa na cena da demo (0 na região, x = 1.400 na
  // Turbulenta). `planZSign`: z_cena = planZSign · y_plano.
  MapTerrain(MapKind kind, Heightfield height, double originX, double originY, double originZ, double planZSign);

  // Só a região tem máscara de biomas, rio e pontes.
  void setRegionExtras(BiomeMask mask, River river, std::vector<Bridge> bridges);

  MapKind kind() const { return kind_; }

  // terrain.js `terrainHeight`: só o relevo.
  double terrainHeight(double x, double z) const;
  // terrain.js `groundHeight`: relevo + tabuleiro e rampas das pontes (o maior dos dois).
  double groundHeight(double x, double z) const;
  // terrain.js `waterAt`: altura da lâmina, ou kNoWater onde não há água.
  double waterAt(double x, double z) const;

  // heightfield.js (só região; na Turbulenta: 0 / false / 0)
  int biomeAt(double x, double z) const;
  bool isWaterCell(double x, double z) const;
  double slopeAt(double x, double z, double d = 1.0) const;

  const River& river() const { return river_; }
  const std::vector<Bridge>& bridges() const { return bridges_; }
  const Heightfield& heightfield() const { return height_; }

 private:
  MapKind kind_ = MapKind::Region;
  Heightfield height_;
  double originX_ = 0, originY_ = 0, originZ_ = 0, planZSign_ = -1;
  bool hasRegionExtras_ = false;
  BiomeMask mask_;
  River river_;
  std::vector<Bridge> bridges_;
};

}  // namespace rpg
