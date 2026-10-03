#pragma once
// Tabela de definições indexada por DefId, na ordem em que as chaves aparecem no arquivo (a mesma
// ordem de declaração do config.js). Consulta por chave para carregar dados e saves; por id no jogo.
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/Assert.h"
#include "core/data/DataError.h"
#include "core/data/Ids.h"

namespace rpg {

template <class Def, class IdT>
class DefTable {
 public:
  IdT add(std::string key, Def def) {
    if (byKey_.contains(key)) throw DataError("chave repetida: " + key);
    if (defs_.size() >= IdT::kInvalid) throw DataError("tabela cheia ao adicionar " + key);
    const IdT id{static_cast<decltype(IdT::value)>(defs_.size())};
    byKey_.emplace(key, id);
    keys_.push_back(std::move(key));
    defs_.push_back(std::move(def));
    return id;
  }

  // Id da chave, ou um id inválido se ela não existir.
  IdT find(std::string_view key) const {
    const auto it = byKey_.find(std::string(key));
    return it == byKey_.end() ? IdT{} : it->second;
  }

  // Id da chave; se não existir, DataError citando quem a referenciou.
  IdT require(std::string_view key, std::string_view referencedBy) const {
    const IdT id = find(key);
    if (!id) throw DataError(std::string(referencedBy) + ": referência desconhecida '" + std::string(key) + "'");
    return id;
  }

  const Def& operator[](IdT id) const {
    RPG_ASSERT(contains(id), "DefTable: id fora da tabela");
    return defs_[id.value];
  }
  Def& mut(IdT id) {
    RPG_ASSERT(contains(id), "DefTable: id fora da tabela");
    return defs_[id.value];
  }

  const std::string& key(IdT id) const {
    RPG_ASSERT(contains(id), "DefTable: id fora da tabela");
    return keys_[id.value];
  }

  bool contains(IdT id) const { return id.valid() && id.value < defs_.size(); }
  std::size_t size() const { return defs_.size(); }

  // Ids em ordem de declaração: for (auto id : table.ids()) …
  std::vector<IdT> ids() const {
    std::vector<IdT> out;
    out.reserve(defs_.size());
    for (std::size_t i = 0; i < defs_.size(); ++i) out.push_back(IdT{static_cast<decltype(IdT::value)>(i)});
    return out;
  }

 private:
  std::vector<Def> defs_;
  std::vector<std::string> keys_;
  std::unordered_map<std::string, IdT> byKey_;
};

}  // namespace rpg
