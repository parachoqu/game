#pragma once
// Ponte entre os bytes do protocolo (uint8_t) e os do transporte (std::byte).
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace rpg::net {

inline std::span<const std::byte> asBytes(const std::vector<std::uint8_t>& v) {
  return std::as_bytes(std::span<const std::uint8_t>(v));
}

inline std::span<const std::uint8_t> asU8(const std::vector<std::byte>& v) {
  return {reinterpret_cast<const std::uint8_t*>(v.data()), v.size()};
}

}  // namespace rpg::net
