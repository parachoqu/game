// Painéis modais (ui/panels.js) e a faixa de abas do menu (ui/menu.js). Cada painel gera a marcação
// como o JS gerava (mesmas classes); os botões têm data-act "p:<ação>" e viram Requests para o
// servidor, que confere tudo (distância, moedas, capacidade, combate) antes de aplicar.
#include <algorithm>
#include <cmath>
#include <format>

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>

#include "client/ClientWorld.h"
#include "client/Presentation.h"
#include "client/fx/FxState.h"
#include "client/ui/GameUi.h"
#include "client/ui/Localization.h"
#include "core/protocol/Replicated.h"
#include "core/rules/Economy.h"

namespace rpg::client {

namespace proto = protocol;
using proto::MsgArg;

namespace {

std::string esc(std::string_view s) { return UiSystem::escape(s); }

std::string hex(std::uint32_t c) { return std::format("#{:06x}", c & 0xFFFFFF); }

// `${(Math.round(n * 10) / 10).toLocaleString('pt-BR')} kg`
std::string kg(double n) {
  std::string s = Localization::number(std::round(n * 10) / 10);
  std::replace(s.begin(), s.end(), '.', ',');
  return s + " kg";
}

std::string num(double v) { return Localization::number(v); }

std::string btn(std::string_view act, std::string_view arg, std::string_view label, bool primary = false, bool disabled = false) {
  return std::format(R"(<button class="btn small{}" data-act="p:{}" data-arg="{}"{}>{}</button>)", primary ? " primary" : "", act, arg,
                     disabled ? R"( disabled="")" : "", label);
}

std::string head(std::string_view title, std::string_view sub, std::string_view closeLabel = "Fechar") {
  return std::format(
      R"(<div class="corner tl"><i class="h"></i><i class="v"></i></div><div class="corner br"><i class="h"></i><i class="v"></i></div>)"
      R"(<div class="ph"><div><h2>{}</h2>{}</div><button class="btn small x" data-act="p:close">{} <kbd>Esc</kbd></button></div><div class="ribbon"><i class="mid"></i></div>)",
      title, sub.empty() ? std::string{} : std::format("<p>{}</p>", sub), closeLabel);
}

bool hasTraining(const proto::PrivateState& P, std::string_view key) {
  return std::find(P.trainings.begin(), P.trainings.end(), key) != P.trainings.end();
}

int countItem(const std::vector<proto::ItemEntry>& list, ItemId id) {
  int n = 0;
  for (const auto& e : list)
    if (e.item == id.value) n += e.qty;
  return n;
}

const char* menuTabs[][2] = {{"inventory", "Inventário"}, {"book", "Livro"}, {"map", "Mapa"}, {"intents", "Intenções"}};

}  // namespace

// ---------------------------------------------------------------- abrir e fechar

void GameUi::openPanel(Panel p, std::uint32_t data, bool intro) {
  if (bookOpen_) closeBook();
  panel_ = p;
  panelData_ = data;
  panelIntro_ = intro;
  panelLocal_ = {};
  if (p == Panel::Inventory) lastTab_ = "inventory";
  else if (p == Panel::Map) lastTab_ = "map";
  else if (p == Panel::Intents) lastTab_ = "intents";
  Rml::Element* el = byId("panel");
  const char* cls = "";
  switch (p) {
    case Panel::Market: case Panel::Forge: case Panel::Trainer: case Panel::Storage: case Panel::Event: case Panel::Inventory: case Panel::Map:
      cls = "wide";
      break;
    case Panel::Stable: case Panel::Astronomer: case Panel::Traveler: case Panel::Loot: case Panel::Pause: cls = "narrow"; break;
    default: break;
  }
  if (el) el->SetClassNames(cls);
  UiSystem::setInner(el, "");
  UiSystem::setHidden(el, false);
  panelT_ = 0;
}

void GameUi::closePanel() {
  if (panel_ == Panel::None) return;
  const Panel was = panel_;
  panel_ = Panel::None;
  UiSystem::setHidden(byId("panel"), true);
  UiSystem::setInner(byId("panel"), "");
  closedPanels_.push_back(was);
}

std::string_view GameUi::menuTab() const {
  if (bookOpen_) return "book";
  switch (panel_) {
    case Panel::Inventory: return "inventory";
    case Panel::Map: return "map";
    case Panel::Intents: return "intents";
    default: return {};
  }
}

void GameUi::openMenu(std::string_view tab) {
  if (tab.empty()) tab = lastTab_;
  // não sobrepõe outros painéis (mercado, portal…)
  if ((panel_ != Panel::None || bookOpen_) && menuTab().empty()) return;
  if (bookOpen_) closeBook();
  else if (!menuTab().empty()) closePanel();
  lastTab_ = std::string(tab);
  if (tab == "inventory") openPanel(Panel::Inventory);
  else if (tab == "book") openBook();
  else if (tab == "map") openPanel(Panel::Map);
  else openPanel(Panel::Intents);
}

void GameUi::toggleMenu(std::string_view tab) {
  const std::string_view cur = menuTab();
  if (!cur.empty() && (tab.empty() || cur == tab)) {
    if (bookOpen_) closeBook();
    else closePanel();
  } else {
    openMenu(tab.empty() ? std::string_view(lastTab_) : tab);
  }
}

void GameUi::syncMenubar() {
  Rml::Element* bar = byId("menubar");
  const std::string_view cur = menuTab();
  UiSystem::setHidden(bar, cur.empty() || !hudOn_);
  UiSystem::setClass(doc_, "with-menubar", !cur.empty() && hudOn_);
  if (cur.empty()) return;
  std::string html;
  for (const auto& [id, name] : menuTabs)
    html += std::format(R"(<button class="{}" data-act="tab:{}">{}<i class="ind"></i></button>)", cur == id ? "on" : "", id, name);
  html += R"(<span class="hint"><kbd>Tab</kbd> abre e fecha · <kbd>Esc</kbd> fecha</span>)";
  UiSystem::setInner(bar, html);
}

// ---------------------------------------------------------------- ações

void GameUi::panelAction(const std::string& act, const std::string& arg) {
  const auto P = panelInput_.world ? panelInput_.world->self() : nullptr;
  if (!P) return;
  const auto idx = [&] { return static_cast<std::uint32_t>(std::max(0, std::atoi(arg.c_str()))); };
  const auto send = [&](proto::Request r) { panelOut_.requests.push_back(std::move(r)); };
  const auto item = [&] { return data_.items.find(arg); };
  panelT_ = 0;  // redesenha no próximo quadro
  if (act == "close") {
    if (panel_ == Panel::Death) send(proto::ReqRespawn{});
    closePanel();
    return;
  }
  switch (panel_) {
    case Panel::Market: {
      const auto mk = static_cast<std::uint16_t>(panelData_);
      if (act == "buy") send(proto::ReqBuy{mk, item().value, 1});
      else if (act == "sell") send(proto::ReqSell{mk, item().value, 1, false});
      else if (act == "sellall") send(proto::ReqSell{mk, item().value, countItem(P->inv, item()), true});
      break;
    }
    case Panel::Forge:
      if (act == "craft") send(proto::ReqCraft{static_cast<std::uint16_t>(idx()), panelLocal_.useStorage});
      else if (act == "repair") send(proto::ReqRepair{static_cast<std::uint8_t>(arg == "armor" ? 1 : 0)});
      else if (act == "toggleStorage") panelLocal_.useStorage = !panelLocal_.useStorage;
      break;
    case Panel::Trainer:
      if (act == "learn") send(proto::ReqLearn{data_.trainings.find(arg).value});
      else if (act == "kit") send(proto::ReqRestartKit{});
      break;
    case Panel::Storage:
      if (act == "store") send(proto::ReqStore{idx(), false});
      else if (act == "storeAll") send(proto::ReqStore{idx(), true});
      else if (act == "storeMats") send(proto::ReqStoreMaterials{});
      else if (act == "take") send(proto::ReqWithdraw{idx(), false});
      else if (act == "takeAll") send(proto::ReqWithdraw{idx(), true});
      break;
    case Panel::Stable:
      if (act == "buy") send(proto::ReqBuyMount{});
      else if (act == "bags") send(proto::ReqStableBagsToStorage{});
      break;
    case Panel::Canteiro:
      if (act == "give") send(proto::ReqDeliver{item().value});
      break;
    case Panel::Astronomer:
      if (act == "give") send(proto::ReqGiveInstrument{});
      break;
    case Panel::Traveler:
      if (act == "tab") panelLocal_.tab = arg;
      break;
    case Panel::Loot:
      if (act == "take") send(proto::ReqTakeLoot{panelData_, static_cast<std::int32_t>(idx())});
      else if (act == "all") send(proto::ReqTakeLoot{panelData_, -1});
      break;
    case Panel::Portal:
      if (act == "enter") {
        send(proto::ReqEnterPortal{});
        closePanel();
      }
      break;
    case Panel::Death:
      if (act == "respawn") {
        send(proto::ReqRespawn{});
        panel_ = Panel::None;
        UiSystem::setHidden(byId("panel"), true);
      }
      break;
    case Panel::Inventory:
      if (act == "equip") send(proto::ReqEquip{idx()});
      else if (act == "unequip") send(proto::ReqUnequip{static_cast<std::uint8_t>(arg == "armor" ? 1 : arg == "bag" ? 2 : 0)});
      else if (act == "drink") send(proto::ReqDrink{idx()});
      else if (act == "toSaddle") send(proto::ReqToSaddle{idx()});
      else if (act == "fromSaddle") send(proto::ReqFromSaddle{idx()});
      else if (act == "drop") send(proto::ReqDrop{idx()});
      break;
    case Panel::Pause:
      panelOut_.local.push_back(act + (arg.empty() ? "" : ":" + arg));
      break;
    default: break;
  }
}

// ---------------------------------------------------------------- marcação

std::string GameUi::glyph(ItemId id) const {
  const ItemLook& g = look_.item(data_.items.key(id));
  return std::format(R"(<span class="g" style="color: {}">{}</span>)", hex(g.color), esc(g.glyph));
}

std::string GameUi::row(ItemId id, std::string_view name, std::string_view sub, std::string_view price, std::string_view acts, bool dim,
                        bool down) const {
  return std::format(R"(<div class="row{}">{}<div class="n">{}{}</div><span class="p{}">{}</span><div class="acts">{}</div></div>)", dim ? " dim" : "",
                     glyph(id), name, sub.empty() ? std::string{} : std::format("<small>{}</small>", sub), down ? " down" : "", price, acts);
}

std::string GameUi::entryName(const proto::ItemEntry& e) const {
  return esc(L_.item(ItemId{e.item})) + (e.uid ? std::format(" ({}%)", std::lround(e.cond)) : std::string{});
}

std::string GameUi::loadLine(const proto::PrivateState& P) const {
  return std::format("Bolsa {} de {} kg · {} moedas", kg(P.bagWeight), num(P.capacity), num(P.coins));
}

std::string GameUi::renderPanel() {
  const ClientWorld& w = *panelInput_.world;
  const proto::PrivateState& P = *w.self();
  const proto::WorldState& W = *w.world();
  const auto weightOf = [&](const proto::ItemEntry& e) { return data_.items.contains(ItemId{e.item}) ? data_.items[ItemId{e.item}].weight * e.qty : 0.0; };
  const auto sellPrice = [&](std::size_t mk, ItemId id, const proto::ItemEntry* e) -> std::optional<double> {
    if (mk >= W.markets.size()) return std::nullopt;
    const auto& m = W.markets[mk];
    const auto b = m.sellBase.find(id.value);
    if (b == m.sellBase.end()) return std::nullopt;
    const auto d = m.demand.find(id.value);
    return rules::sellPrice(b->second, d == m.demand.end() ? 1.0 : d->second, e && e->uid, e ? e->cond : 100);
  };
  const auto demand = [&](std::size_t mk, ItemId id) {
    if (mk >= W.markets.size()) return 1.0;
    const auto d = W.markets[mk].demand.find(id.value);
    return d == W.markets[mk].demand.end() ? 1.0 : d->second;
  };
  switch (panel_) {
    // -------------------------------------------------------------- mercado
    case Panel::Market: {
      const MarketId mk{static_cast<std::uint16_t>(panelData_)};
      if (!data_.markets.contains(mk)) return head("Mercado", "");
      const std::string& mkKey = data_.markets.key(mk);
      const MarketId other = data_.markets.find(mkKey == "vale" ? "alto" : "vale");
      const MarketDef& m = data_.markets[mk];
      std::string buyRows, sellRows;
      for (std::uint16_t i = 0; i < data_.items.size(); ++i) {
        const ItemId id{i};
        if (const auto price = m.buyPrice(id)) {
          const auto& def = data_.items[id];
          buyRows += row(id, esc(L_.item(id)), std::format("{} · {}", kg(def.weight), esc(L_.itemDesc(id))), num(*price) + " m",
                         btn("buy", data_.items.key(id), "Comprar", false, P.coins < *price));
        }
      }
      for (std::uint16_t i = 0; i < data_.items.size(); ++i) {
        const ItemId id{i};
        if (!m.sellBase(id)) continue;
        const int n = countItem(P.inv, id);
        const double d = demand(mk.value, id);
        const proto::ItemEntry* e = nullptr;
        for (const auto& x : P.inv)
          if (x.item == id.value) {
            e = &x;
            break;
          }
        const auto price = sellPrice(mk.value, id, e);
        std::string sub = n ? std::format("{} na bolsa", n) : "nada na bolsa";
        if (d < 0.97) sub += std::format(" · procura baixa ({}%)", std::lround(d * 100));
        if (other.valid())
          if (const auto op = sellPrice(other.value, id, nullptr)) sub += std::format(" · no {}: {} m", mkKey == "vale" ? "Alto" : "Vale", num(*op));
        sellRows += row(id, esc(L_.item(id)), sub, price ? num(*price) + " m" : "",
                        n ? btn("sell", data_.items.key(id), "Vender 1") + btn("sellall", data_.items.key(id), "Todos") : "", !n, d < 0.97);
      }
      return head(esc(L_.market(mk)), loadLine(P) + ". Compra e retirada acontecem aqui; o que está em outro entreposto fica lá.") +
             std::format(R"(<div class="pb"><div class="cols"><section><h3>O mercado vende</h3><div class="rows">{}</div></section><section><h3>O mercado compra</h3><div class="rows">{}</div>)"
                         R"(<p class="note">Cada venda reduz a procura local; ela se recupera com o tempo. Preços do outro entreposto são apenas consulta.</p></section></div></div>)",
                         buyRows, sellRows);
    }
    // -------------------------------------------------------------- bancada
    case Panel::Forge: {
      std::string rec;
      for (std::uint16_t i = 0; i < data_.recipes.size(); ++i) {
        const RecipeDef& r = data_.recipes[RecipeId{i}];
        std::string why;
        if (r.requiredTraining.valid() && !hasTraining(P, data_.trainings.key(r.requiredTraining)))
          why = L_.message(proto::MessageId::CraftRequires);
        std::string ins;
        bool missing = false;
        for (const auto& [id, n] : r.inputs) {
          const int have = countItem(P.inv, id) + (panelLocal_.useStorage ? countItem(P.storage, id) : 0);
          if (have < n) missing = true;
          if (!ins.empty()) ins += ", ";
          ins += std::format(R"(<span style="color: {}">{}× {} ({})</span>)", have >= n ? "#ece4d2" : "#e0624a", n, esc(UiSystem::lower(L_.item(id))), have);
        }
        if (why.empty() && missing) why = L_.message(proto::MessageId::CraftMissing);
        std::string sub = esc(L_.recipeNote(r.out));
        if (r.requiredTraining.valid()) sub += " · " + esc(L_.training(r.requiredTraining));
        sub += " — " + ins;
        rec += row(r.out, std::format("{}{}", r.quantity > 1 ? std::format("{}× ", r.quantity) : "", esc(L_.item(r.out))), sub,
                   why.empty() ? "" : std::format(R"(<span class="why">{}</span>)", esc(why)), btn("craft", std::to_string(i), "Fabricar", why.empty(), !why.empty()),
                   !why.empty());
      }
      std::string rep;
      const auto repRow = [&](const std::optional<proto::ItemEntry>& g, const char* slot) {
        if (!g) return;
        const rules::RepairCost c = rules::repairCost(g->cond);
        rep += row(ItemId{g->item}, esc(L_.item(ItemId{g->item})), std::format("{}% de condição{}", std::lround(g->cond), g->cond <= 0 ? " · inutilizado" : ""),
                   g->cond >= 100 ? "intacto" : std::format("{} m{}", num(c.coins), c.lingote ? " + 1 lingote" : ""),
                   g->cond < 100 ? btn("repair", slot, "Reparar") : "");
      };
      repRow(P.weapon, "weapon");
      repRow(P.armor, "armor");
      if (rep.empty()) rep = R"(<p class="note">Nenhum equipamento equipado para reparar.</p>)";
      return head("Bancada do Vale", loadLine(P) + ". Fabricar consome materiais; reparar consome moedas e, se o item estiver muito gasto, um lingote.") +
             std::format(R"(<div class="pb"><div class="cols"><section><h3>Receitas</h3><div class="rows">{}</div>)"
                         R"(<p class="note"><span class="check" data-act="p:toggleStorage">{} Usar materiais do armazém do Vale</span></p></section>)"
                         R"(<section><h3>Reparo do conjunto equipado</h3><div class="rows">{}</div>)"
                         R"(<div class="callout" style="margin-top: 12px">Desgaste não destrói o item: em 0% ele fica inutilizado até o reparo. Derrotas em áreas perigosas seguem outra regra.</div></section></div></div>)",
                         rec, panelLocal_.useStorage ? "☑" : "☐", rep);
    }
    // -------------------------------------------------------------- instrutor
    case Panel::Trainer: {
      struct Group {
        const char* title;
        std::vector<const char*> ids;
      };
      static const std::vector<std::pair<const char*, const char*>> icon = {
          {"espadachim", "espada"},          {"arqueiro", "arco"},           {"artesao", "martelo"},       {"alquimista", "pocao"},
          {"lutador", "manoplas"},           {"combatente_haste", "lanca"},  {"concussao_corte", "machado"}, {"assassino", "adaga"},
          {"magia_elemental", "cajado_gelo"}, {"magia_natural", "fetiche_metamorfose"}, {"artes_espirituais", "tomo_sagrado"}};
      static const Group groups[] = {
          {"Armas e Famílias de Combate (Cap. 21)", {"espadachim", "lutador", "concussao_corte", "combatente_haste", "assassino", "arqueiro"}},
          {"Escolas e Tradições de Magia (Cap. 22)", {"magia_elemental", "magia_natural", "artes_espirituais"}},
          {"Ofícios e Produção (Cap. 05)", {"artesao", "alquimista"}}};
      std::string sections;
      for (const Group& g : groups) {
        std::string rows;
        for (const char* key : g.ids) {
          const TrainingId tid = data_.trainings.find(key);
          if (!tid.valid()) continue;
          std::string ic = "espada";
          for (const auto& [k, v] : icon)
            if (std::string_view(k) == key) ic = v;
          ItemId icId = data_.items.find(ic);
          if (!icId.valid()) icId = data_.items.find("espada");
          const bool learned = hasTraining(P, key);
          const int cost = data_.trainings[tid].cost;
          rows += row(icId, esc(L_.training(tid)), esc(L_.trainingDesc(tid)), learned ? "aprendido" : std::format("{} m", cost),
                      learned ? "" : btn("learn", key, "Aprender", P.coins >= cost, P.coins < cost), learned);
        }
        sections += std::format(R"(<section><h3>{}</h3><div class="rows">{}</div></section>)", g.title, rows);
      }
      bool noWeapon = !P.weapon;
      for (const auto& e : P.inv)
        if (data_.items.contains(ItemId{e.item}) && data_.items[ItemId{e.item}].kind == ItemKind::Weapon) noWeapon = false;
      std::string kit;
      if (noWeapon)
        kit = std::format(R"(<div class="callout warn"><b>Sem arma?</b> O armazém cede um kit básico para recomeçar: martelo de oficina e uma poção. {}</div>)",
                          btn("kit", "", P.coins >= data_.restartKitPrice ? std::format("Kit de recomeço — {} m", num(data_.restartKitPrice)) : "Kit de recomeço — cedido"));
      return head("Instrutor do Vale", "Aprender libera o uso de famílias de armas, técnicas e tradições mágicas. Sem classes obrigatórias.") +
             std::format(R"(<div class="pb stack">{}<div class="callout">Conhecimentos, receitas e o Livro nunca entram no saque. Uma derrota retira bens elegíveis na zona, mas preserva sua trajetória.</div>{}</div>)",
                         sections, kit);
    }
    // -------------------------------------------------------------- armazém
    case Panel::Storage: {
      const auto list = [&](const std::vector<proto::ItemEntry>& arr, const char* act, const char* label) {
        if (arr.empty()) return std::string(R"(<p class="note">Vazio.</p>)");
        std::string out;
        for (std::size_t i = 0; i < arr.size(); ++i) {
          const auto& e = arr[i];
          out += row(ItemId{e.item}, entryName(e), std::format("{} · {}", e.qty, kg(weightOf(e))), "",
                     btn(act, std::to_string(i), label) + (e.qty > 1 ? btn(std::string(act) + "All", std::to_string(i), "Todos") : ""));
        }
        return out;
      };
      return head("Armazém do Vale", loadLine(P) + ". Itens guardados aqui não entram em nenhuma regra de saque.") +
             std::format(R"(<div class="pb"><div class="cols"><section><h3>Bolsa</h3><div class="rows">{}</div>{}</section><section><h3>Guardado</h3><div class="rows">{}</div></section></div></div>)",
                         list(P.inv, "store", "Guardar"), P.inv.empty() ? "" : "<p>" + btn("storeMats", "", "Guardar todos os materiais") + "</p>",
                         list(P.storage, "take", "Retirar"));
    }
    // -------------------------------------------------------------- estábulo
    case Panel::Stable: {
      const ItemId montaria = data_.items.find("montaria");
      std::string bags;
      if (!P.stableBags.empty()) {
        std::string names;
        for (const auto& e : P.stableBags) names += (names.empty() ? "" : ", ") + entryName(e);
        bags = std::format(R"(<div class="callout">O cuidador guardou os alforjes que voltaram com o animal: {}. {}</div>)", names, btn("bags", "", "Levar ao armazém"));
      }
      return head("Estábulo do Vale", "Montarias ampliam decisões de transporte; não são fuga instantânea.") +
             std::format(R"(<div class="pb stack">{}<dl class="kv"><dt>Chamar e montar</dt><dd>Tecla R, fora de combate. Leva alguns segundos.</dd><dt>Alforjes</dt><dd>Seguem com o animal. Em Fronteira e Full Loot fazem parte da carga transportada.</dd><dt>Full Loot</dt><dd>O item de montaria entra na regra de perda.</dd><dt>Região Turbulenta</dt><dd>Montarias não atravessam.</dd></dl>{}</div>)",
                         row(montaria, esc(L_.item(montaria)), "+60 kg em alforjes · mais rápido em estrada · ser atingido derruba da sela",
                             P.mount ? "possui" : std::format("{} m", num(data_.mountPrice)),
                             P.mount ? "" : btn("buy", "", "Comprar", true, P.coins < data_.mountPrice), P.mount.has_value()),
                         bags);
    }
    // -------------------------------------------------------------- quadro de relatos
    case Panel::Board: {
      static const char* camp[][2] = {{"Estabelecido", "Patrulhas percorrem a Passagem; fogueiras acesas."},
                                      {"Pressionado", "Patrulhas recolhidas; defesa reforçada na entrada."},
                                      {"Deslocado", "Acampamento vazio; trilhas novas rumo às Colinas do Oeste."},
                                      {"Em recuperação", "Pequenos grupos retornam e reconstroem."}};
      const auto& c = camp[std::min<std::size_t>(W.campState, 3)];
      const std::size_t n = W.vanished.size();
      const char* phaseName = n <= 3 ? "indício" : n <= 8 ? "repercussão" : "ruptura";
      const char* phaseDesc = n <= 3 ? "astrônomos percebem ausências isoladas." : n <= 8 ? "referências de navegação deixam de coincidir." : "fenômenos incomuns aparecem com mais frequência.";
      double alto = 0;
      const MarketId altoId = data_.markets.find("alto");
      if (const auto p = sellPrice(altoId.value, data_.items.find("ferramentas"), nullptr)) alto = *p;
      std::string notes;
      notes += std::format(R"(<div class="callout warn"><b>Passagem Estreita — {}.</b> {}</div>)", W.evtDone ? "reaberta" : "instável",
                           W.evtDone ? "Guardas no canteiro e no meio da garganta. Continua sendo Fronteira."
                                     : std::format("O canteiro na entrada pede ferramentas e lingotes. Abastecimento: {}%.", static_cast<int>(std::floor(W.evtProgress))));
      notes += std::format(R"(<div class="callout bad"><b>Acampamento das Garras — {}.</b> {}</div>)", c[0], c[1]);
      notes += std::format(R"(<div class="callout cobalt"><b>Comércio.</b> O Entreposto Alto paga cerca de {} moedas por ferramenta. A Estrada Longa é protegida, porém mais demorada; a Passagem é curta e exposta.</div>)", num(alto));
      notes += std::format(R"(<div class="callout" style="border-left-color: #dfe8ff"><b style="color: #dfe8ff">O céu.</b> {} Há quem diga que existe um instrumento antigo a oeste da Passagem.</div>)",
                           n ? std::format("Relatos de {} ponto{} de luz que sumiram. Fase observada: {} — {}", n, n > 1 ? "s" : "", phaseName, phaseDesc)
                             : "Astrônomos falam de brilhos desiguais. Ninguém concorda sobre o motivo.");
      notes += W.portal ? std::format(R"(<div class="callout turb"><b>Rasgo violeta.</b> Visto em {}. Quem atravessa entra sem companhia e sem montaria.</div>)", esc(L_.place(static_cast<PlaceId>(W.portalPlace))))
                        : R"(<div class="callout turb"><b>Rasgos violetas.</b> Aparecem em lugares diferentes e fecham depois de pouco tempo. Quem volta traz fragmentos que valem bem no Alto.</div>)";
      return head("Quadro de relatos", "O que se comenta no Vale. Cada relato descreve o estado atual do mundo.") + std::format(R"(<div class="pb stack">{}</div>)", notes);
    }
    // -------------------------------------------------------------- canteiro
    case Panel::Canteiro: {
      const double total = W.evtPlayerPts + W.evtOthersPts;
      const long pct = std::lround(total > 0 ? W.evtPlayerPts / total * 100 : 0);
      const ItemId ferr = data_.items.find("ferramentas"), ling = data_.items.find("lingote");
      const int nf = countItem(P.inv, ferr), nl = countItem(P.inv, ling);
      static const std::pair<const char*, const char*> labels[] = {{"ferramentas", "ferramentas"}, {"lingotes", "lingotes"}, {"reconhecimento", "reconhecimento"},
                                                                   {"saqueadores", "saqueadores afastados"}, {"acampamento", "pressão sobre as Garras"}};
      std::string parts;
      for (const auto& [k, label] : labels)
        if (const auto it = W.evtContrib.find(k); it != W.evtContrib.end() && it->second > 0)
          parts += std::format("{}{} ({})", parts.empty() ? "" : ", ", label, std::lround(it->second));
      return head("Canteiro da Passagem", W.evtDone ? "A Passagem foi reaberta. O canteiro segue comprando, com pagamento menor."
                                                    : "Um trecho da Passagem deixou de ser confiável. A mestra do canteiro reúne materiais, relatos e escoltas.") +
             std::format(R"(<div class="pb stack"><div><strong>Abastecimento {}%</strong><div class="meterbig"><i style="width: {:.1f}%"></i></div>)"
                         R"(<p class="note">Sua parte: {}. Outros participantes também abastecem o canteiro.</p></div>)"
                         R"(<div class="rows">{}{}</div>)"
                         R"(<div class="callout warn">Também contam: registrar o movimento na ruína de vigia, afastar saqueadores dentro da Passagem e pressionar o Acampamento das Garras. Nada disso é obrigatório.</div></div>)",
                         static_cast<int>(std::floor(W.evtProgress)), W.evtProgress,
                         W.evtPlayerPts > 0 ? std::format("{}% das contribuições registradas — {}", pct, parts) : "nenhuma contribuição ainda",
                         row(ferr, "Entregar ferramentas", std::format("{} na bolsa · {} m cada", nf, num(rules::canteiroPay(true, W.evtDone))), "",
                             btn("give", "ferramentas", "Entregar todas", nf > 0, nf == 0), nf == 0),
                         row(ling, "Entregar lingotes", std::format("{} na bolsa · {} m cada", nl, num(rules::canteiroPay(false, W.evtDone))), "",
                             btn("give", "lingote", "Entregar todos", false, nl == 0), nl == 0));
    }
    // -------------------------------------------------------------- astrônoma
    case Panel::Astronomer: {
      const int has = countItem(P.inv, data_.items.find("instrumento"));
      const std::size_t n = W.vanished.size();
      const std::string quote = n ? std::format("Contei {} ausência{} desde que cheguei.", n, n > 1 ? "s" : "") : "O brilho da Lanterna oscila de um jeito que não oscilava.";
      return head("A astrônoma do observatório", "Observação, interpretação e o que ninguém sabe ainda.") +
             std::format(R"(<div class="pb stack"><p>“{} Não sei a causa. Ninguém sabe.”</p>)"
                         R"(<div class="callout" style="border-left-color: #dfe8ff"><b style="color: #dfe8ff">Interpretações que circulam.</b> Os navegadores do Alto falam em névoa alta. Os ferreiros do Vale dizem que é sinal de minério novo sob a serra. Os guardiões da ruína de vigia juram que as estrelas estão sendo apagadas de propósito. Nenhuma delas foi confirmada.</div>{}</div>)",
                         quote,
                         W.instrumentGiven ? R"(<p class="note">O instrumento que você trouxe já está em uso.</p>)"
                                           : "<p>“Um instrumento novo ajudaria nas medições: dois lingotes e um cristal celeste, trabalho de artesão.”</p>" +
                                                 btn("give", "", has ? "Entregar instrumento (60 m)" : "Você não tem um instrumento", has > 0, has == 0));
    }
    // -------------------------------------------------------------- viajante
    case Panel::Traveler: {
      const auto& travelers = panelInput_.statics->layout().travelers;
      const TravelerText* t = panelData_ < travelers.size() ? L_.traveler(travelers[panelData_].key) : nullptr;
      if (!t) return head("Viajante", "");
      const bool eq = panelLocal_.tab != "bio";
      std::string body;
      if (eq) {
        std::string rows;
        for (const auto& g : t->gear) rows += std::format(R"(<div class="row"><span class="g">·</span><div class="n">{}</div><span></span><div></div></div>)", esc(g));
        body = std::format(R"(<div class="rows">{}</div><p class="note">A inspeção de equipamento não revela a biografia.</p>)", rows);
      } else {
        std::string items;
        for (const auto& [k, v] : t->publicEntries) items += std::format(R"(<div class="callout" style="border-left-color: #dccaa2"><b style="color: #dccaa2">{}.</b> {}</div>)", esc(k), esc(v));
        const std::string first = t->name.substr(0, t->name.find(' '));
        body = std::format(R"(<div class="stack">{}</div><p class="note">Só aparecem os marcos que {} escolheu exibir. O resto do Livro é pessoal.</p>)", items, esc(first));
      }
      return head(esc(t->name), esc(t->role) + " · viajante (simula outro jogador)") +
             std::format(R"(<div class="pb"><p>{} {}</p>{}</div>)", btn("tab", "eq", "Equipamento", eq), btn("tab", "bio", "Biografia pública", !eq), body);
    }
    // -------------------------------------------------------------- carga no chão
    case Panel::Loot: {
      const auto& lv = panelInput_.fx->loot;
      if (!lv || lv->bag != panelData_ || lv->items.empty()) return head("Carga", "Vazia.") + R"(<div class="pb"><p class="note">Nada restou.</p></div>)";
      const double left = std::max(0.0, lv->expires - W.time);
      std::string rows;
      for (std::size_t i = 0; i < lv->items.size(); ++i) {
        const auto& e = lv->items[i];
        rows += row(ItemId{e.item}, entryName(e), std::format("{} · {}", e.qty, kg(weightOf(e))), "", btn("take", std::to_string(i), "Pegar"));
      }
      return head(lv->own ? "Sua carga deixada para trás" : "Carga no chão",
                  std::format("{} · {} · some em {} min", esc(L_.place(static_cast<PlaceId>(lv->place))), esc(L_.zone(static_cast<ZoneKind>(lv->zone))),
                              static_cast<int>(std::ceil(left / 60)))) +
             std::format(R"(<div class="pb"><div class="rows">{}</div><p>{}</p><p class="note">{}</p></div>)", rows, btn("all", "", "Pegar tudo", true), loadLine(P));
    }
    // -------------------------------------------------------------- portal
    case Panel::Portal: {
      std::string why;
      if (P.mounted) why = L_.message(proto::MessageId::MountNoTurbulent);
      else if (P.state != static_cast<std::uint8_t>(proto::PlayerState::Free)) why = L_.message(proto::MessageId::RequestTooFar);
      const long left = W.portal ? static_cast<long>(std::ceil(W.portalExpires - W.time)) : 0;
      return head("Um rasgo de luz violeta", std::format("Fecha em {} s. Leia antes de atravessar: não há confirmação dentro da região.", left)) +
             std::format(R"(<div class="pb stack"><dl class="kv">)"
                         R"(<dt>Categoria</dt><dd><span class="tag" style="color: #a07cf2">Turbulenta</span> — Full Loot</dd>)"
                         R"(<dt>Acesso</dt><dd>Individual. Montarias e alforjes ficam do lado de fora.</dd>)"
                         R"(<dt>Outras pessoas</dt><dd>Aparecem como silhuetas sem nome. Arma e ataques continuam legíveis.</dd>)"
                         R"(<dt>Objetivo</dt><dd>Fragmentos anômalos em três santuários.</dd>)"
                         R"(<dt>Saídas</dt><dd>Três pedras de extração. Extrair leva 4 s e é interrompido por dano.</dd>)"
                         R"(<dt>Colapso</dt><dd>Em 4 minutos a região colapsa. Quem estiver dentro perde tudo o que leva.</dd>)"
                         R"(<dt>Retorno</dt><dd>Ao local deste portal.</dd>)"
                         R"(<dt>Se cair</dt><dd>Tudo o que você leva fica na região. Conhecimentos, moedas e o Livro permanecem.</dd></dl>{}</div>)"
                         R"(<div class="pf">{}{}</div>)",
                         why.empty() ? "" : std::format(R"(<div class="callout warn">{}</div>)", esc(why)), btn("close", "", "Não atravessar"),
                         btn("enter", "", "Atravessar sem companhia", true, !why.empty()));
    }
    // -------------------------------------------------------------- derrota
    case Panel::Death: {
      if (!panelInput_.fx->death) return "";
      const proto::DeathReport& r = *panelInput_.fx->death;
      const auto zone = static_cast<ZoneKind>(r.zone);
      const ZoneLook& zl = look_.zone(zone);
      const auto list = [&](const std::vector<proto::DeathLine>& arr) {
        if (arr.empty()) return std::string(R"(<p class="note">Nada.</p>)");
        std::string out = "<ul>";
        for (const auto& l : arr) out += std::format("<li>{}{}</li>", l.qty > 1 ? std::format("{}× ", l.qty) : "", esc(L_.item(ItemId{l.item})));
        return out + "</ul>";
      };
      std::string kept = "<ul>";
      for (const MsgArg& k : r.kept) {
        const std::string s = k.type == MsgArg::Type::Message && static_cast<proto::MessageId>(k.id) == proto::MessageId::KeptCoins
                                  ? L_.message(proto::MessageId::KeptCoins, {MsgArg::integer(r.coinsKept)})
                                  : L_.arg(k);
        kept += std::format("<li>{}</li>", esc(s));
      }
      kept += "</ul>";
      return std::format(R"(<div class="corner tl"><i class="h"></i><i class="v"></i></div><div class="corner br"><i class="h"></i><i class="v"></i></div>)"
                         R"(<div class="ph"><div><h2>Você caiu em {}</h2><p><span class="tag" style="color: {}">{} {}</span> {}</p></div></div><div class="ribbon"><i class="mid"></i></div>)"
                         R"(<div class="pb"><div class="cols"><section><h3>Ficou no local</h3>{}{}{}</section>)"
                         R"(<section><h3>Permanece com você</h3>{}{}</section></div></div>)"
                         R"(<div class="pf">{}</div>)",
                         esc(L_.place(static_cast<PlaceId>(r.place))), hex(zl.color), esc(zl.mark), esc(L_.zone(zone)), esc(L_.zoneDeath(zone)), list(r.lost),
                         r.destroyed.empty() ? "" : "<h3>Destruído</h3>" + list(r.destroyed),
                         r.bag && zone != ZoneKind::Turbulenta
                             ? R"(<p class="note">Um saco com a carga ficou no chão, marcado no mapa. Saqueadores podem recolhê-lo; derrotá-los devolve a carga ao chão.</p>)"
                             : "",
                         kept,
                         r.hasWeapon ? "" : R"(<div class="callout warn">Sem arma equipada: o instrutor do Vale oferece um kit de recomeço barato.</div>)",
                         btn("respawn", "", "Retornar ao abrigo do Vale", true));
    }
    // -------------------------------------------------------------- as quatro perguntas
    case Panel::Event: {
      if (!panelInput_.fx->eventDone) return "";
      const auto& q = *panelInput_.fx->eventDone;
      static const std::pair<const char*, const char*> labels[] = {{"ferramentas", "Ferramentas entregues"}, {"lingotes", "Lingotes entregues"},
                                                                   {"reconhecimento", "Reconhecimento da passagem"}, {"saqueadores", "Saqueadores afastados"},
                                                                   {"acampamento", "Pressão sobre o acampamento"}};
      std::string parts;
      for (const auto& p : q.parts) {
        std::string label = p.kind;
        for (const auto& [k, v] : labels)
          if (p.kind == k) label = v;
        parts += std::format("{}{} ({})", parts.empty() ? "" : " · ", label, num(p.pts));
      }
      return head("A Passagem Estreita foi reaberta", "Toda mudança importante responde a quatro perguntas.", "Entendi") +
             std::format(R"(<div class="pb"><div class="q4">)"
                         R"(<div><h4>O que mudou</h4><p>Guardas passaram a vigiar o canteiro e o meio da Passagem. Saqueadores reaparecem com menos frequência. O Entreposto Alto voltou a receber ferramentas e paga menos por elas; o Vale abriu novas encomendas.</p></div>)"
                         R"(<div><h4>Quais ações contribuíram</h4><div class="split"><i style="width: {}%; background-color: #46b3a8"></i><i style="width: {}%; background-color: #3b7fb6"></i></div>)"
                         R"(<p>Você: <b>{}%</b> · outros participantes: <b>{}%</b>.</p><p class="note">{}</p><p class="note">Outros participantes forneceram materiais, fizeram escoltas e combateram.</p></div>)"
                         R"(<div><h4>Quem foi afetado</h4><p>Comerciantes do Entreposto Alto, transportadores da rota, o canteiro e quem cruza a Passagem.</p></div>)"
                         R"(<div><h4>Que respostas ainda são possíveis</h4><p>A Passagem continua sendo Fronteira: a regra de perda não mudou. O Acampamento das Garras pode se recuperar e voltar a patrulhar. Os preços se ajustam com as próximas entregas.</p></div>)"
                         R"(</div><p class="note">O Livro registra a participação comprovada; a história da região registra o resultado coletivo.</p></div>)",
                         q.you, q.others, q.you, q.others, parts.empty() ? "Você não contribuiu desta vez." : parts);
    }
    // -------------------------------------------------------------- inventário
    case Panel::Inventory: {
      const auto slot = [&](const std::optional<proto::ItemEntry>& g, const char* s, const char* label) {
        if (!g) return std::format(R"(<div class="eq"><small>{}</small><b style="color: #a8a193">Vazio</b></div>)", label);
        const bool mount = std::string_view(s) == "mount";
        return std::format(R"(<div class="eq"><small>{}</small><b>{}</b><span>{}</span>{}</div>)", label, esc(L_.item(ItemId{g->item})),
                           mount ? "Alforjes de 60 kg" : std::format("{}% de condição", std::lround(g->cond)), mount ? "" : btn("unequip", s, "Remover"));
      };
      const bool reach = P.saddleReachable;
      std::string invRows;
      for (std::size_t i = 0; i < P.inv.size(); ++i) {
        const auto& e = P.inv[i];
        const ItemId id{e.item};
        if (!data_.items.contains(id)) continue;
        const ItemDef& it = data_.items[id];
        std::string acts;
        if (it.kind == ItemKind::Weapon || it.kind == ItemKind::Armor || it.kind == ItemKind::Bag) acts += btn("equip", std::to_string(i), "Equipar");
        if (data_.items.key(id) == "pocao") acts += btn("drink", std::to_string(i), "Beber");
        if (reach) acts += btn("toSaddle", std::to_string(i), "→ Alforjes");
        acts += btn("drop", std::to_string(i), "Largar");
        std::string sub = std::format("{} · {}", e.qty, kg(it.weight * e.qty));
        if (it.requiredTraining.valid() && !hasTraining(P, data_.trainings.key(it.requiredTraining))) sub += " · requer " + esc(L_.training(it.requiredTraining));
        invRows += row(id, entryName(e), sub, "", acts);
      }
      if (invRows.empty()) invRows = R"(<p class="note">Bolsa vazia.</p>)";
      std::string sadRows;
      if (!P.mount) {
        sadRows = R"(<p class="note">Sem montaria. O estábulo do Vale vende uma.</p>)";
      } else if (P.saddle.empty()) {
        sadRows = R"(<p class="note">Alforjes vazios.</p>)";
      } else {
        for (std::size_t i = 0; i < P.saddle.size(); ++i) {
          const auto& e = P.saddle[i];
          sadRows += row(ItemId{e.item}, entryName(e), std::format("{} · {}", e.qty, kg(weightOf(e))), "", reach ? btn("fromSaddle", std::to_string(i), "← Bolsa") : "");
        }
      }
      std::string fam = "punhos";
      if (P.weapon && P.weapon->cond > 0 && data_.items.contains(ItemId{P.weapon->item}) && data_.items[ItemId{P.weapon->item}].family.valid())
        fam = data_.weapons.key(data_.items[ItemId{P.weapon->item}].family);
      std::string skills;
      for (const SkillId s : data_.weapons[data_.weapons.find(fam)].skills)
        if (s.valid()) skills += std::format("{}<b>{}</b> — {}", skills.empty() ? "" : " · ", esc(L_.skill(s)), esc(L_.skillDesc(s)));
      std::string trainings;
      for (const auto& t : P.trainings) {
        const TrainingId tid = data_.trainings.find(t);
        trainings += (trainings.empty() ? "" : ", ") + esc(tid.valid() ? L_.training(tid) : t);
      }
      const char* ls = P.load == 1 ? "sobrecarga" : P.load == 2 ? "no limite" : "carga normal";
      return head("Inventário", std::format("Bolsa {} de {} kg ({}) · {} moedas", kg(P.bagWeight), num(P.capacity), ls, num(P.coins))) +
             std::format(R"(<div class="pb stack"><div class="slots4">{}{}{}{}</div>)"
                         R"(<p class="note">Técnicas da arma atual: {} Conhecimentos: {}.</p>)"
                         R"(<div class="cols"><section><h3>Bolsa (carga transportada)</h3><div class="rows">{}</div></section>)"
                         R"(<section><h3>Alforjes {}</h3><div class="rows">{}</div>{})"
                         R"(<div class="callout" style="margin-top: 10px">Peso em três estados: normal até 100%; sobrecarga até 130% (lento e sem esquiva); acima disso nada mais entra. Trocar de conjunto não é possível em combate.</div></section></div></div>)",
                         slot(P.weapon, "weapon", "Arma"), slot(P.armor, "armor", "Armadura"), slot(P.bag, "bag", "Bolsa"), slot(P.mount, "mount", "Montaria"),
                         skills.empty() ? "nenhuma." : skills, trainings, invRows,
                         P.mount ? std::format("— {} de {} kg", kg(P.saddleWeight), num(P.saddleCapacity)) : "", sadRows,
                         P.mount && !reach ? R"(<p class="note">Aproxime-se da montaria para mexer nos alforjes.</p>)" : "");
    }
    // -------------------------------------------------------------- intenções
    case Panel::Intents: {
      const auto stat = [&](const char* k) {
        const auto it = P.stats.find(k);
        return it == P.stats.end() ? 0.0 : it->second;
      };
      const double total = W.evtPlayerPts + W.evtOthersPts;
      const long pct = std::lround(total > 0 ? W.evtPlayerPts / total * 100 : 0);
      struct Intent {
        const char *kind, *title, *hint;
        bool done;
        std::string prog;
      };
      const double crafted = stat("crafted_ferramentas");
      const Intent list[] = {
          {"Produzir", "Forjar ferramentas na bancada do Vale", "Minério e madeira no Bosque das Forjas, a oeste. Dois minérios viram um lingote.", crafted >= 3,
           std::format("{}/3", num(std::min(3.0, crafted)))},
          {"Comerciar", "Vender ferramentas no Entreposto Alto", "O Alto paga mais. Escolha entre a Estrada Longa (protegida, longa) e a Passagem (curta, Fronteira).",
           stat("sold_alto_ferramentas") > 0, ""},
          {"Contribuir", "Ajudar a reabrir a Passagem Estreita", "O canteiro na entrada da Passagem aceita materiais e relatos.", W.evtDone && W.evtPlayerPts > 0,
           W.evtPlayerPts > 0 ? std::format("{}% das contribuições", pct) : ""},
          {"Explorar", "Encontrar a estrutura antiga que os mapas não mostram", "Relatos falam de um instrumento a oeste da Passagem.", P.flags.contains("disc_observatorio"), ""},
          {"Observar", "Registrar uma ausência no céu", "Estrelas somem aos poucos. À noite, a diferença é visível.", !W.observed.empty(), ""},
          {"Arriscar", "Atravessar um rasgo violeta e voltar", "Portais temporários levam a uma Região Turbulenta. Leve só o que aceita perder.", stat("extracoes") > 0, ""},
      };
      std::string body;
      for (const Intent& it : list)
        body += std::format(R"(<div class="intent{}"><span class="ck">{}</span><div><span class="kind">{}{}</span><h4>{}</h4><p>{}</p></div></div>)", it.done ? " done" : "",
                            it.done ? "✓" : "", it.kind, !it.prog.empty() && !it.done ? " · " + it.prog : "", it.title, it.hint);
      if (panelIntro_)
        body += R"(<div class="callout" style="margin-top: 14px"><b>Controles.</b> WASD anda · Shift + WASD corre · arraste o chão com o mouse para girar e inclinar a câmera · clique num inimigo para atacar · clique num lugar ou pessoa para usar (ou F) · segure o botão direito para mirar de perto e clique para disparar · Espaço pula (Shift + Espaço: salto impulsionado) · Ctrl agacha · C esquiva · Q/E técnicas da arma · Tab abre o menu · Tab + 1/2/3 escolhe a distância da câmera.</div>)";
      return head(panelIntro_ ? "Escolha uma intenção" : "Intenções",
                  panelIntro_ ? "Nada aqui é obrigatório. O mundo continua mudando enquanto você decide como viver nele."
                              : "Sugestões de caminho. O Livro registra o que você fizer, não o que estava na lista.",
                  panelIntro_ ? "Começar" : "Fechar") +
             std::format(R"(<div class="pb">{}</div>)", body);
    }
    // -------------------------------------------------------------- mapa
    case Panel::Map: {
      std::string leg;
      for (ZoneKind z : {ZoneKind::Protegida, ZoneKind::Fronteira, ZoneKind::FullLoot})
        leg += std::format(R"(<span><i style="background-color: {}"></i>{}</span>)", hex(look_.zone(z).color), esc(L_.zone(z)));
      leg += R"(<span><i style="background-color: #f2c86a"></i>Sua carga no chão</span><span><i style="background-color: #c9a8ff"></i>Rasgo violeta</span><span><i style="background-color: #c8b49a"></i>Montaria</span>)";
      // nomes por cima da imagem (o canvas da demo escrevia o texto)
      std::string labels;
      const double s = 640.0 / WorldMap::kSize;
      for (const MapLabel& l : map_.labels(s, 0, 0, [&](const std::string& k) { return P.flags.contains("disc_" + k); }))
        labels += std::format(R"(<span class="maplabel{}" style="left: {:.0f}px; top: {:.0f}px;">{}</span>)", l.known ? "" : " unknown", l.x, l.y - 20,
                              l.known ? esc(L_.ui("map.place." + l.key)) : "?");
      return head("Mapa do recorte", "Lugares não descobertos aparecem como “?”. Nomes provisórios.") +
             std::format(R"(<div class="pb"><div id="fullmap-wrap"><img id="fullmap" src="dyn:fullmap"/><div id="fullmap-labels">{}</div></div><div class="legend">{}</div></div>)", labels, leg);
    }
    // -------------------------------------------------------------- pausa
    case Panel::Pause: {
      const PanelInput& in = panelInput_;
      std::string sens;
      for (const auto& [id, label] : {std::pair{"baixa", "Lenta"}, std::pair{"media", "Normal"}, std::pair{"alta", "Rápida"}})
        sens += btn("camsens", id, label, in.camSens == id) + " ";
      std::string quality;
      for (const auto& [id, label] : {std::pair{"alta", "Alta"}, std::pair{"media", "Média"}, std::pair{"baixa", "Baixa"}})
        quality += btn("quality", id, label, in.quality == id) + " ";
      return head("Pausa", "O mundo desta demo fica parado enquanto este menu está aberto.", "Voltar") +
             std::format(R"(<div class="pb stack"><dl class="kv">)"
                         R"(<dt>Andar · Correr</dt><dd>WASD (relativo à câmera) · segure Shift para correr</dd>)"
                         R"(<dt>Agachar</dt><dd>Segure Ctrl: passo curto e sem corrida</dd>)"
                         R"(<dt>Pular · Salto impulsionado</dt><dd>Espaço · Shift + Espaço (mais alto e com impulso à frente; gasta mais vigor)</dd>)"
                         R"(<dt>Atacar</dt><dd>Clique no inimigo: aproxima e ataca até ele cair</dd>)"
                         R"(<dt>Usar</dt><dd>Clique no lugar, pessoa ou recurso — ou F no mais próximo</dd>)"
                         R"(<dt>Mirar</dt><dd>Segure o botão direito: a câmera aproxima por cima do ombro, o corpo acompanha a mira e o clique dispara na hora</dd>)"
                         R"(<dt>Esquivar</dt><dd>C</dd><dt>Técnicas</dt><dd>Q e E, na direção do alvo ou do cursor</dd>)"
                         R"(<dt>Poção · Montaria</dt><dd>1 · R, ou clique na barra de ações</dd>)"
                         R"(<dt>Menu</dt><dd>Tab (ao soltar): Inventário, Livro, Mapa e Intenções · I inventário · B Livro · J intenções</dd>)"
                         R"(<dt>Distância da câmera</dt><dd>Segure Tab e aperte 1 (distante), 2 (média) ou 3 (próxima); a roda continua ajustando</dd>)"
                         R"(<dt>Minimapa</dt><dd>Sempre visível; M (ou o botão +) amplia e volta ao normal. O mapa completo fica na aba Mapa do Tab</dd>)"
                         R"(<dt>Câmera</dt><dd>Arraste o chão com o botão esquerdo: para os lados gira, para cima e para baixo inclina (ou use as setas), até olhar acima do horizonte. Mirando, o ponteiro trava no centro e o mouse gira a câmera. O “N” do minimapa recentraliza; a roda aproxima</dd></dl>)"
                         R"(<h3>Câmera</h3><p>{}{}</p>)"
                         R"(<p class="note">Velocidade do giro, valendo para o mouse, o arrasto e as setas. Mirando, o ponteiro trava no centro da tela e o mouse gira a câmera nos dois eixos. A escolha fica salva nas configurações do jogo.</p>)"
                         R"(<h3>Qualidade gráfica</h3><p>{}</p>)"
                         R"(<p class="note">Alta: oclusão de ambiente, anti-serrilhado e grama cheia. Média: menos grama e sem oclusão. Baixa: sem pós-processamento nem grama. A escolha fica salva nas configurações do jogo.</p>)"
                         R"(<div class="callout">Esta é uma demo de concepção. Nomes, números, regras e textos do Livro são provisórios e servem para discutir o documento-mestre, não para representar decisões aprovadas.</div>)"
                         R"(<h3>Créditos dos modelos 3D</h3><p class="note">Personagens “Kachujin G Rosales” e “Eve By J.Gonzales” e a animação “Unarmed Walk Forward”: Mixamo (Adobe). Cavalo “HORSE - Realistic 3D Model (DEMO FREE)” e leoa “LIONESS - Realistic 3D Model (DEMO FREE)”: WildMesh 3D (sketchfab.com/WildMesh_3D), licença CC BY-NC 4.0 — uso não comercial, com crédito ao autor. Os modelos foram reduzidos e comprimidos para esta demo.</p>)"
                         R"(<p>{} {}</p></div>)",
                         sens, btn("caminvert", "", in.camInvert ? "Vertical invertido" : "Inverter vertical", in.camInvert), quality,
                         btn("mute", "", in.muted ? "Ativar som" : "Silenciar"), btn("restart", "", "Nova trajetória"));
    }
    default: return "";
  }
}

PanelOutput GameUi::panels(const PanelInput& in) {
  panelInput_ = in;
  PanelOutput out;
  out.requests.swap(panelOut_.requests);
  out.local.swap(panelOut_.local);
  out.closed.swap(closedPanels_);
  const ClientWorld* w = in.world;
  if (!w || !w->self() || !w->world()) {
    syncMenubar();
    return out;
  }
  if (panel_ != Panel::None) {
    panelT_ -= in.dt;
    if (panelT_ <= 0) {
      panelT_ = 0.25;
      Rml::Element* el = byId("panel");
      // redesenha mantendo a rolagem (panels.js `draw`)
      float scroll = 0;
      if (Rml::Element* pb = el ? el->QuerySelector(".pb") : nullptr) scroll = pb->GetScrollTop();
      UiSystem::setInner(el, renderPanel());
      if (Rml::Element* pb = el ? el->QuerySelector(".pb") : nullptr) pb->SetScrollTop(scroll);
      if (panel_ == Panel::Map) {
        // mapa completo: a região inteira com os marcadores
        const proto::EntityState* me = w->me();
        const proto::PrivateState* P = w->self();
        const proto::WorldState* W = w->world();
        MapMarkers m;
        if (me) {
          m.playerX = me->x;
          m.playerZ = me->z;
          m.playerYaw = me->yaw;
          m.turbulent = static_cast<MapKind>(me->map) == MapKind::Turbulent;
        }
        m.camYaw = in.camYaw;
        m.time = W->time;
        for (const auto& e : w->entities())
          if (e.kind == proto::EntityKind::LootBag && (e.flags & proto::kFlagOwn) && static_cast<MapKind>(e.map) == MapKind::Region) m.ownBags.emplace_back(e.x, e.z);
        m.portal = W->portal;
        m.portalX = W->portalX;
        m.portalZ = W->portalZ;
        m.mount = P->mountPresent && !P->mounted;
        m.mountX = P->mountX;
        m.mountZ = P->mountZ;
        if (full_.width() != 640) full_ = Raster(640, 640);
        map_.drawFull(full_, m);
        ui_.render().setDynamicTexture("dyn:fullmap", 640, 640, full_.pixels());
      }
    }
  }
  syncBook(in.dt);
  syncMenubar();
  return out;
}

}  // namespace rpg::client
