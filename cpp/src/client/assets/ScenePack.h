#pragma once
// Pacote visual do cliente (cpp/assets/client), gerado por demo/tools/bake-client-scene.mjs a partir
// da montagem de cena da própria demo: geometrias, materiais (com os recursos de `patchMaterial`),
// texturas, blocos de relevo (só os pesos das camadas; a malha sai do heightfield), malhas
// estáticas, instâncias, campos de LOD da vegetação e modelos dinâmicos.
//
// Os fluxos binários vêm comprimidos com o codec do meshoptimizer; `decode*` devolve os bytes crus.
#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <glm/glm.hpp>

namespace rpg {
class StaticMap;
}

namespace rpg::client {

// Recursos de shader de engine/materials.js (patchMaterial) e o material pulsante da anomalia.
enum MaterialFeature : std::uint32_t {
  kFeatTri = 1u << 0,
  kFeatOcc = 1u << 1,
  kFeatSplat = 1u << 2,
  kFeatWind = 1u << 3,
  kFeatGrass = 1u << 4,
  kFeatWater = 1u << 5,
  kFeatShore = 1u << 6,
  kFeatAnomaly = 1u << 7,
};

struct PackStream {
  std::uint64_t offset = 0, bytes = 0;
  std::uint32_t count = 0, stride = 0;
  enum class Codec : std::uint8_t { Raw, MeshoptVertex, MeshoptIndex } codec = Codec::Raw;
};

struct PackAttribute {
  bool normalizedU8 = false;  // u8 normalizado
  bool u16 = false;           // inteiro de 16 bits (skinIndex); senão float32
  int size = 3;               // componentes
  PackStream stream;
};

struct PackGeometry {
  std::uint32_t count = 0;
  std::map<std::string, PackAttribute, std::less<>> attributes;
  std::optional<PackStream> index;
  glm::vec3 boundsMin{0.0f}, boundsMax{0.0f};
};

struct PackSource {
  int w = 0, h = 0;
  std::vector<std::string> files;  // uma por camada (texturas de array têm várias)
};

enum class Wrap : std::uint8_t { Repeat, Clamp, Mirror };

struct PackMip {
  int w = 0, h = 0;
  std::string file;
};

struct PackTexture {
  int source = -1;
  bool flipY = false, srgb = false, array = false, mipmaps = true, nearest = false;
  int anisotropy = 1;
  Wrap wrapS = Wrap::Repeat, wrapT = Wrap::Repeat;
  std::vector<PackMip> mipLevels;  // mipmaps fornecidos (folhagem: preservam a cobertura)
  std::array<float, 9> uvMatrix{1, 0, 0, 0, 1, 0, 0, 0, 1};  // coluna a coluna (Matrix3 do three)
};

// Valor de uniform de um recurso (uScale, uNStr, uWind, uTint, uTScales, texturas…).
using UniformValue = std::variant<double, std::vector<double>, int /* textura */>;

struct PackMaterial {
  std::string name, type;  // MeshStandardMaterial, MeshLambertMaterial, MeshBasicMaterial…
  std::uint32_t features = 0;
  std::map<std::string, UniformValue, std::less<>> uniforms;
  glm::vec3 color{1.0f}, emissive{0.0f};
  float emissiveIntensity = 1, roughness = 1, metalness = 0, opacity = 1, alphaTest = 0, envMapIntensity = 1;
  bool transparent = false, vertexColors = false, flatShading = false, depthWrite = true, depthTest = true, additive = false;
  int side = 0;  // 0 frente, 1 trás, 2 os dois
  std::optional<glm::vec2> polygonOffset;
  glm::vec2 normalScale{1.0f};
  int map = -1, normalMap = -1, roughnessMap = -1, metalnessMap = -1, emissiveMap = -1, aoMap = -1, alphaMap = -1;

  bool unlit() const { return type == "MeshBasicMaterial"; }
  bool lambert() const { return type == "MeshLambertMaterial"; }
};

struct PackDraw {
  int geometry = -1, material = -1;
  std::uint32_t start = 0, count = 0;  // faixa de índices (0/0 = a geometria inteira)
  glm::mat4 matrix{1.0f};
  int map = 0;
  int renderOrder = 0;
  std::string group, name;
};

struct PackInstanced {
  int map = 0, geometry = -1, material = -1;
  std::uint32_t count = 0;
  std::string group;
  PackStream matrices;
  std::optional<PackStream> colors;
};

struct PackLodPart {
  int geometry = -1, material = -1;
  enum class Tint : std::uint8_t { None, Foliage, Trunk } tint = Tint::None;
};

struct PackLodChunk {
  std::uint32_t n = 0;
  float cx = 0, cz = 0, radius = 0;
  PackStream items;  // por instância: matriz (16 floats), cor da copa (3), tom do tronco (1)
};

struct PackLodField {
  std::string name, band;  // "tree" ou "shrub"
  bool secondary = false;
  int map = 0;
  std::vector<std::vector<PackLodPart>> levels;
  std::vector<PackLodChunk> chunks;
};

struct PackTerrainLod {
  int n = 0;          // vértices por lado
  double step = 0;    // m entre vértices
  PackStream splat;   // por vértice: 12 bytes (10 pesos + 2 de enchimento)
  std::uint32_t hash = 0;  // FNV-1a das posições e normais float32 que a demo gerou
};

struct PackTerrainTile {
  int map = 0;
  double x0 = 0, z0 = 0, size = 0;
  double x = 0, z = 0, radius = 0;
  int material = -1;
  PackTerrainLod near, far;
};

struct PackModel {
  glm::mat4 matrix{1.0f};          // posição no mundo (modelos dinâmicos do cenário)
  double x = 0, z = 0;
  std::string kind;
  std::vector<PackDraw> parts;     // matrizes relativas à raiz do modelo
};

// ---------------------------------------------------------------- personagens (models.js)
// Trilha de um clipe já adaptado ao modelo: nó do esqueleto, propriedade e quadros-chave.
struct PackTrack {
  int node = -1;
  enum class Path : std::uint8_t { Position, Quaternion, Scale, Other } path = Path::Other;
  std::uint32_t count = 0, size = 0;
  bool step = false;
  PackStream times, values;
};
struct PackClip {
  double duration = 0;
  std::vector<PackTrack> tracks;
};
struct PackRigMesh {
  int node = -1, geometry = -1, material = -1;
  bool skinned = false;
  PackStream bindMatrix, boneInverses;
  std::vector<int> bones;  // nós dos ossos, na ordem do esqueleto
};
struct PackGrip {  // suporte da arma preso ao osso da mão (characters.js `grip`)
  std::string bone;
  glm::vec3 t{0.0f}, s{1.0f};
  glm::vec4 r{0, 0, 0, 1};  // x, y, z, w (ordem do three)
};
struct PackCycle {
  double dur = 0, off = 0, speed = 0;
};
struct PackRig {
  std::string kind;  // humanoid, quadruped, hound
  std::vector<std::string> names;
  std::vector<int> parent;
  std::vector<bool> bone;
  PackStream rest;  // por nó: posição (3), quaternion xyzw (4), escala (3)
  std::vector<PackRigMesh> meshes;
  std::map<std::string, PackClip, std::less<>> clips;
  std::vector<std::string> clipOrder;  // ordem em que o mixer da demo ativa as ações
  double hipsY = 0, hipsBase = 0, height = 0, seatY = 0, seatZ = 0;
  std::map<std::string, glm::vec4, std::less<>> idleRef;  // pose ociosa dos ossos ampliados
  std::map<std::string, PackCycle, std::less<>> cycle;
  std::optional<PackGrip> gripR, gripL;
};
struct PackClipMeta {
  std::string contactMode, locomotionMode, group;
  std::optional<std::array<double, 2>> motion;  // janela de deslocamento do quadril (esquivas)
};

// HDRI reduzido do kit de natureza (RGBE em WebP) e as cores dominantes dele (kit-manifest.json).
struct PackEnvironment {
  bool present = false;
  std::string file;
  int w = 0, h = 0;
  std::uint32_t zenith = 0, horizon = 0, ground = 0;  // 0xRRGGBB (sRGB)
};

struct ScenePack {
  std::filesystem::path dir;
  std::vector<std::uint8_t> bin;
  std::vector<PackSource> sources;
  std::vector<PackTexture> textures;
  std::vector<PackMaterial> materials;
  std::vector<PackGeometry> geometries;
  std::vector<PackTerrainTile> terrain;
  std::vector<PackDraw> statics;
  std::vector<PackInstanced> instanced;
  std::vector<PackLodField> lodFields;
  std::map<std::string, std::vector<float>, std::less<>> lodBands;  // tree, shrub
  double shadowFar = 55;
  std::vector<PackModel> nodes, shrines;  // pontos de coleta (por índice do layout) e santuários
  std::optional<PackModel> campPalisade;
  std::map<std::string, std::vector<PackDraw>, std::less<>> templates;  // portal, lootBag, lootBagOwn
  std::map<std::string, std::vector<PackDraw>, std::less<>> weapons;    // família (":sombra" = brilho das sombras)
  std::map<std::string, std::vector<PackDraw>, std::less<>> projectiles;  // "arrow:player", "bolt", "spell:ice"…
  PackEnvironment environment;
  std::map<std::string, PackRig, std::less<>> rigs;
  std::map<std::string, PackClipMeta, std::less<>> clipMeta;

  // Lê scene.json + scene.bin (confere o tamanho). Lança std::runtime_error com o motivo.
  static ScenePack load(const std::filesystem::path& dir);

  // Bytes crus de um fluxo de vértices (count × stride) ou índices (count × uint32).
  std::vector<std::uint8_t> decodeVertices(const PackStream& s) const;
  std::vector<std::uint32_t> decodeIndices(const PackStream& s) const;
  // Atributo como floats (u8 normalizado vira [0, 1]).
  std::vector<float> attributeFloats(const PackGeometry& g, std::string_view name) const;
  std::vector<float> floats(const PackStream& s) const;  // fluxo de floats (trilhas, pose de repouso)
};

// Malha de um bloco de relevo refeita do heightfield (scene-world.js `tileGeometry`): posições,
// normais a 1 m e índices com a diagonal a–d. Mesmos floats que a demo gerou.
struct TerrainTileMesh {
  std::vector<float> positions, normals;
  std::vector<std::uint32_t> indices;
};
TerrainTileMesh buildTerrainTile(const StaticMap& map, double x0, double z0, int n, double step);
std::uint32_t terrainTileHash(const TerrainTileMesh& m);

}  // namespace rpg::client
