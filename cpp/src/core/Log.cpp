#include "core/Log.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "core/Assert.h"

namespace rpg::log {
namespace {

std::atomic<Level> gLevel{Level::Info};
std::mutex gMutex;
const auto gStart = std::chrono::steady_clock::now();

const char* tag(Level l) {
  switch (l) {
    case Level::Debug: return "debug";
    case Level::Info: return "info ";
    case Level::Warn: return "aviso";
    case Level::Error: return "ERRO ";
  }
  return "?";
}

}  // namespace

void setLevel(Level l) { gLevel.store(l, std::memory_order_relaxed); }
Level level() { return gLevel.load(std::memory_order_relaxed); }

void write(Level l, std::string_view text) {
  const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - gStart).count();
  const std::lock_guard lock(gMutex);
  std::fprintf(stderr, "[%9.3f %s] %.*s\n", t, tag(l), static_cast<int>(text.size()), text.data());
}

}  // namespace rpg::log

namespace rpg::detail {

void assertFailed(const char* expr, const char* msg, std::source_location where) {
  rpg::log::error("falha de invariante: {} ({}) em {}:{}", msg, expr, where.file_name(), where.line());
  std::abort();
}

}  // namespace rpg::detail
