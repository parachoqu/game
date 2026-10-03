#pragma once
// Tradução de um material do pacote para o que a GPU usa: blocos de uniformes (espelhos de
// `Material` em surface.frag/terrain.frag e `Draw` em mesh.vert), texturas por slot e o estado do
// pipeline (face, mistura, profundidade, polygonOffset). Sem SDL; testável.
#include <array>
#include <cstdint>

#include "client/assets/ScenePack.h"

namespace rpg::client {

struct MaterialBlock {
  float color[4];     // rgb linear; a: opacidade
  float emissive[4];  // rgb × intensidade
  float pbr[4];       // x rugosidade, y metal, z alphaTest
  float tri[4];       // x uScale, y uNStr, zw normalScale
  float tint[4];      // uTint (relevo)
  float tscale[12];   // uTScales[10]
  std::int32_t flags[4];  // x recursos, y modelo, z mapas, w 1 chapado / 2 dois lados / 4 transparente
};
static_assert(sizeof(MaterialBlock) == 144);

struct DrawBlock {
  float wind[4];    // x uWind, y recursos de vértice (1 vento, 2 grama), z instanciado, w matiz (Tint)
  float uvRow0[4];
  float uvRow1[4];
  float flags[4];   // x: cores de vértice
};
static_assert(sizeof(DrawBlock) == 64);

enum class Cull : std::uint8_t { None, Back, Front };
enum class Blend : std::uint8_t { Opaque, Alpha, Additive };

inline constexpr std::size_t kSurfaceSlots = 5;  // map, normal, rough, metal, ao (terreno: uTA, uTN, uMacro)

struct MaterialSetup {
  MaterialBlock block{};
  DrawBlock draw{};
  std::array<int, kSurfaceSlots> textures{-1, -1, -1, -1, -1};
  bool terrain = false;
  bool transparent = false;
  Cull cull = Cull::Back;
  Blend blend = Blend::Opaque;
  bool depthWrite = true, depthTest = true;
  bool depthBias = false;
  float biasFactor = 0, biasUnits = 0;
};

MaterialSetup setupMaterial(const ScenePack& pack, const PackMaterial& m);

}  // namespace rpg::client
