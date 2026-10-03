#pragma once
// A interface do jogo no documento assets/ui/game.rml (index.html da demo): telas de carregamento,
// título e criação (main.js), e o resto que o jogo mostra por cima do mundo. Cada parte mexe no DOM
// como o módulo da demo correspondente (ids e classes iguais) e devolve ao app o que o jogador pediu.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "client/ui/UiSystem.h"
#include "client/ui/WorldMap.h"
#include "core/data/GameData.h"
#include "core/protocol/Requests.h"
#include "core/protocol/Snapshot.h"

namespace Rml {
class Element;
class ElementDocument;
}  // namespace Rml

namespace rpg::client {

class ClientWorld;
class FxState;
class Localization;
struct Presentation;

// O que o HUD lê a cada quadro (hud.js `updateHud` + o prompt de interact.js).
struct HudInput {
  const ClientWorld* world = nullptr;
  const FxState* fx = nullptr;
  const StaticWorld* statics = nullptr;
  double dt = 0, now = 0;
  double camYaw = 0, camPitch = 0.67;
  bool aiming = false, aimOnTarget = false;
  double aimDist = 0;
  double mouseX = 0, mouseY = 0;
  std::string prompt;  // RML (pode ter <kbd>); vazio esconde
  bool uiOpen = false;
};

// Painéis modais (panels.js) e o Livro (bookui.js).
enum class Panel : std::uint8_t {
  None, Market, Forge, Trainer, Storage, Stable, Board, Canteiro, Astronomer, Traveler, Loot, Portal, Death, Event, Inventory, Intents, Map, Pause,
};
struct PanelInput {
  const ClientWorld* world = nullptr;
  const FxState* fx = nullptr;
  const StaticWorld* statics = nullptr;
  double dt = 0, camYaw = 0;
  // configurações que a pausa mostra
  std::string camSens = "media", quality = "alta";
  bool camInvert = false, muted = false;
};
struct PanelOutput {
  std::vector<protocol::Request> requests;
  std::vector<std::string> local;  // ações que só o app resolve ("camsens:alta", "quality:baixa", "mute", "restart"…)
  std::vector<Panel> closed;       // painéis fechados desde o último quadro
};

struct CreationForm {
  std::string name;
  std::size_t model = 0, origin = 0, start = 2;  // main.js: o terceiro conhecimento vem marcado
};

// Nomes sugeridos na criação (main.js NAMES) e modelos de personagem (CHAR_MODELS).
extern const std::vector<std::string> kSuggestedNames;
extern const std::vector<std::string> kCharacterModels;

class GameUi {
 public:
  GameUi(UiSystem& ui, const Localization& L, const GameData& data, const Presentation& look);

  Rml::ElementDocument* document() { return doc_; }
  Rml::Element* byId(const char* id);

  // ---- telas (main.js `boot`, `buildCreate`)
  struct BootPhase {
    std::string label;
    double weight = 0;
  };
  void setBootPhases(std::vector<BootPhase> phases);
  // Fase atual (índice), sub-etapa (texto, vazio = só a fase) e quantas sub-etapas já vieram.
  void bootProgress(int phase, std::string_view sub, int subs);
  void bootDone();
  void bootLore(double time);  // as regras das zonas passando a cada 4,5 s

  void showTitle(bool canContinue);
  void showCreate(CreationForm& form);
  void hideScreens();

  enum class ScreenAction : std::uint8_t { None, NewGame, Continue, Back, Start };
  // Clique ou tecla do quadro (Enter começa, Esc volta). Lê o nome do campo de volta em `form`.
  ScreenAction screenInput(const InputState& in, CreationForm& form);

  // Aviso central (entrando, conexão perdida): vazio esconde.
  void notice(std::string_view text);

  // ---- HUD (hud.js)
  enum class HudAction : std::uint8_t { Potion, Mount, CamNorth, MiniSize, CaptionClose };
  void showHud(bool on);
  // Atualiza o HUD; devolve os botões clicados desde o último quadro.
  std::vector<HudAction> hud(const HudInput& in);
  void toggleMinimap();
  // Legenda da observação do céu (#caption): título e texto; vazio esconde.
  void caption(const std::pair<std::string, std::string>& text);
  bool minimapBig() const { return miniBig_; }
  // O mapa da região (fundo do minimapa e do mapa completo); construído no carregamento.
  WorldMap& worldMap() { return map_; }

  // ---- painéis, menu (Tab) e Livro
  void openPanel(Panel p, std::uint32_t data = 0, bool intro = false);
  void closePanel();
  Panel panel() const { return panel_; }
  std::string_view menuTab() const;  // aba do menu aberta (inventory, book, map, intents) ou vazio
  void openMenu(std::string_view tab = {});
  void toggleMenu(std::string_view tab = {});
  void openBook();
  void closeBook();
  bool bookOpen() const { return bookOpen_; }
  // Redesenha o que estiver aberto e devolve o que os botões pediram desde o último quadro.
  PanelOutput panels(const PanelInput& in);

 private:
  void onAction(const std::string& act, const std::string& arg);
  void renderCards(CreationForm& form);

  void panelAction(const std::string& act, const std::string& arg);
  void bookAction(const std::string& act, const std::string& arg);
  void syncMenubar();
  void syncBook(double dt);
  std::string renderPanel();
  std::string renderBook();
  std::string glyph(ItemId id) const;
  std::string row(ItemId id, std::string_view name, std::string_view sub, std::string_view price, std::string_view acts, bool dim = false,
                  bool down = false) const;
  std::string entryName(const protocol::ItemEntry& e) const;
  std::string loadLine(const protocol::PrivateState& P) const;
  void syncToasts(const FxState& fx);
  void drawMinimap(const HudInput& in);

  UiSystem& ui_;
  const Localization& L_;
  const GameData& data_;
  const Presentation& look_;
  Rml::ElementDocument* doc_ = nullptr;
  std::vector<BootPhase> phases_;
  ScreenAction pending_ = ScreenAction::None;
  CreationForm* form_ = nullptr;
  bool cardsDirty_ = false;
  int lore_ = -1;
  // HUD
  WorldMap map_;
  Raster mini_;
  bool miniBig_ = false, hudOn_ = false;
  double miniT_ = 0, textT_ = 0;
  std::vector<HudAction> hudActs_;
  std::vector<std::pair<std::uint32_t, Rml::Element*>> toastEls_;
  // painéis
  Panel panel_ = Panel::None;
  std::uint32_t panelData_ = 0;
  bool panelIntro_ = false;
  struct {
    bool useStorage = true;
    std::string tab = "eq";
  } panelLocal_;
  double panelT_ = 0;
  PanelInput panelInput_;
  PanelOutput panelOut_;
  std::vector<Panel> closedPanels_;
  std::string lastTab_ = "inventory";
  Raster full_;
  // Livro
  bool bookOpen_ = false, bookPublic_ = false, bookWriting_ = false, bookDirty_ = false;
  std::string bookTab_ = "historia", lastBookTab_;
  double bookT_ = 0;
};

}  // namespace rpg::client
