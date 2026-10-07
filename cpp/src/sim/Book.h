#pragma once
// O Livro do personagem (game/book.js): curadoria de acontecimentos verificáveis.
//
// A demo gravava título e texto já escritos em português. Aqui cada entrada guarda o modelo
// (`tmpl`, uma chave que o cliente conhece) e os argumentos; o cliente escreve o texto. A contagem e os
// dados agregados (`tally`) continuam na simulação, porque são fatos do mundo. Entradas importadas de
// um save da demo trazem o texto pronto (`literal`).
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "core/protocol/BookData.h"

namespace rpg::sim {

using protocol::BookCat;
using protocol::BookDomain;
using protocol::BookEntry;
using protocol::BookTitle;
using protocol::TallyData;

class Book {
 public:
  // Resultado de uma escrita: a entrada criada/atualizada (para o evento ao dono), ou nada.
  using Change = std::optional<int>;  // índice em entries()

  // Registro único: só na primeira vez que a chave acontece.
  Change first(double time, int day, std::string key, BookCat cat, BookDomain domain, std::string tmpl,
               protocol::MsgArgs args = {});
  // Marco agregado: soma `inc` à entrada da chave (ou cria). `data` substitui os dados agregados quando vem.
  Change tally(double time, int day, std::string key, BookCat cat, BookDomain domain, std::string tmpl, int inc = 1,
               std::optional<TallyData> data = std::nullopt, protocol::MsgArgs args = {});
  // Acontecimento individual (derrotas, incursões).
  Change log(double time, int day, BookCat cat, BookDomain domain, std::string tmpl, protocol::MsgArgs args,
             std::string key = {});
  Change note(double time, int day, std::string text);
  // Devolve true se o título é novo.
  bool grantTitle(std::string id);

  const std::vector<BookEntry>& entries() const { return entries_; }
  std::vector<BookEntry>& entries() { return entries_; }
  const std::vector<BookTitle>& titles() const { return titles_; }
  std::vector<BookTitle>& titles() { return titles_; }
  const std::optional<std::string>& shownTitle() const { return shownTitle_; }
  void setShownTitle(std::optional<std::string> t) { shownTitle_ = std::move(t); }
  int seq() const { return seq_; }
  void setSeq(int s) { seq_ = s; }
  bool hasKey(std::string_view key) const;
  const BookEntry* find(std::string_view key) const;
  void clear();

  // Muda a cada escrita: o servidor reenvia o Livro quando ela muda.
  std::uint32_t version() const { return version_; }
  void touch() { ++version_; }

 private:
  int push(BookEntry e, double time, int day);

  std::vector<BookEntry> entries_;
  std::vector<BookTitle> titles_;
  std::optional<std::string> shownTitle_;
  int seq_ = 1;
  std::uint32_t version_ = 0;
};

}  // namespace rpg::sim
