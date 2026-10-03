#pragma once
// Caminhos vindos do build (macros com o diretório de fontes) e da linha de comando chegam como texto
// UTF-8. No Linux o std::filesystem::path usa os bytes como estão; no Windows o construtor a partir de
// `std::string` interpretaria a página de código ANSI e estragaria "Área de trabalho". Construir a partir
// de `std::u8string` deixa a conversão explícita e igual nos dois sistemas.
#include <filesystem>
#include <string>
#include <string_view>

namespace rpg {

inline std::filesystem::path pathFromUtf8(std::string_view utf8) {
  return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

// Inverso, para logs e mensagens (que são UTF-8 em todo o projeto).
inline std::string pathToUtf8(const std::filesystem::path& p) {
  const std::u8string s = p.u8string();
  return std::string(s.begin(), s.end());
}

// Diretório do usuário para saves e configurações (não é criado aqui):
//   Linux    $XDG_DATA_HOME/projeto-game, ou ~/.local/share/projeto-game
//   Windows  %APPDATA%\projeto-game
//   macOS    ~/Library/Application Support/projeto-game
// Sem nenhuma dessas variáveis, `projeto-game` no diretório atual.
std::filesystem::path userDataDir();

}  // namespace rpg
