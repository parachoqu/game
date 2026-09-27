#pragma once
// Erro de carregamento ou de consistência dos dados de design. A mensagem sempre diz o arquivo e a
// chave, para o problema ser corrigido no JSON (ou no config.js da demo, que é a origem).
#include <stdexcept>
#include <string>

namespace rpg {

class DataError : public std::runtime_error {
 public:
  explicit DataError(const std::string& what) : std::runtime_error(what) {}
};

}  // namespace rpg
