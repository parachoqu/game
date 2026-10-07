#pragma once
// Esqueleto de um molde de personagem (models.js `mold` + `instantiate`), decodificado do pacote:
// nós da subárvore do pivot em ordem de traverse (pai antes do filho), pose de repouso, malhas com
// pele e os clipes já adaptados ao modelo. Compartilhado por todos os clones; cada personagem só
// guarda a própria `Pose`.
//
// Quaternions no glm (w, x, y, z); o pacote grava na ordem do three (x, y, z, w).
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "client/assets/ScenePack.h"

namespace rpg::client::anim {

// Pose local de cada nó (o que o mixer e as camadas procedurais escrevem).
struct Pose {
  std::vector<glm::vec3> t, s;
  std::vector<glm::quat> r;
};

struct Track {
  int node = -1;
  enum class Path : std::uint8_t { Position, Quaternion, Scale } path = Path::Position;
  bool step = false;
  std::uint32_t size = 3;
  std::vector<float> times, values;
};

struct Clip {
  std::string key;
  double duration = 0;
  std::vector<Track> tracks;
};

// Interpolantes do three (LinearInterpolant, QuaternionLinearInterpolant, DiscreteInterpolant):
// antes do primeiro quadro vale o primeiro, depois do último vale o último.
void sampleTrack(const Track& t, double time, float* out);

// Quaternion.slerpFlat / slerp do three (r185): t pode passar de 1 (ampBone amplia a pose).
glm::quat slerpThree(const glm::quat& a, const glm::quat& b, double t);

// "mixamorigHips" → "Hips" (models.js `boneKey`)
std::string_view boneKey(std::string_view name);

struct RigMesh {
  int node = -1, geometry = -1, material = -1;
  bool skinned = false;
  glm::mat4 bind{1.0f};
  std::vector<int> bones;
  std::vector<glm::mat4> inverses;
};

struct Grip {
  int node = -1;      // osso da mão
  glm::mat4 local{1.0f};
};

struct Rig {
  std::string id, kind;
  std::vector<std::string> names;
  std::vector<int> parent;
  std::vector<bool> isBone;
  Pose rest;
  std::vector<RigMesh> meshes;
  std::map<std::string, Clip, std::less<>> clips;
  std::vector<std::string> clipOrder;
  std::unordered_map<std::string, int> bones;  // boneKey → nó (o último osso com a chave, como boneMap)
  // humanoides
  double hipsY = 0, hipsBase = 0;
  std::map<std::string, glm::quat, std::less<>> idleRef;
  std::map<std::string, PackCycle, std::less<>> cycle;
  std::optional<Grip> gripR, gripL;
  // cavalo
  double seatY = 0, seatZ = 0;

  int bone(std::string_view key) const;
  const Clip* clip(std::string_view key) const;
  static Rig fromPack(const ScenePack& pack, const std::string& id);
};

// Matrizes de mundo de cada nó: `root` × cadeia de matrizes locais (TRS).
void worldMatrices(const Rig& rig, const Pose& pose, const glm::mat4& root, std::vector<glm::mat4>& out);
glm::mat4 composeTRS(const glm::vec3& t, const glm::quat& r, const glm::vec3& s);
// Euler XYZ do three: R = Rx · Ry · Rz
glm::mat4 eulerXYZ(const glm::vec3& r);

// characters.js `bend`: gira o osso em torno de um eixo do espaço do modelo (x: lado esquerdo, y: cima,
// z: frente), compondo a orientação dos pais até o pivot (nó 0, exclusive).
void bend(const Rig& rig, Pose& pose, std::string_view key, double x, double y, double z, double angle);

// O que os animadores consultam do mundo: onde o corpo está, para onde olha, a câmera (nível de
// detalhe da animação) e o chão (inclinação no declive).
struct AnimWorld {
  glm::vec3 rootPos{0.0f};
  double rootYaw = 0;
  glm::vec3 camera{0.0f};
  std::function<double(double, double)> groundHeight;
};

// characters.js `lodStep`: longe da câmera o esqueleto anda a cada 3 (45 m) ou 6 (90 m) quadros, com o
// tempo acumulado. Devolve 0 nos quadros pulados.
struct AnimLod {
  double acc = 0;
  std::uint32_t tick = 0;
  double step(double dt, const glm::vec3& pos, const glm::vec3& camera);
};

}  // namespace rpg::client::anim
