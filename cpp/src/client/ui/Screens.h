#pragma once
// Telas fora do jogo (index.html + main.js): carregamento com fases, título e criação de personagem.
// Desenho imediato sobre o mundo 3D (o título mostra o Vale à noite, como na demo).
#include <cstdint>
#include <string>
#include <vector>

#include "client/Input.h"
#include "client/ui/UiBatch.h"
#include "core/data/GameData.h"

namespace rpg::client {

class Localization;
struct Presentation;

struct CreationForm {
  std::string name;
  std::size_t model = 0, origin = 0, start = 2;  // main.js: o terceiro conhecimento vem marcado
};

// Nomes sugeridos na criação (main.js NAMES) e modelos de personagem.
extern const std::vector<std::string> kSuggestedNames;
extern const std::vector<std::string> kCharacterModels;

class Screens {
 public:
  enum class Result : std::uint8_t { None, NewGame, Continue, Back, Start };

  void loading(UiBatch& ui, const UiFonts& f, const InputState& in, const Localization& L, double progress, std::string_view text);
  Result title(UiBatch& ui, const UiFonts& f, const InputState& in, const Localization& L, bool canContinue, double fade);
  Result create(UiBatch& ui, const UiFonts& f, const InputState& in, const Localization& L, const GameData& data,
                const Presentation& look, CreationForm& form);
  // Mensagem central (entrando, recusado, conexão perdida).
  void notice(UiBatch& ui, const UiFonts& f, const InputState& in, std::string_view text);
};

}  // namespace rpg::client
