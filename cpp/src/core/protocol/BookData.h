#pragma once
// Dados do Livro do personagem, compartilhados pela simulação (que escreve), pelo save e pelo cliente
// (que escreve o texto a partir do modelo e dos argumentos). Ver sim/Book.h.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/protocol/ByteStream.h"
#include "core/protocol/Messages.h"

namespace rpg::protocol {

enum class BookCat : std::uint8_t { Historia, Feitos, Descobertas, Legado };
enum class BookDomain : std::uint8_t { None, Trabalho, Comercio, Exploracao, Combate, Risco, Ceu, Comunidade };

std::string_view bookCatKey(BookCat c);
std::string_view bookDomainKey(BookDomain d);
std::optional<BookCat> bookCatFromKey(std::string_view k);
std::optional<BookDomain> bookDomainFromKey(std::string_view k);

using TallyData = std::vector<std::pair<std::string, double>>;

struct BookEntry {
  int id = 0;
  double time = 0.0;
  int day = 1;
  std::string key;
  BookCat cat = BookCat::Historia;
  BookDomain domain = BookDomain::None;
  std::string tmpl;  // modelo de texto (cliente)
  protocol::MsgArgs args;
  int count = 1;
  TallyData data;  // `tally`: dados agregados (ex.: ferramentas por item)
  bool isPublic = false;
  bool personal = false;  // anotação do jogador
  std::optional<double> updatedTime;
  // save importado da demo: título e texto já escritos
  bool literal = false;
  std::string literalTitle, literalText;

  template <class A>
  void io(A& a) {
    a(id); a(time); a(day); a(key); a(cat); a(domain); a(tmpl);
    a(args); a(count); a(data); a(isPublic); a(personal); a(updatedTime);
    a(literal); a(literalTitle); a(literalText);
  }
};

struct BookTitle {
  std::string id;
  std::string literalName, literalWhy;  // só em saves importados
  template <class A>
  void io(A& a) { a(id); a(literalName); a(literalWhy); }
};

}  // namespace rpg::protocol
