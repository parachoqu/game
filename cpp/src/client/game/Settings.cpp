#include "client/game/Settings.h"

#include <fstream>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "core/Paths.h"

namespace rpg::client {

namespace {

using Json = nlohmann::json;

std::string_view sensKey(CamSensitivity s) {
  return s == CamSensitivity::Baixa ? "baixa" : s == CamSensitivity::Alta ? "alta" : "media";
}

}  // namespace

Settings Settings::load(const std::filesystem::path& file) {
  Settings out;
  std::error_code ec;
  if (file.empty() || !std::filesystem::is_regular_file(file, ec)) return out;
  std::ifstream in(file, std::ios::binary);
  std::stringstream ss;
  ss << in.rdbuf();
  const Json j = Json::parse(ss.str(), nullptr, false);
  if (j.is_discarded() || !j.is_object()) {
    log::warn("configurações em {} ilegíveis; usando os padrões", pathToUtf8(file));
    return out;
  }
  const std::string sens = j.value("cameraSens", std::string("media"));
  out.camera.sens = sens == "baixa" ? CamSensitivity::Baixa : sens == "alta" ? CamSensitivity::Alta : CamSensitivity::Media;
  out.camera.invert = j.value("cameraInvert", false);
  const std::string q = j.value("quality", std::string());
  if (q == "alta" || q == "media" || q == "baixa") {
    out.quality = q;
    out.qualityChosen = j.value("qualityChosen", true);
  }
  out.muted = j.value("muted", false);
  if (const auto it = j.find("last"); it != j.end() && it->is_object() && it->value("name", std::string()).size()) {
    out.last = Profile{it->value("name", std::string()), it->value("origin", std::string()), it->value("start", std::string()),
                       it->value("model", std::string())};
  }
  return out;
}

bool Settings::save(const std::filesystem::path& file) const {
  if (file.empty()) return false;
  Json j = Json::object();
  j["cameraSens"] = sensKey(camera.sens);
  j["cameraInvert"] = camera.invert;
  j["quality"] = quality;
  j["qualityChosen"] = qualityChosen;
  j["muted"] = muted;
  if (last) j["last"] = {{"name", last->name}, {"origin", last->origin}, {"start", last->start}, {"model", last->model}};
  else j["last"] = nullptr;
  std::error_code ec;
  std::filesystem::create_directories(file.parent_path(), ec);
  std::filesystem::path tmp = file;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      log::warn("não foi possível gravar {}", pathToUtf8(tmp));
      return false;
    }
    out << j.dump(2) << '\n';
  }
  std::filesystem::rename(tmp, file, ec);
  if (ec) {
    log::warn("não foi possível gravar {}: {}", pathToUtf8(file), ec.message());
    return false;
  }
  return true;
}

}  // namespace rpg::client
