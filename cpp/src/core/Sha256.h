#pragma once
// SHA-256 (FIPS 180-4), para conferir que cliente e servidor usam os mesmos dados de design.
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace rpg {

class Sha256 {
 public:
  Sha256();
  void update(std::span<const std::uint8_t> data);
  void update(std::string_view text);
  std::array<std::uint8_t, 32> digest();
  std::string hex();  // 64 dígitos em minúsculas

 private:
  void block(const std::uint8_t* p);
  std::array<std::uint32_t, 8> h_{};
  std::array<std::uint8_t, 64> buf_{};
  std::size_t used_ = 0;
  std::uint64_t bits_ = 0;
};

}  // namespace rpg
