#pragma once
// Registro mínimo em stderr, com nível e horário relativo ao início do processo. Thread-safe: o
// servidor local roda numa thread própria e escreve junto com o cliente.
#include <format>
#include <string_view>

namespace rpg::log {

enum class Level { Debug, Info, Warn, Error };

void setLevel(Level level);
Level level();
void write(Level level, std::string_view text);

template <class... Args>
void debug(std::format_string<Args...> fmt, Args&&... args) {
  if (level() <= Level::Debug) write(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void info(std::format_string<Args...> fmt, Args&&... args) {
  if (level() <= Level::Info) write(Level::Info, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void warn(std::format_string<Args...> fmt, Args&&... args) {
  if (level() <= Level::Warn) write(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
}
template <class... Args>
void error(std::format_string<Args...> fmt, Args&&... args) {
  write(Level::Error, std::format(fmt, std::forward<Args>(args)...));
}

}  // namespace rpg::log
