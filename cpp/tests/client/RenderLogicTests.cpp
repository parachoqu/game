// Lógica da fase 7 sem GPU: níveis de qualidade e ajuste automático (quality.js), câmera da sombra do
// sol, quem entra na passada de sombra e a cobertura do chão (ground-cover.js) a partir do bake.
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <set>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "TestData.h"
#include "client/assets/ScenePack.h"
#include "client/game/Quality.h"
#include "client/game/ShadowCamera.h"
#include "client/world/GroundCover.h"
#include "client/world/PackScene.h"
#include "core/Paths.h"

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

TEST_CASE("Qualidade: a tabela de quality.js e o repasse ao cenário", "[client][quality]") {
  const auto& levels = qualityLevels();
  REQUIRE(levels.size() == 3);
  CHECK(levels[0].key == "alta");
  CHECK(levels[2].key == "baixa");
  const QualityLevel& alta = qualityLevel("alta");
  CHECK(alta.gtao);
  CHECK(alta.shadow == 4096);
  CHECK(alta.shadowSpan == 70);
  CHECK(qualityLevel("media").shadow == 2048);
  CHECK_FALSE(qualityLevel("media").gtao);
  CHECK_FALSE(qualityLevel("baixa").post);
  CHECK(qualityLevel("ultra").key == "alta");  // desconhecido → alta
  CHECK(lowerQuality("alta") == std::optional<std::string_view>("media"));
  CHECK_FALSE(lowerQuality("baixa"));

  const WorldDetail d = detailFor(qualityLevel("baixa"));
  CHECK(d.blockFar == 700);
  CHECK(d.tileNear == 130);
  CHECK(d.secondaryDensity == Approx(0.65));
  CHECK(d.treeBands == std::vector<float>{0, 0, 70, 380});
  CHECK(d.coverRadius == 48);
  CHECK(d.coverDensity == Approx(0.35));
}

TEST_CASE("Qualidade: ajuste automático desce até duas vezes com a mediana acima de 30 ms", "[client][quality]") {
  AutoQuality a;
  std::string level = "alta";
  int changes = 0;
  for (int i = 0; i < 2000; ++i)
    if (const auto next = a.sample(45.0, level)) {
      level = std::string(*next);
      ++changes;
    }
  CHECK(changes == 2);
  CHECK(level == "baixa");

  AutoQuality fast;  // rápido: não muda e para de medir
  for (int i = 0; i < 500; ++i) CHECK_FALSE(fast.sample(10.0, "alta"));
  AutoQuality chosen(true);  // escolha salva: nunca ajusta
  for (int i = 0; i < 500; ++i) CHECK_FALSE(chosen.sample(100.0, "alta"));
  AutoQuality spikes;  // travadas isoladas (> 250 ms) não contam
  for (int i = 0; i < 1000; ++i) CHECK_FALSE(spikes.sample(400.0, "alta"));
}

TEST_CASE("Sombra do sol: o foco cai no centro do mapa e a câmera anda de texel em texel", "[client][shadow]") {
  const glm::vec3 toLight = glm::normalize(glm::vec3(0.4f, 0.8f, -0.3f));
  const glm::vec3 focus(37.3f, 18.2f, 391.7f);
  const ShadowCamera c = sunShadowCamera(toLight, focus, 70, 340, 4096);
  CHECK(c.texelWorld == Approx(140.0f / 4096.0f));
  const glm::vec4 p = c.viewProj * glm::vec4(focus, 1.0f);
  CHECK(std::abs(p.x) < 2.0f / 4096.0f);
  CHECK(std::abs(p.y) < 2.0f / 4096.0f);
  CHECK(p.z > 0.0f);
  CHECK(p.z < 1.0f);  // 160 m até a luz, dentro de 1–340 m
  // um ponto a 60 m do foco ainda cabe; a 80 m, não
  const glm::vec3 side = glm::normalize(glm::cross(toLight, glm::vec3(0, 1, 0)));
  CHECK(std::abs((c.viewProj * glm::vec4(focus + side * 60.0f, 1)).x) < 1.0f);
  CHECK(std::abs((c.viewProj * glm::vec4(focus + side * 80.0f, 1)).x) > 1.0f);
  // encaixe: andar um pouco desloca o mapa por um número inteiro de texels
  const ShadowCamera d = sunShadowCamera(toLight, focus + glm::vec3(0.013f, 0, 0.021f), 70, 340, 4096);
  const glm::vec4 o1 = c.viewProj * glm::vec4(0, 0, 0, 1), o2 = d.viewProj * glm::vec4(0, 0, 0, 1);
  const float shift = (o2.x - o1.x) * 2048.0f;
  CHECK(std::abs(shift - std::round(shift)) < 1e-2f);
  // sol a pino: sem degenerar
  const ShadowCamera z = sunShadowCamera(glm::vec3(0, 1, 0), focus, 70, 340, 1024);
  CHECK(std::isfinite(z.viewProj[0][0]));
}

TEST_CASE("Sombra do sol: quem entra na passada (castShadow, sombreadores da vegetação)", "[client][shadow][pack]") {
  if (!packAvailable()) SKIP("cpp/assets/client ausente");
  WorldView v(pack(), meshes());
  const glm::vec3 eye(40, 24, 405), at(40, 18, 375);
  const ShadowCamera sc = sunShadowCamera(glm::normalize(glm::vec3(0.4f, 0.8f, -0.3f)), at, 70, 340, 4096);
  PackFrame f;
  ShadowView sv{sc.viewProj, at, 55, 36};
  v.update(eye, viewProj(eye, at), glm::normalize(at - eye), DynamicSceneState{}, f, &sv);
  REQUIRE_FALSE(f.shadowDraws.empty());
  bool vegetation = false;
  for (const PackDrawCmd& d : f.shadowDraws) {
    REQUIRE((d.frameInstances ? f.instances.size() : v.staticInstances().size()) >= d.firstInstance + d.instanceCount);
    vegetation |= d.frameInstances && d.instanced;
  }
  CHECK(vegetation);  // sombreadores dos campos de LOD até 55 m
  // sem vegetação na sombra (nível baixo), menos chamadas; sem ShadowView, nenhuma
  PackFrame g;
  ShadowView none{sc.viewProj, at, 0, 36};
  v.update(eye, viewProj(eye, at), glm::normalize(at - eye), DynamicSceneState{}, g, &none);
  CHECK(g.shadowDraws.size() < f.shadowDraws.size());
  PackFrame h;
  v.update(eye, viewProj(eye, at), glm::normalize(at - eye), DynamicSceneState{}, h);
  CHECK(h.shadowDraws.empty());
}

TEST_CASE("Folhagem de dois lados: cartões repetidos ficam um só, o último (LessEqual)", "[client][pack]") {
  if (!packAvailable()) SKIP("cpp/assets/client ausente");
  const PackLodField* pine = nullptr;
  for (const PackLodField& f : pack().lodFields)
    if (f.name == "pine#0") pine = &f;
  REQUIRE(pine);
  REQUIRE(pine->levels.size() > 1);
  const auto check = [&](const PackLodPart& part, bool twoSided) {
    const PackGeometry& g = pack().geometries[static_cast<std::size_t>(part.geometry)];
    REQUIRE(g.index);
    const std::vector<std::uint32_t> raw = pack().decodeIndices(*g.index);
    const MeshSlice& s = meshes().geometries[static_cast<std::size_t>(part.geometry)];
    if (!twoSided) {
      CHECK(s.indexCount == raw.size());
      return;
    }
    // o kit repete cada cartão (A A D D): sobra bem menos da metade
    CHECK(s.indexCount * 2 < raw.size());
    // o último triângulo do original é sempre o último do seu grupo
    for (std::size_t k = 0; k < 3; ++k) CHECK(meshes().indices[s.firstIndex + s.indexCount - 3 + k] == raw[raw.size() - 3 + k]);
    // nenhum par coincidente
    std::set<std::array<float, 9>> seen;
    for (std::uint32_t t = 0; t < s.indexCount; t += 3) {
      std::array<std::array<float, 3>, 3> v{};
      for (std::size_t k = 0; k < 3; ++k) {
        const PackVertex& pv = meshes().vertices[static_cast<std::size_t>(s.baseVertex) + meshes().indices[s.firstIndex + t + k]];
        v[k] = {pv.pos[0], pv.pos[1], pv.pos[2]};
      }
      std::sort(v.begin(), v.end());
      std::array<float, 9> key{};
      for (std::size_t k = 0; k < 9; ++k) key[k] = v[k / 3][k % 3];
      CHECK(seen.insert(key).second);
    }
  };
  for (const PackLodPart& p : pine->levels[0]) check(p, pack().materials[static_cast<std::size_t>(p.material)].side == 2);
}

TEST_CASE("Cobertura do chão: células do bake, perto/longe e rarear pela distância", "[client][cover][pack]") {
  if (!packAvailable()) SKIP("cpp/assets/client ausente");
  const PackGroundCover& G = pack().groundCover;
  REQUIRE(G.present);
  REQUIRE(G.kinds.size() == 7);
  CHECK(G.kinds[0].name == "grass");
  CHECK(G.kinds[3].variants.size() == 5);  // seixos
  REQUIRE(G.count > 100000);

  // ground-cover.js `show`: densidade inteira até 30 m; 45% no raio
  CHECK(GroundCover::visibleCount(100, 10, 84, 1) == 100);
  CHECK(GroundCover::visibleCount(100, 84, 84, 1) == 45);
  CHECK(GroundCover::visibleCount(100, 10, 84, 0.35f) == 35);
  CHECK(GroundCover::visibleCount(100, 10, 84, 0) == 0);

  GroundCover cover(pack());
  REQUIRE(cover.present());
  // cada instância cai dentro da sua célula, sobre o relevo da demo
  const StaticMap& region = staticWorld().map(MapKind::Region);
  int checked = 0;
  for (std::size_t c = 0; c < cover.cellCount() && checked < 200; c += 37) {
    const PackCoverCell& cell = G.cells[c];
    for (std::size_t k = 0; k < cell.kinds.size(); ++k) {
      if (!cell.kinds[k].count) continue;
      const GroundCover::Item it = cover.item(c, k, 0);
      const auto x = static_cast<double>(it.x), y = static_cast<double>(it.y), z = static_cast<double>(it.z);
      CHECK(x >= G.origin + cell.i * G.cell - 0.01);
      CHECK(x <= G.origin + (cell.i + 1) * G.cell + 0.01);
      CHECK(std::abs(y - region.groundHeight(x, z)) < 0.3);
      CHECK(it.s > 0);
      ++checked;
    }
  }
  CHECK(checked > 20);

  // o mercado: grama e flores à vista, nenhuma no nível sem cobertura
  const glm::vec3 eye(40, 24, 405), at(40, 18, 375);
  PackFrame f;
  cover.update(eye, viewProj(eye, at), 84, 1, f);
  CHECK(f.instances.size() > 5000);
  CHECK_FALSE(f.draws.empty());
  for (const PackDrawCmd& d : f.draws) {
    CHECK(d.tint == Tint::Color);
    CHECK(f.instances.size() >= d.firstInstance + d.instanceCount);
  }
  PackFrame thin;
  cover.update(eye, viewProj(eye, at), 48, 0.35f, thin);
  CHECK(thin.instances.size() < f.instances.size() / 2);
}
