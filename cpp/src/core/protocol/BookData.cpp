#include "core/protocol/BookData.h"

#include <array>

namespace rpg::protocol {
namespace {

constexpr std::array<std::string_view, 4> kCats = {"historia", "feitos", "descobertas", "legado"};
constexpr std::array<std::string_view, 8> kDomains = {"", "trabalho", "comercio", "exploracao",
                                                      "combate", "risco", "ceu", "comunidade"};

}  // namespace

std::string_view bookCatKey(BookCat c) { return kCats[static_cast<std::size_t>(c)]; }
std::string_view bookDomainKey(BookDomain d) { return kDomains[static_cast<std::size_t>(d)]; }

std::optional<BookCat> bookCatFromKey(std::string_view k) {
  for (std::size_t i = 0; i < kCats.size(); ++i)
    if (kCats[i] == k) return static_cast<BookCat>(i);
  return std::nullopt;
}

std::optional<BookDomain> bookDomainFromKey(std::string_view k) {
  for (std::size_t i = 0; i < kDomains.size(); ++i)
    if (kDomains[i] == k) return static_cast<BookDomain>(i);
  return std::nullopt;
}

}  // namespace rpg::protocol
