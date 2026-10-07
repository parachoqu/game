// Cena do pacote sem GPU: malhas únicas, visibilidade (relevo, cenário, vegetação com LOD),
// materiais, luz de ambiente e céu.
#include <cmath>
#include <filesystem>
#include <fstream>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "TestData.h"
#include "client/assets/EnvironmentMap.h"
#include "client/assets/Image.h"
#include "client/assets/ScenePack.h"
#include "client/world/PackMaterials.h"
#include "client/world/EntityModels.h"
#include "client/world/PackScene.h"
#include "client/world/SkyView.h"
#include "core/Paths.h"
#include "core/protocol/Replicated.h"

using namespace rpg;
using namespace rpg::client;
using Catch::Approx;
using rpg::test::staticWorld;

namespace {

std::filesystem::path packDir() { return pathFromUtf8(RPG_TEST_ASSETS_DIR) / "client"; }

bool packAvailable() {
  std::ifstream f(packDir() / "scene.bin", std::ios::binary);
  char head[8] = {};
  return f && f.read(head, 7) && std::string(head, 7) != "version";
}

const ScenePack& pack() {
  static const ScenePack p = ScenePack::load(packDir());
  return p;
}

const PackMeshes& meshes() {
  static const PackMeshes m = buildPackMeshes(pack(), staticWorld());
  return m;
}

glm::mat4 viewProj(glm::vec3 eye, glm::vec3 at) {
  return glm::perspectiveRH_ZO(glm::radians(55.0f), 1100.0f / 700.0f, 0.1f, 2400.0f) * glm::lookAtRH(eye, at, glm::vec3(0, 1, 0));
}

}  // namespace

TEST_CASE("Cena do pacote: um buffer para tudo, faixas válidas", "[client][pack]") {
  if (!packAvailable()) SKIP("cpp/assets/client ausente");
  const PackMeshes& M = meshes();
  REQUIRE(M.geometries.size() == pack().geometries.size());
  REQUIRE(M.terrain.size() == pack().terrain.size());
  for (const MeshSlice& s : M.geometries) {
    REQUIRE(s.firstIndex + s.indexCount <= M.indices.size());
    REQUIRE(s.indexCount % 3 == 0);
  }
  for (const TerrainSlice& t : M.terrain)
    for (const MeshSlice& s : t.lod) {
      REQUIRE(s.indexCount > 0);
      REQUIRE(s.baseVertex >= 0);
    }
  // índices dentro dos vértices de cada geometria
  for (std::size_t g = 0; g < M.geometries.size(); ++g) {
    const MeshSlice& s = M.geometries[g];
    for (std::uint32_t i = 0; i < s.indexCount; ++i) REQUIRE(M.indices[s.firstIndex + i] < pack().geometries[g].count);
  }
}

TEST_CASE("Vegetação: nível por instância com histerese (updateLODFields)", "[client][pack]") {
  if (!packAvailable()) SKIP("cpp/assets/client ausente");
  WorldView v(pack(), meshes());
  const glm::vec2 cam(40, 375);
  v.updateLod(cam, true);
  const auto& fields = pack().lodFields;
  std::size_t checked = 0;
  for (std::size_t f = 0; f < fields.size(); ++f) {
    const auto& bands = fields[f].band == "tree" ? pack().lodBands.at("tree") : pack().lodBands.at("shrub");
    for (std::size_t c = 0; c < fields[f].chunks.size(); ++c) {
      const auto& levels = v.lodLevels(f, c);
      const auto raw = pack().decodeVertices(fields[f].chunks[c].items);
      for (std::size_t i = 0; i < levels.size(); ++i) {
        float m[20];
        std::memcpy(m, &raw[i * 80], 80);
        const float d = std::hypot(m[12] - cam.x, m[14] - cam.y);
        std::uint8_t want = 255;
        for (std::size_t b = 0; b < bands.size(); ++b)
          if (d < bands[b]) {
            want = static_cast<std::uint8_t>(b);
            break;
          }
        REQUIRE(levels[i] == want);
        ++checked;
      }
    }
  }
  REQUIRE(checked > 10000);
  // andar menos de 1,5 m não reavalia; perto de uma fronteira a histerese segura o nível
  const std::uint32_t before = v.activeCount(0);
  v.updateLod(cam + glm::vec2(1.0f, 0.0f));
  REQUIRE(v.activeCount(0) == before);
}

TEST_CASE("Visão do mundo: o mercado ao meio-dia", "[client][pack]") {
  if (!packAvailable()) SKIP("cpp/assets/client ausente");
  WorldView v(pack(), meshes());
  const glm::vec3 eye(40, 24, 405), at(40, 18, 375);
  PackFrame f;
  v.update(eye, viewProj(eye, at), glm::normalize(at - eye), DynamicSceneState{}, f);
  REQUIRE(f.terrain.size() > 20);
  REQUIRE(f.draws.size() > 30);
  REQUIRE(f.lodInstances > 1000);
  // os blocos da Turbulenta (a 1,4 km) ficam fora
  for (const auto& [tile, lod] : f.terrain) REQUIRE(meshes().terrain[static_cast<std::size_t>(tile)].map == 0);
  // blocos perto ficam no nível alto
  bool nearSeen = false;
  for (const auto& [tile, lod] : f.terrain) nearSeen |= lod == 0;
  REQUIRE(nearSeen);
  // transparentes por último, de trás para a frente
  bool seenTransparent = false;
  float lastDepth = 1e30f;
  for (const PackDrawCmd& d : f.draws) {
    const bool t = pack().materials[static_cast<std::size_t>(d.material)].transparent;
    if (t) {
      seenTransparent = true;
      REQUIRE(d.viewDepth <= lastDepth + 1e-3f);
      lastDepth = d.viewDepth;
    } else {
      REQUIRE_FALSE(seenTransparent);
    }
    REQUIRE((d.frameInstances ? f.instances.size() : v.staticInstances().size()) >= d.firstInstance + d.instanceCount);
  }
}

TEST_CASE("Materiais: recursos, mapas e estado do pipeline", "[client][pack]") {
  if (!packAvailable()) SKIP("cpp/assets/client ausente");
  bool terrain = false, water = false, foliage = false;
  for (const PackMaterial& m : pack().materials) {
    const MaterialSetup s = setupMaterial(pack(), m);
    if (m.features & kFeatSplat) {
      terrain = true;
      REQUIRE(s.terrain);
      REQUIRE(s.textures[0] >= 0);
      REQUIRE(pack().textures[static_cast<std::size_t>(s.textures[0])].array);
      REQUIRE(s.block.tscale[0] > 0);
    }
    if (m.features & kFeatWater) {
      water = true;
      REQUIRE(s.transparent);
      REQUIRE(s.blend == Blend::Alpha);
      REQUIRE_FALSE(s.depthWrite);
      REQUIRE(s.textures[1] >= 0);
    }
    if (m.alphaTest > 0 && m.side == 2) {
      foliage = true;
      REQUIRE(s.cull == Cull::None);
      REQUIRE(s.block.pbr[2] == Approx(m.alphaTest));
    }
    if (m.unlit()) REQUIRE(s.block.flags[1] == 2);
  }
  REQUIRE((terrain && water && foliage));
}

TEST_CASE("Luz de ambiente: harmônicos esféricos e o HDRI do kit", "[client][env]") {
  // ambiente constante: E(n) = π·L em qualquer direção
  EnvironmentMap flat;
  flat.w = 64;
  flat.h = 32;
  flat.rgb.assign(64 * 32 * 3, 0.5f);
  const IrradianceSH sh = irradianceSH(flat);
  for (const glm::vec3 n : {glm::vec3(0, 1, 0), glm::vec3(1, 0, 0), glm::vec3(0, -1, 0), glm::normalize(glm::vec3(1, 1, 1))})
    REQUIRE(evalIrradiance(sh, n).r == Approx(0.5 * 3.14159265).epsilon(0.02));
  // atmosfera: de dia o zênite é azulado e o horizonte, mais claro
  const glm::vec3 sun = glm::normalize(glm::vec3(0.0f, 1.0f, -0.45f));
  const glm::vec3 zenith = preethamRadiance({0, 1, 0}, sun), horizon = preethamRadiance(glm::normalize(glm::vec3(1, 0.05f, 0)), sun);
  REQUIRE(zenith.b > zenith.r);
  REQUIRE(horizon.r > zenith.r);
  // a seleção troca só quando a chave muda
  EnvironmentSelector sel;
  REQUIRE(sel.update(sun, false, 1.0));  // sem kit: atmosfera
  REQUIRE_FALSE(sel.update(sun, false, 1.0));
  REQUIRE(sel.current() != nullptr);
  REQUIRE(sel.update(sun, true, 1.0));    // Turbulenta: nenhum
  REQUIRE(sel.current() == nullptr);
  REQUIRE(sel.maxMip() < 0);
  if (packAvailable() && pack().environment.present) {
    const EnvironmentMap kit = decodeRgbe(loadWebp(pack().dir / pathFromUtf8(pack().environment.file)));
    REQUIRE(kit.w == 256);
    sel.setKit(kit);
    REQUIRE(sel.update(sun, false, 1.0));
    REQUIRE(sel.current()->w == 256);
    REQUIRE(sel.maxMip() == Approx(6.0f));
  }
}

TEST_CASE("Céu: estrelas, sumiço e lua", "[client][sky]") {
  SkyView sky;
  REQUIRE(sky.layout().stars.size() == 1700 + 6 + 7 + 8 + 6 + 1 + 12);
  REQUIRE(SkyView::goneAt(-1) == 0.0f);
  REQUIRE(SkyView::goneAt(3.5) == 1.0f);
  REQUIRE(SkyView::goneAt(1.5) <= 0.5f);
  SkyInput in;
  in.night = 1;
  in.sunDir = glm::normalize(glm::vec3(0.3f, -0.8f, -0.45f));
  in.time = 100;
  in.vanished.push_back({sky.layout().titleStar, 90});
  in.lineOpacity = 0.35f;
  SkyFrame f;
  sky.build(in, f);
  REQUIRE(f.stars.size() == sky.layout().stars.size());
  REQUIRE(f.stars[static_cast<std::size_t>(sky.layout().titleStar)].attr[2] == 1.0f);
  REQUIRE(f.starNight == 1.0f);
  REQUIRE(f.lines.size() % 2 == 0);
  REQUIRE(f.lines.size() == 2 * (7 + 6 + 7 + 5));
  REQUIRE(f.overlay.size() % 3 == 0);
  REQUIRE_FALSE(f.overlay.empty());  // lua à noite
  in.night = 0;
  in.lineOpacity = 0;
  sky.build(in, f);
  REQUIRE(f.overlay.empty());
  REQUIRE(f.lines.empty());
  REQUIRE(f.starNight == Approx(0.04f));
}

TEST_CASE("Modelos de entidades: projéteis pela escola e pelo dono, apontados no voo", "[client][pack]") {
  namespace proto = rpg::protocol;
  const auto type = [](proto::ProjectileKind k, int school, proto::ProjectileOwner o) {
    return static_cast<std::uint16_t>(static_cast<unsigned>(k) | (static_cast<unsigned>(school) << 4) | (static_cast<unsigned>(o) << 8));
  };
  REQUIRE(projectileModelKey(type(proto::ProjectileKind::Arrow, 0, proto::ProjectileOwner::Player)) == "arrow:player");
  REQUIRE(projectileModelKey(type(proto::ProjectileKind::Arrow, 0, proto::ProjectileOwner::Shadow)) == "arrow:shadow");
  REQUIRE(projectileModelKey(type(proto::ProjectileKind::Arrow, 0, proto::ProjectileOwner::Enemy)) == "arrow:enemy");
  REQUIRE(projectileModelKey(type(proto::ProjectileKind::Bolt, 0, proto::ProjectileOwner::Player)) == "bolt");
  REQUIRE(projectileModelKey(type(proto::ProjectileKind::Spell, 0, proto::ProjectileOwner::Enemy)) == "spell");
  REQUIRE(projectileModelKey(type(proto::ProjectileKind::Spell, 2, proto::ProjectileOwner::Player)) == "spell:fire");
  REQUIRE(projectileModelKey(type(proto::ProjectileKind::Arrow, 1, proto::ProjectileOwner::Player)) == "spell:ice");
  for (const glm::vec3 d : {glm::vec3(1, 0, 0), glm::vec3(0, 0, -1), glm::normalize(glm::vec3(0.3f, -0.4f, 0.8f)), glm::vec3(0, 1, 0)}) {
    const glm::vec3 z = glm::vec3(rotationFromZ(d) * glm::vec4(0, 0, 1, 0));
    REQUIRE(glm::length(z - d) < 1e-5f);
  }
  if (!packAvailable()) SKIP("cpp/assets/client ausente");
  for (const char* k : {"arrow:player", "arrow:shadow", "arrow:enemy", "bolt", "spell", "spell:ice", "spell:fire", "spell:nature", "spell:wind",
                        "spell:curse", "spell:vital", "spell:holy"})
    REQUIRE(pack().projectiles.contains(k));
  REQUIRE(pack().templates.contains("lootBagOwn"));
  int shards = 0;
  for (const PackDraw& d : pack().templates.at("portal")) shards += d.name == "shard";
  REQUIRE(shards == 5);
}
