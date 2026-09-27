#include "client/ui/Localization.h"

#include <charconv>
#include <cmath>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "core/Paths.h"

namespace rpg::client {

using protocol::MessageId;
using protocol::MsgArg;

namespace {

nlohmann::json readJson(const std::filesystem::path& p) {
  std::ifstream f(p);
  if (!f) throw std::runtime_error("não foi possível abrir " + pathToUtf8(p));
  return nlohmann::json::parse(f);
}

// Minúsculas/maiúscula inicial em UTF-8 só para o que aparece nos textos (ASCII e Latin-1 acentuado).
std::string lowerFirstAware(std::string s, bool all) {
  for (std::size_t i = 0; i < s.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
      if (c >= 'A' && c <= 'Z') s[i] = static_cast<char>(c + 32);
    } else if (c == 0xC3 && i + 1 < s.size()) {
      unsigned char d = static_cast<unsigned char>(s[i + 1]);
      if (d >= 0x80 && d <= 0x9E && d != 0x97) s[i + 1] = static_cast<char>(d + 0x20);
      ++i;
    }
    if (!all) break;
  }
  return s;
}

std::string capFirst(std::string s) {
  if (s.empty()) return s;
  unsigned char c = static_cast<unsigned char>(s[0]);
  if (c >= 'a' && c <= 'z') s[0] = static_cast<char>(c - 32);
  else if (c == 0xC3 && s.size() > 1) {
    unsigned char d = static_cast<unsigned char>(s[1]);
    if (d >= 0xA0 && d <= 0xBE && d != 0xB7) s[1] = static_cast<char>(d - 0x20);
  }
  return s;
}

}  // namespace

std::string Localization::number(double v) {
  if (!std::isfinite(v)) return std::isnan(v) ? "NaN" : (v > 0 ? "Infinity" : "-Infinity");
  if (v == std::floor(v) && std::abs(v) < 1e15) return std::to_string(static_cast<long long>(v));
  char buf[64];
  const auto r = std::to_chars(buf, buf + sizeof buf, v);
  return std::string(buf, r.ptr);
}

Localization Localization::load(const std::filesystem::path& dir, const GameData& data) {
  Localization L;
  L.data_ = &data;
  // (os objetos JSON ficam em variáveis: iterar `.items()` de um temporário deixaria o laço sem dono)
  const nlohmann::json messages = readJson(dir / "messages.json");
  for (const auto& [k, v] : messages.items()) L.messages_[k] = v.get<std::string>();
  for (std::size_t i = 0; i < static_cast<std::size_t>(MessageId::Count); ++i) {
    const std::string_view key = protocol::messageKey(static_cast<MessageId>(i));
    if (!L.messages_.contains(key)) throw std::runtime_error("messages.json sem a chave " + std::string(key));
  }
  if (std::filesystem::exists(dir / "ui.json")) {
    const nlohmann::json ui = readJson(dir / "ui.json");
    for (const auto& [k, v] : ui.items()) L.ui_[k] = v.get<std::string>();
  }
  const nlohmann::json inter = readJson(dir / "interactables.json");
  for (const auto& [k, v] : inter.items()) L.interactables_[k] = v.get<std::string>();
  if (std::filesystem::exists(dir / "book.json")) {
    const nlohmann::json b = readJson(dir / "book.json");
    const nlohmann::json entries = b.value("entries", nlohmann::json::object());
    const nlohmann::json titles = b.value("titles", nlohmann::json::object());
    for (const auto& [k, v] : entries.items()) L.book_[k] = {v.value("title", k), v.value("text", std::string{})};
    for (const auto& [k, v] : titles.items()) L.titles_[k] = {v.value("name", k), v.value("why", std::string{})};
  }
  for (const char* table : {"items", "places", "zones", "enemies", "trainings", "skills", "weapons", "origins", "starts",
                            "markets", "travelers", "discoveries", "recipes"}) {
    const auto file = dir / (std::string(table) + ".json");
    if (!std::filesystem::exists(file)) continue;
    auto& t = L.tables_[table];
    const nlohmann::json j = readJson(file);
    for (const auto& [k, v] : j.items()) {
      if (!v.is_object()) continue;
      for (const auto& [f, s] : v.items())
        if (s.is_string()) t[k][f] = s.get<std::string>();
    }
  }
  return L;
}

std::string Localization::field(std::string_view table, std::string_view key, std::string_view f) const {
  const auto t = tables_.find(table);
  if (t == tables_.end()) return std::string(key);
  const auto k = t->second.find(std::string(key));
  if (k == t->second.end()) return std::string(key);
  const auto v = k->second.find(std::string(f));
  return v == k->second.end() ? std::string(key) : v->second;
}

std::string Localization::item(ItemId id) const { return field("items", data_->items.key(id), "name"); }
std::string Localization::itemDesc(ItemId id) const { return field("items", data_->items.key(id), "desc"); }
std::string Localization::place(PlaceId id) const { return field("places", placeKey(id), "name"); }
std::string Localization::zone(ZoneKind z) const { return field("zones", zoneKey(z), "name"); }
std::string Localization::zoneRule(ZoneKind z) const { return field("zones", zoneKey(z), "rule"); }
std::string Localization::zoneDeath(ZoneKind z) const { return field("zones", zoneKey(z), "death"); }
std::string Localization::enemy(EnemyTypeId id) const { return field("enemies", data_->enemies.key(id), "name"); }
std::string Localization::training(TrainingId id) const { return field("trainings", data_->trainings.key(id), "name"); }
std::string Localization::skill(SkillId id) const { return field("skills", data_->skills.key(id), "name"); }
std::string Localization::skillDesc(SkillId id) const { return field("skills", data_->skills.key(id), "desc"); }
std::string Localization::weapon(WeaponFamilyId id) const { return field("weapons", data_->weapons.key(id), "name"); }
std::string Localization::origin(OriginId id) const { return field("origins", data_->origins.key(id), "name"); }
std::string Localization::originHint(OriginId id) const { return field("origins", data_->origins.key(id), "hint"); }
std::string Localization::start(StartId id) const { return field("starts", data_->starts.key(id), "label"); }
std::string Localization::startDesc(StartId id) const { return field("starts", data_->starts.key(id), "desc"); }
std::string Localization::market(MarketId id) const { return field("markets", data_->markets.key(id), "name"); }
std::string Localization::travelerName(std::string_view key) const { return field("travelers", key, "name"); }

std::string Localization::interactable(std::string_view key) const {
  const auto it = interactables_.find(key);
  return it == interactables_.end() ? std::string(key) : it->second;
}

std::string Localization::bookTitle(const protocol::BookEntry& e) const {
  if (e.literal) return e.literalTitle;
  const auto it = book_.find(e.tmpl);
  return it == book_.end() ? e.tmpl : format(it->second.first, e.args);
}

std::string Localization::bookText(const protocol::BookEntry& e) const {
  if (e.literal) return e.literalText;
  const auto it = book_.find(e.tmpl);
  return it == book_.end() ? std::string{} : format(it->second.second, e.args);
}

std::string Localization::bookTitleName(const protocol::BookTitle& t) const {
  if (!t.literalName.empty()) return t.literalName;
  const auto it = titles_.find(t.id);
  return it == titles_.end() ? t.id : it->second.first;
}

std::string Localization::ui(std::string_view key) const {
  const auto it = ui_.find(key);
  return it == ui_.end() ? std::string(key) : it->second;
}

std::string Localization::arg(const MsgArg& a) const {
  switch (a.type) {
    case MsgArg::Type::Int:
    case MsgArg::Type::Real: return number(a.number);
    case MsgArg::Type::Text: return a.text;
    case MsgArg::Type::Item: return item(ItemId{a.id});
    case MsgArg::Type::Place: return place(static_cast<PlaceId>(a.id));
    case MsgArg::Type::Training: return training(TrainingId{a.id});
    case MsgArg::Type::Zone: return zone(static_cast<ZoneKind>(a.id));
    case MsgArg::Type::Enemy: return enemy(EnemyTypeId{a.id});
    case MsgArg::Type::Message: return message(static_cast<MessageId>(a.id));
  }
  return {};
}

std::string Localization::format(std::string_view t, const protocol::MsgArgs& args) const {
  std::string out;
  out.reserve(t.size() + 16);
  for (std::size_t i = 0; i < t.size(); ++i) {
    if (t[i] != '{') {
      out += t[i];
      continue;
    }
    const std::size_t close = t.find('}', i);
    if (close == std::string_view::npos) {
      out += t.substr(i);
      break;
    }
    const std::string_view body = t.substr(i + 1, close - i - 1);
    const std::size_t bar = body.find('|');
    const std::string_view idx = body.substr(0, bar);
    const std::string_view mod = bar == std::string_view::npos ? std::string_view{} : body.substr(bar + 1);
    std::size_t n = 0;
    const auto r = std::from_chars(idx.data(), idx.data() + idx.size(), n);
    if (r.ec != std::errc{} || n >= args.size()) {
      out += t.substr(i, close - i + 1);
      i = close;
      continue;
    }
    if (mod == "s") {
      const double v = args[n].number;
      if (v > 1 || v < -1) out += 's';
    } else {
      std::string s = arg(args[n]);
      if (mod == "lower") s = lowerFirstAware(std::move(s), true);
      else if (mod == "cap") s = capFirst(std::move(s));
      out += s;
    }
    i = close;
  }
  return out;
}

std::string Localization::message(MessageId id, const protocol::MsgArgs& args) const {
  const auto it = messages_.find(protocol::messageKey(id));
  return it == messages_.end() ? std::string(protocol::messageKey(id)) : format(it->second, args);
}

MessageId compassMessage(double dx, double dz) {
  const double a = std::fmod(std::atan2(dx, -dz) * 180.0 / 3.14159265358979323846 + 360.0, 360.0);
  static constexpr MessageId dirs[] = {MessageId::DirN, MessageId::DirNE, MessageId::DirE, MessageId::DirSE,
                                       MessageId::DirS, MessageId::DirSW, MessageId::DirW, MessageId::DirNW};
  return dirs[static_cast<int>(std::round(a / 45.0)) % 8];
}

}  // namespace rpg::client
