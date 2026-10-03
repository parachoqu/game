#include "server/persist/SaveStore.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <system_error>

#include "core/Log.h"
#include "core/Paths.h"

namespace rpg::server::persist {

namespace fs = std::filesystem;

std::optional<Json> readJsonFile(const fs::path& path) {
  std::error_code ec;
  if (!fs::is_regular_file(path, ec)) return std::nullopt;
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    log::warn("não foi possível abrir {}", pathToUtf8(path));
    return std::nullopt;
  }
  std::stringstream ss;
  ss << in.rdbuf();
  Json j = Json::parse(ss.str(), nullptr, false);
  if (j.is_discarded()) {
    log::warn("{} não é um JSON válido; ignorado", pathToUtf8(path));
    return std::nullopt;
  }
  return j;
}

bool writeJsonFile(const fs::path& path, const Json& j) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  fs::path tmp = path;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      log::warn("não foi possível gravar {}", pathToUtf8(tmp));
      return false;
    }
    out << j.dump(1, '\t');
    out << '\n';
    if (!out) {
      log::warn("falha ao gravar {}", pathToUtf8(tmp));
      return false;
    }
  }
  fs::rename(tmp, path, ec);
  if (ec) {
    log::warn("falha ao trocar {} por {}: {}", pathToUtf8(tmp), pathToUtf8(path), ec.message());
    fs::remove(tmp, ec);
    return false;
  }
  return true;
}

SaveStore::SaveStore(fs::path dir) : dir_(std::move(dir)) {}

std::string SaveStore::fileNameFor(std::string_view name) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  for (const char ch : name) {
    const auto c = static_cast<unsigned char>(ch);
    if (c >= 'A' && c <= 'Z') out += static_cast<char>(c - 'A' + 'a');
    else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_') out += static_cast<char>(c);
    else {
      out += '%';
      out += kHex[c >> 4];
      out += kHex[c & 15];
    }
  }
  if (out.empty()) out = "_";
  return out + ".json";
}

fs::path SaveStore::characterPath(std::string_view name) const {
  return dir_ / "characters" / pathFromUtf8(fileNameFor(name));
}

std::optional<Json> SaveStore::loadWorld() const { return readJsonFile(dir_ / "world.json"); }

bool SaveStore::saveWorld(const Json& rec) const { return writeJsonFile(dir_ / "world.json", rec); }

std::optional<Json> SaveStore::loadCharacter(std::string_view name) const { return readJsonFile(characterPath(name)); }

bool SaveStore::saveCharacter(std::string_view name, const Json& rec) const {
  return writeJsonFile(characterPath(name), rec);
}

bool SaveStore::hasCharacter(std::string_view name) const {
  std::error_code ec;
  return fs::is_regular_file(characterPath(name), ec);
}

bool SaveStore::removeCharacter(std::string_view name) const {
  std::error_code ec;
  return fs::remove(characterPath(name), ec);
}

std::vector<std::string> SaveStore::characters() const {
  std::vector<fs::path> files;
  std::error_code ec;
  for (const auto& e : fs::directory_iterator(dir_ / "characters", ec))
    if (e.is_regular_file(ec) && e.path().extension() == ".json") files.push_back(e.path());
  std::sort(files.begin(), files.end());
  std::vector<std::string> out;
  for (const fs::path& f : files) {
    const auto j = readJsonFile(f);
    if (!j || !j->is_object()) continue;
    const auto p = j->find("profile");
    if (p == j->end() || !p->is_object()) continue;
    const auto n = p->find("name");
    if (n != p->end() && n->is_string()) out.push_back(n->get<std::string>());
  }
  return out;
}

}  // namespace rpg::server::persist
