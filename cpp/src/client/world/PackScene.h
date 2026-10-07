#pragma once
// Cena do pacote visual pronta para a GPU, e o que desenhar a cada quadro (sem SDL; testável).
//
//   PackMeshes  — todos os vértices e índices num só par de buffers: as geometrias do pacote e os
//                 blocos de relevo (perto e longe) refeitos do heightfield com os pesos do bake.
//   WorldView   — a lógica de visibilidade da demo:
//                   relevo: `updateWorldDetail` (perto/longe com histerese de 20 m, Turbulenta só
//                           quando a câmera está a menos de blockFar/2 + 200 m dela);
//                   cenário: recorte por frustum e distância (`blockFar`, extras com `extraFar`);
//                   vegetação: `updateLODFields` (nível por instância, histerese de 3 m);
//                   modelos dinâmicos: pontos de coleta (escala 0,55 esgotados), santuários e a
//                           paliçada extra do acampamento.
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "client/assets/ScenePack.h"
#include "client/geom/MeshData.h"

namespace rpg {
class StaticWorld;
}

namespace rpg::client {

// Vértice único de todas as malhas do pacote (56 bytes; ver mesh.vert).
struct PackVertex {
  float pos[3];
  float normal[3];
  float uv[2];
  std::uint16_t color[4];  // half float, linear (cor de vértice do three)
  std::uint8_t splat[12];  // pesos das 10 camadas do relevo (u8 normalizado)
  float depth;             // profundidade local da água (margem)
};
static_assert(sizeof(PackVertex) == 56);

// Pele de um vértice (skinIndex/skinWeight), num fluxo à parte só com as geometrias dos personagens.
struct SkinVertex {
  std::uint16_t joints[4];
  float weights[4];
};
static_assert(sizeof(SkinVertex) == 24);

struct MeshSlice {
  std::uint32_t firstIndex = 0, indexCount = 0;
  std::int32_t baseVertex = 0;
  std::int32_t skinBase = -1;  // primeiro vértice em PackMeshes::skin (-1 = sem pele)
  glm::vec3 boundsMin{0.0f}, boundsMax{0.0f};
};

struct TerrainSlice {
  std::array<MeshSlice, 2> lod;  // 0 perto, 1 longe
  glm::vec3 center{0.0f};
  float radius = 0;
  float x = 0, z = 0, flatRadius = 0;  // centro e raio no plano (updateWorldDetail)
  int map = 0, material = -1;
};

struct PackMeshes {
  std::vector<PackVertex> vertices;
  std::vector<std::uint32_t> indices;
  std::vector<MeshSlice> geometries;  // por geometria do pacote
  std::vector<TerrainSlice> terrain;  // por bloco de relevo do pacote
  std::vector<SkinVertex> skin;       // pele das geometrias com skinIndex/skinWeight
};
PackMeshes buildPackMeshes(const ScenePack& pack, const StaticWorld& world);

// Matiz da instância (InstancedMesh.instanceColor): nenhuma, copa (rgb), tronco (a) ou rgb livre.
enum class Tint : std::uint8_t { None, Foliage, Trunk, Color };

// Uma chamada de desenho de malha do pacote.
struct PackDrawCmd {
  int geometry = -1, material = -1;
  std::uint32_t firstInstance = 0, instanceCount = 1;
  bool frameInstances = false;  // instâncias do quadro (senão, do buffer estático)
  bool instanced = false;       // InstancedMesh (o vento usa a posição da instância)
  bool mirrored = false;        // determinante negativo: inverte a face da frente
  Tint tint = Tint::None;
  int renderOrder = 0;
  float viewDepth = 0;          // para ordenar os transparentes
  std::int32_t boneBase = -1;   // malha com pele: primeira matriz da paleta em PackFrame::bones
};

struct PackFrame {
  std::vector<std::pair<int, int>> terrain;  // (bloco, nível)
  std::vector<PackDrawCmd> draws;
  std::vector<Instance> instances;           // instâncias deste quadro
  std::vector<glm::mat4> bones;              // paletas das malhas com pele (no mundo)
  std::vector<PackDrawCmd> shadowDraws;      // passada de sombra do sol (instâncias em `instances`)
  std::uint32_t lodInstances = 0;            // vegetação ativa (relatórios)
};

// Sombra do sol neste quadro: quem entra na passada de profundidade. Cada objeto segue o castShadow
// da demo; a vegetação desenha os sombreadores de lod-field.js até `vegetationFar` da câmera
// (arbustos até 60% disso) e os personagens só perto do foco (characters.js, 34–38 m).
struct ShadowView {
  glm::mat4 viewProj{1.0f};  // ShadowCamera::viewProj
  glm::vec3 focus{0.0f};
  float vegetationFar = 55;  // LOD_BANDS.shadowFar; 0 = vegetação sem sombra
  float characterFar = 36;
};

// Alcances de `quality.js` (nível Alta por padrão).
struct WorldDetail {
  float tileNear = 300, blockFar = 1600, extraFar = 900;
  std::vector<float> treeBands{28, 65, 150, 700}, shrubBands{22, 50, 360};
  float secondaryDensity = 1;
  float coverRadius = 84, coverDensity = 1;  // cobertura do chão (setGroundCoverQuality)
};

// Estado dos modelos dinâmicos do cenário (vem do mundo replicado).
// Um modelo do pacote posto no mundo neste quadro (portal, saco de carga, projétil, arma…).
struct ModelInstance {
  const std::vector<PackDraw>* parts = nullptr;
  glm::mat4 root{1.0f};
  bool portal = false;  // turbulent.js: disco pulsando e estilhaços em órbita
  // sem `parts`: uma geometria só (malha rígida de personagem), com `root` como matriz de mundo
  int geometry = -1, material = -1;
  std::optional<glm::vec4> color;  // matiz livre (multiplica a cor; alfa = opacidade)
};

// Malha com pele de um personagem neste quadro. A paleta (osso_mundo · inversa · bindMatrix) fica em
// DynamicSceneState::bones a partir de `boneBase`.
struct SkinnedInstance {
  int geometry = -1, material = -1;
  glm::mat4 mesh{1.0f};  // matriz de mundo da malha (as normais seguem a do three)
  std::uint32_t boneBase = 0;
  glm::vec3 center{0.0f};  // esfera para o recorte
  float radius = 2.0f;
  std::optional<glm::vec4> color;
};

struct DynamicSceneState {
  std::vector<bool> nodeDepleted;  // por índice de nó do pacote
  std::vector<bool> shrineTaken;
  bool campPalisade = false;  // camp.js: só com o acampamento "pressionado"
  double time = 0;            // G.time (o cristal dos santuários gira e flutua)
  std::vector<ModelInstance> models;
  std::vector<SkinnedInstance> skinned;
  std::vector<glm::mat4> bones;
};

class GroundCover;

class WorldView {
 public:
  WorldView(const ScenePack& pack, const PackMeshes& meshes);
  ~WorldView();

  void setDetail(const WorldDetail& d);
  const WorldDetail& detail() const { return detail_; }

  // Instâncias fixas (cenário estático e InstancedMesh do bake), sobem para a GPU uma vez.
  const std::vector<Instance>& staticInstances() const { return staticInstances_; }

  // Monta o quadro para a câmera (posição e viewProj).
  // Com `shadow`, também monta PackFrame::shadowDraws (recortados pelo volume da câmera do sol).
  void update(glm::vec3 cam, const glm::mat4& viewProj, const glm::vec3& camForward, const DynamicSceneState& dyn, PackFrame& out,
              const ShadowView* shadow = nullptr);

  // updateLODFields isolado (testes): devolve o nível atual de cada instância de um campo.
  void updateLod(glm::vec2 cam, bool force = false);
  const std::vector<std::uint8_t>& lodLevels(std::size_t field, std::size_t chunk) const { return fields_[field].chunks[chunk].level; }
  std::uint32_t activeCount(std::size_t field) const;

 private:
  struct LodChunk {
    std::uint32_t n = 0;
    float cx = 0, cz = 0, radius = 0;
    glm::vec3 center{0.0f};
    float sphereRadius = 0;
    std::vector<float> px, pz;
    std::vector<Instance> items;  // matriz + (copa rgb, tronco)
    std::vector<std::uint8_t> level;
    int mode = -2;
    std::uint32_t limit = 0;
  };
  struct LodFieldState {
    const PackLodField* src = nullptr;
    bool tree = true;
    std::vector<LodChunk> chunks;
  };
  struct StaticEntry {
    int draw = -1;  // índice em pack.statics
    glm::vec3 center{0.0f};
    float radius = 0;
    float cullFar = 0;  // 0 = sem limite próprio (só blockFar)
    bool mirrored = false;
  };
  struct InstancedEntry {
    int index = -1;
    std::uint32_t first = 0;
    glm::vec3 center{0.0f};
    float radius = 0;
  };

  void updateChunk(LodChunk& c, const std::vector<float>& bands, std::uint32_t limit, glm::vec2 cam);

  const ScenePack& pack_;
  const PackMeshes& meshes_;
  WorldDetail detail_;
  std::unique_ptr<GroundCover> cover_;
  std::vector<Instance> staticInstances_;
  std::vector<StaticEntry> statics_;
  std::vector<InstancedEntry> instanced_;
  std::vector<LodFieldState> fields_;
  std::vector<std::uint8_t> tileNear_;
  bool lodDirty_ = true;
  glm::vec2 lastLodCam_{1e30f};
};

}  // namespace rpg::client
