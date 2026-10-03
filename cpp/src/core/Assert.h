#pragma once
// Verificação de invariantes que vale também em Release: uma simulação autoritativa não pode seguir
// com estado corrompido. Falha registra arquivo, linha e mensagem e aborta.
#include <source_location>

namespace rpg::detail {
[[noreturn]] void assertFailed(const char* expr, const char* msg, std::source_location where);
}  // namespace rpg::detail

#define RPG_ASSERT(expr, msg)                                                         \
  do {                                                                                \
    if (!(expr)) ::rpg::detail::assertFailed(#expr, msg, std::source_location::current()); \
  } while (false)
