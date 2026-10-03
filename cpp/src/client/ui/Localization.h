#pragma once
// Textos do cliente (pt-BR): mensagens da simulação (MessageId + argumentos), nomes de itens,
// lugares, zonas, inimigos, treinos, técnicas e rótulos da interface. Tudo vem de data/text/pt-BR.
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/data/GameData.h"
#include "core/protocol/BookData.h"
#include "core/protocol/Interact.h"
#include "core/protocol/Messages.h"
#include "core/world/ZoneMap.h"

namespace rpg::client {

// Viajante inspecionável (travelers.json): equipamento e a biografia pública, separados.
struct TravelerText {
  std::string name, role;
  std::vector<std::string> gear;
  std::vector<std::pair<std::string, std::string>> publicEntries;
};

class Localization {
 public:
  // `textDir`: data/text/pt-BR. Falta de chave de mensagem é erro (std::runtime_error com a chave).
  static Localization load(const std::filesystem::path& textDir, const GameData& data);

  std::string message(protocol::MessageId id, const protocol::MsgArgs& args = {}) const;
  // Aplica {0}, {0|lower}, {0|cap}, {0|s} de um modelo.
  std::string format(std::string_view tmpl, const protocol::MsgArgs& args) const;
  std::string arg(const protocol::MsgArg& a) const;

  std::string item(ItemId id) const;
  std::string itemDesc(ItemId id) const;
  std::string place(PlaceId id) const;
  std::string zone(ZoneKind z) const;
  std::string zoneRule(ZoneKind z) const;
  std::string zoneDeath(ZoneKind z) const;
  std::string enemy(EnemyTypeId id) const;
  std::string training(TrainingId id) const;
  std::string trainingDesc(TrainingId id) const;
  std::string skill(SkillId id) const;
  std::string skillDesc(SkillId id) const;
  std::string weapon(WeaponFamilyId id) const;
  std::string origin(OriginId id) const;
  std::string originHint(OriginId id) const;
  std::string start(StartId id) const;
  std::string startDesc(StartId id) const;
  std::string market(MarketId id) const;
  std::string recipeNote(ItemId out) const;  // recipes.json: nota da receita pelo item fabricado
  // Rótulo de um interagível do layout (interactables.json: "market.vale", "forge"…).
  std::string interactable(std::string_view key) const;
  std::string travelerName(std::string_view key) const;
  const TravelerText* traveler(std::string_view key) const;
  // Livro: título e texto de uma entrada (book.json: modelo → {title, text}); saves importados da
  // demo trazem o texto pronto.
  std::string bookTitle(const protocol::BookEntry& e) const;
  std::string bookText(const protocol::BookEntry& e) const;
  std::string bookTitleName(const protocol::BookTitle& t) const;
  std::string bookTitleWhy(const protocol::BookTitle& t) const;
  // Nome do capítulo do dia pelo domínio que mais aparece (book.js CHAPTER_NAMES; "none" = Chegada).
  std::string bookChapter(std::string_view domain) const;
  // Um modelo de book.json aplicado a argumentos (partes que BookText junta).
  std::string bookPart(std::string_view key, const protocol::MsgArgs& args, bool title = false) const;

  // Rótulos soltos da interface (ui.json); devolve a própria chave se não houver texto.
  std::string ui(std::string_view key) const;

  // Número como o JS o escreveria em `${n}` (inteiro sem ".0", decimal mais curto).
  static std::string number(double v);

 private:
  const GameData* data_ = nullptr;
  std::map<std::string, std::string, std::less<>> messages_, ui_, interactables_;
  std::map<std::string, std::pair<std::string, std::string>, std::less<>> book_, titles_;
  std::map<std::string, std::string, std::less<>> chapters_;
  std::map<std::string, TravelerText, std::less<>> travelers_;
  // tabela → chave → campo → texto
  std::map<std::string, std::map<std::string, std::map<std::string, std::string>>, std::less<>> tables_;
  std::string field(std::string_view table, std::string_view key, std::string_view f) const;
};

// Nome escrito de uma direção (turbulent.js `compass`): mensagens dir.*.
protocol::MessageId compassMessage(double dx, double dz);

}  // namespace rpg::client
