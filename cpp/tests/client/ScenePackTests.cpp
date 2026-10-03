// Pacote visual do cliente (cpp/assets/client, Git LFS): carrega, decodifica cada fluxo e confere que
// o relevo refeito em C++ é o mesmo que a demo gerou (hash das posições e normais de cada bloco).
#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>

#include "TestData.h"
#include "client/assets/Image.h"
#include "client/assets/ScenePack.h"
#include "core/Paths.h"

using namespace rpg;
using namespace rpg::client;
using rpg::test::staticWorld;

namespace {

std::filesystem::path packDir() { return pathFromUtf8(RPG_TEST_ASSETS_DIR) / "client"; }

// Sem `git lfs pull`, os arquivos são ponteiros de texto do LFS: os testes se declaram pulados.
bool packAvailable() {
  std::ifstream f(packDir() / "scene.bin", std::ios::binary);
  char head[8] = {};
  return f && f.read(head, 7) && std::string(head, 7) != "version";
}

const ScenePack& pack() {
  static const ScenePack p = ScenePack::load(packDir());
  return p;
}

}  // namespace

TEST_CASE("Pacote visual: tabelas e fluxos decodificam", "[client][pack]") {
  if (!packAvailable()) SKIP("cpp/assets/client sem git lfs pull");
  const ScenePack& P = pack();
  REQUIRE(P.geometries.size() > 100);
  REQUIRE(P.materials.size() > 50);
  REQUIRE(P.terrain.size() == 272);
  REQUIRE(P.lodFields.size() > 5);
  REQUIRE(P.nodes.size() == staticWorld().layout().nodes.size());
  REQUIRE(P.templates.contains("portal"));
  for (const PackGeometry& g : P.geometries) {
    const auto pos = P.attributeFloats(g, "position");
    REQUIRE(pos.size() == g.count * 3u);
    if (g.index) {
      const auto idx = P.decodeIndices(*g.index);
      for (std::uint32_t i : idx) REQUIRE(i < g.count);
    }
  }
  for (const PackInstanced& in : P.instanced) CHECK(P.decodeVertices(in.matrices).size() == in.count * 64u);
  for (const PackLodField& f : P.lodFields)
    for (const PackLodChunk& c : f.chunks) CHECK(P.decodeVertices(c.items).size() == c.n * 80u);
  for (const PackMaterial& m : P.materials)
    for (int t : {m.map, m.normalMap, m.roughnessMap}) CHECK(t < static_cast<int>(P.textures.size()));
}

TEST_CASE("Pacote visual: toda imagem abre e tem o tamanho registrado", "[client][pack]") {
  if (!packAvailable()) SKIP("cpp/assets/client sem git lfs pull");
  const ScenePack& P = pack();
  for (const PackSource& s : P.sources)
    for (const std::string& f : s.files) {
      const ImageRGBA img = loadWebp(P.dir / f);
      CHECK(img.w == s.w);
      CHECK(img.h == s.h);
    }
}

TEST_CASE("Pacote visual: o relevo refeito do heightfield é o mesmo da demo", "[client][pack][parity]") {
  if (!packAvailable()) SKIP("cpp/assets/client sem git lfs pull");
  const ScenePack& P = pack();
  int checked = 0;
  for (const PackTerrainTile& t : P.terrain) {
    const StaticMap& map = staticWorld().map(t.map == 0 ? MapKind::Region : MapKind::Turbulent);
    for (const PackTerrainLod* l : {&t.near, &t.far}) {
      const TerrainTileMesh m = buildTerrainTile(map, t.x0, t.z0, l->n, l->step);
      CHECK(terrainTileHash(m) == l->hash);
      CHECK(P.decodeVertices(l->splat).size() == static_cast<std::size_t>(l->n) * l->n * 12);
      ++checked;
    }
  }
  CHECK(checked == 544);
}
