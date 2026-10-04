#pragma once
// Resumo dos dados de design (cpp/data): SHA-256 de todos os arquivos, em ordem de caminho relativo,
// cada um como "caminho\0conteúdo\0" (sem os \r de um checkout com CRLF). Cliente e servidor com dados diferentes não se entendem (os
// índices do protocolo vêm das tabelas): o Hello leva o resumo e o servidor recusa o que não bate.
#include <filesystem>
#include <string>

namespace rpg {

std::string dataHash(const std::filesystem::path& dir);

}  // namespace rpg
