#pragma once
// Handle geracional: índice do slot + geração. Um handle guardado depois que a entidade foi removida
// deixa de resolver (a geração do slot mudou), em vez de apontar para quem ocupou o lugar. É o que
// substitui as referências diretas a objetos do JS (`e.targetBag`, `P.aimEnemy`, `bag.claimed`).
#include <compare>
#include <cstdint>
#include <functional>

namespace rpg {

template <class Tag>
struct Handle {
  std::uint32_t index = 0;
  std::uint32_t generation = 0;  // 0 = handle nulo; slots válidos começam na geração 1

  constexpr bool valid() const { return generation != 0; }
  constexpr explicit operator bool() const { return valid(); }
  constexpr auto operator<=>(const Handle&) const = default;

  // Forma compacta para o protocolo (32 bits de índice, 32 de geração).
  constexpr std::uint64_t packed() const { return (std::uint64_t{generation} << 32) | index; }
  static constexpr Handle unpack(std::uint64_t v) {
    return {static_cast<std::uint32_t>(v & 0xFFFFFFFFu), static_cast<std::uint32_t>(v >> 32)};
  }
};

}  // namespace rpg

template <class Tag>
struct std::hash<rpg::Handle<Tag>> {
  std::size_t operator()(const rpg::Handle<Tag>& h) const noexcept { return std::hash<std::uint64_t>{}(h.packed()); }
};
