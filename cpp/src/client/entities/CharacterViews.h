#pragma once
// Personagens do mundo replicado com os moldes do pacote (characters.js + o que player.js,
// enemies.js, npcs.js e mount.js entregam ao animador). Sem SDL; testável.
//
//   CharacterLibrary — os moldes decodificados (Rig) e as variantes de material de characters.js
//                      (`tinted`, `shadowed`, os tons das feras), acrescentadas ao pacote antes de ele
//                      subir para a GPU;
//   CharacterViews   — uma vista por EntityId (jogadores, inimigos, NPCs, montarias): monta a entrada
//                      do animador a partir do EntityState, anima e escreve as malhas do quadro
//                      (paletas de ossos, partes rígidas e as armas presas às empunhaduras).
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "client/anim/HumanoidAnimator.h"
#include "client/anim/QuadrupedAnimator.h"
#include "client/assets/ScenePack.h"
#include "client/world/PackScene.h"
#include "core/protocol/Snapshot.h"

namespace rpg {
class StaticWorld;
struct GameData;
}  // namespace rpg

namespace rpg::client {

class ClientWorld;

// Aparências de characters.js: tom por facção, silhueta das sombras e os tons das feras.
enum class CharacterLook : std::uint8_t { Plain, Guard, Bandit, Shadow, HoundLeader, HoundPale, LionessMane, LionessPale };

class CharacterLibrary {
 public:
  // Decodifica os moldes e acrescenta as variantes a `pack.materials` (antes de Renderer::loadPack).
  explicit CharacterLibrary(ScenePack& pack);

  const anim::Rig* rig(std::string_view id) const;
  const anim::ClipMetaMap& clipMeta() const { return meta_; }
  // Material de cada malha do molde (na ordem de Rig::meshes) com a aparência pedida.
  const std::vector<int>& materials(std::string_view rig, CharacterLook look) const;

 private:
  std::map<std::string, anim::Rig, std::less<>> rigs_;
  anim::ClipMetaMap meta_;
  std::map<std::pair<std::string, CharacterLook>, std::vector<int>, std::less<>> materials_;
};

// characters.js RACE: escala (lados, altura) por origem.
glm::vec2 raceScale(std::string_view origin);
// Famílias que vão na mão esquerda (arco e tomos) e as que vão nas duas (manoplas e adaga).
bool weaponInLeftHand(std::string_view family);
bool weaponInBothHands(std::string_view family);

struct CharacterContext {
  const GameData* data = nullptr;
  const StaticWorld* statics = nullptr;
  glm::vec3 camera{0.0f};  // posição da câmera (nível de detalhe da animação)
  double dt = 1.0 / 60.0;
  // P.aimT do próprio jogador (ThirdPersonCamera::aimT, a rampa de updateCamera): vem do input local,
  // sem esperar o snapshot. Os outros jogadores usam a flag de mira replicada com a mesma rampa.
  std::optional<double> localAim;
};

class CharacterViews {
 public:
  CharacterViews(const ScenePack& pack, const CharacterLibrary& lib);
  ~CharacterViews();

  // Anima as vistas pelo mundo replicado e acrescenta as malhas do quadro a `out`
  // (skinned, bones e models). Vistas de entidades que sumiram do snapshot são descartadas.
  void update(const ClientWorld& world, const CharacterContext& ctx, DynamicSceneState& out);

  std::size_t size() const { return views_.size(); }
  // Matrizes de mundo dos nós do último quadro (testes): vazio se a entidade não tem vista.
  const std::vector<glm::mat4>* nodeMatrices(std::uint32_t id) const;
  const anim::Pose* pose(std::uint32_t id) const;

 private:
  struct View;
  View* viewFor(const protocol::EntityState& e, const CharacterContext& ctx);
  void emit(View& v, const glm::mat4& root, const glm::vec3& center, float radius, DynamicSceneState& out);

  const ScenePack& pack_;
  const CharacterLibrary& lib_;
  std::unordered_map<std::uint32_t, std::unique_ptr<View>> views_;
  std::uint32_t frame_ = 0;
};

}  // namespace rpg::client
