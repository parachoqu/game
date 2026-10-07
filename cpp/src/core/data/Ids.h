#pragma once
// Ids das definições de design. No JS as regras usavam strings ('espada', 'garraLider'); aqui cada
// chave vira um índice pequeno e tipado na hora do carregamento. As chaves continuam valendo nos
// arquivos de dados, nos saves e no protocolo (que envia o índice, conferido pela versão dos dados).
#include <compare>
#include <cstdint>
#include <functional>

namespace rpg {

template <class Tag>
struct DefId {
  static constexpr std::uint16_t kInvalid = 0xFFFF;
  std::uint16_t value = kInvalid;

  constexpr bool valid() const { return value != kInvalid; }
  constexpr explicit operator bool() const { return valid(); }
  constexpr auto operator<=>(const DefId&) const = default;
};

struct ItemTag;
struct WeaponFamilyTag;
struct SkillTag;
struct TrainingTag;
struct RecipeTag;
struct EnemyTypeTag;
struct ZoneTag;
struct MarketTag;
struct OriginTag;
struct StartTag;

using ItemId = DefId<ItemTag>;
using WeaponFamilyId = DefId<WeaponFamilyTag>;
using SkillId = DefId<SkillTag>;
using TrainingId = DefId<TrainingTag>;
using RecipeId = DefId<RecipeTag>;
using EnemyTypeId = DefId<EnemyTypeTag>;
using ZoneId = DefId<ZoneTag>;
using MarketId = DefId<MarketTag>;
using OriginId = DefId<OriginTag>;
using StartId = DefId<StartTag>;

}  // namespace rpg

template <class Tag>
struct std::hash<rpg::DefId<Tag>> {
  std::size_t operator()(const rpg::DefId<Tag>& id) const noexcept { return id.value; }
};
