#include "core/Paths.h"

#include <cstdlib>

namespace rpg {

namespace {

constexpr const char* kAppDir = "projeto-game";

#ifdef _WIN32
std::filesystem::path envPath(const wchar_t* name) {
  wchar_t* value = nullptr;
  std::size_t len = 0;
  if (_wdupenv_s(&value, &len, name) != 0 || !value) return {};
  std::filesystem::path p(value);
  std::free(value);
  return p;
}
#else
std::filesystem::path envPath(const char* name) {
  const char* value = std::getenv(name);
  return value && *value ? pathFromUtf8(value) : std::filesystem::path{};
}
#endif

}  // namespace

std::filesystem::path userDataDir() {
#ifdef _WIN32
  if (auto appData = envPath(L"APPDATA"); !appData.empty()) return appData / kAppDir;
#elif defined(__APPLE__)
  if (auto home = envPath("HOME"); !home.empty()) return home / "Library" / "Application Support" / kAppDir;
#else
  if (auto xdg = envPath("XDG_DATA_HOME"); !xdg.empty()) return xdg / kAppDir;
  if (auto home = envPath("HOME"); !home.empty()) return home / ".local" / "share" / kAppDir;
#endif
  return std::filesystem::path(kAppDir);
}

}  // namespace rpg
