#include "sim/Book.h"

#include <algorithm>

namespace rpg::sim {
int Book::push(BookEntry e, double time, int day) {
  e.id = seq_++;
  e.time = time;
  e.day = day;
  entries_.push_back(std::move(e));
  touch();
  return static_cast<int>(entries_.size()) - 1;
}

bool Book::hasKey(std::string_view key) const { return find(key) != nullptr; }

const BookEntry* Book::find(std::string_view key) const {
  for (const BookEntry& e : entries_)
    if (e.key == key) return &e;
  return nullptr;
}

Book::Change Book::first(double time, int day, std::string key, BookCat cat, BookDomain domain, std::string tmpl,
                         protocol::MsgArgs args) {
  if (hasKey(key)) return std::nullopt;
  BookEntry e;
  e.key = std::move(key);
  e.cat = cat;
  e.domain = domain;
  e.tmpl = std::move(tmpl);
  e.args = std::move(args);
  return push(std::move(e), time, day);
}

Book::Change Book::tally(double time, int day, std::string key, BookCat cat, BookDomain domain, std::string tmpl,
                         int inc, std::optional<TallyData> data, protocol::MsgArgs args) {
  for (std::size_t i = 0; i < entries_.size(); ++i) {
    BookEntry& e = entries_[i];
    if (e.key != key) continue;
    e.count += inc;
    if (data) e.data = std::move(*data);  // `e.data = data ?? e.data`
    if (!args.empty()) e.args = std::move(args);
    e.updatedTime = time;
    touch();
    return static_cast<int>(i);
  }
  BookEntry e;
  e.key = std::move(key);
  e.cat = cat;
  e.domain = domain;
  e.tmpl = std::move(tmpl);
  e.args = std::move(args);
  e.count = inc;
  if (data) e.data = std::move(*data);
  return push(std::move(e), time, day);
}

Book::Change Book::log(double time, int day, BookCat cat, BookDomain domain, std::string tmpl, protocol::MsgArgs args,
                       std::string key) {
  BookEntry e;
  e.key = key.empty() ? "ev" + std::to_string(seq_) : std::move(key);
  e.cat = cat;
  e.domain = domain;
  e.tmpl = std::move(tmpl);
  e.args = std::move(args);
  return push(std::move(e), time, day);
}

Book::Change Book::note(double time, int day, std::string text) {
  BookEntry e;
  e.key = "nota" + std::to_string(seq_);
  e.cat = BookCat::Historia;
  e.domain = BookDomain::None;
  e.tmpl = "note";
  e.args = {protocol::MsgArg::str(std::move(text))};
  e.personal = true;
  return push(std::move(e), time, day);
}

bool Book::grantTitle(std::string id) {
  if (std::any_of(titles_.begin(), titles_.end(), [&](const BookTitle& t) { return t.id == id; })) return false;
  titles_.push_back({std::move(id), {}, {}});
  touch();
  return true;
}

void Book::clear() {
  entries_.clear();
  titles_.clear();
  shownTitle_.reset();
  seq_ = 1;
  touch();
}

}  // namespace rpg::sim
