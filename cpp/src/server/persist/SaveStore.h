#pragma once
// Registros salvos em arquivos JSON (o localStorage da demo):
//
//   <dir>/world.json
//   <dir>/characters/<nome>.json
//
// O nome do arquivo vem do nome do personagem, em minúsculas e com tudo o que não é [a-z0-9_-]
// escrito como %XX: "Ana" e "ana" são o mesmo personagem em qualquer sistema de arquivos. A escrita vai
// para um temporário que troca de nome no fim, então uma queda no meio nunca deixa um save pela metade.
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "server/persist/SaveRecords.h"

namespace rpg::server::persist {

class SaveStore {
 public:
  explicit SaveStore(std::filesystem::path dir);

  const std::filesystem::path& dir() const { return dir_; }

  std::optional<Json> loadWorld() const;
  bool saveWorld(const Json& rec) const;

  std::optional<Json> loadCharacter(std::string_view name) const;
  bool saveCharacter(std::string_view name, const Json& rec) const;
  bool hasCharacter(std::string_view name) const;
  bool removeCharacter(std::string_view name) const;
  // Nomes dos personagens salvos (lidos do perfil de cada arquivo), em ordem alfabética do arquivo.
  std::vector<std::string> characters() const;

  static std::string fileNameFor(std::string_view name);

 private:
  std::filesystem::path characterPath(std::string_view name) const;

  std::filesystem::path dir_;
};

// Lê um JSON de um arquivo; nada se não existe ou não é JSON válido (o erro vai para o log).
std::optional<Json> readJsonFile(const std::filesystem::path& path);
// Grava com troca de nome atômica; false (e log) se falhar.
bool writeJsonFile(const std::filesystem::path& path, const Json& j);

}  // namespace rpg::server::persist
