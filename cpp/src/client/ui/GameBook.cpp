// O Livro (tecla B): ui/bookui.js. Memória, expressão e legado: capítulos por dia, feitos,
// descobertas, legado, trilhas e perfil; o jogador escolhe o que exibir e escreve anotações.
#include <algorithm>
#include <format>
#include <map>

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Elements/ElementFormControlTextArea.h>

#include "client/ClientWorld.h"
#include "client/fx/FxState.h"
#include "client/ui/GameUi.h"
#include "client/ui/Localization.h"
#include "core/GameClock.h"

namespace rpg::client {

namespace proto = protocol;

namespace {

std::string esc(std::string_view s) { return UiSystem::escape(s); }

struct Tab {
  const char *id, *name, *lede;
};
const Tab kTabs[] = {
    {"historia", "História", "Capítulos da trajetória, na ordem em que aconteceram."},
    {"feitos", "Feitos", "Marcos de trabalho, combate, cooperação — e derrotas."},
    {"descobertas", "Descobertas", "Lugares, sinais e observações. Coordenadas não são publicadas."},
    {"legado", "Legado", "Participação em acontecimentos coletivos e títulos reconhecidos."},
    {"trilhas", "Trilhas & Estilos", "Sinergias e orientações do Capítulo 23: você é o que você equipa e pratica."},
    {"perfil", "Perfil", "Origem, conhecimentos, ofícios e o que você escolhe mostrar."},
};

struct PathCard {
  const char *name, *focus, *desc;
  std::vector<const char*> weapons, armors, skills;
};
const PathCard kPaths[] = {
    {"Vanguarda de Ferro", "Linha de Frente · Absorção & Controle", "Combate corpo a corpo pesado, absorção de golpes e quebra de formação adversária.",
     {"espada", "martelo_guerra"}, {"armadura_pesada", "armadura_ferro"}, {"Investida", "Impacto Sísmico", "Giro Cortante"}},
    {"Lutador Ágil", "Velocidade · Pressão Rápida & Esquiva", "Sequências fulminantes de socos ou estocadas duplas, aproveitando qualquer brecha com mobilidade elevada.",
     {"manoplas", "adagas"}, {"armadura_couro", "roupa_viajante"}, {"Sequência Veloz", "Golpe Triplo", "Passo Sombrio"}},
    {"Sentinela dos Desfiladeiros", "Média Distância · Guarda & Estabilidade", "Mantém adversários a distância segura com estocadas precisas ou esmagamento tático de postura.",
     {"lanca", "maca"}, {"armadura_ferro", "armadura_couro"}, {"Estocada Penetrante", "Arremesso de Lança", "Esmagar"}},
    {"Batedor da Fronteira", "Precisão · Distância & Emboscada", "Disparos cirúrgicos à longa distância e recuo rápido em terreno acidentado ou florestas densas.",
     {"arco_longo", "besta", "machado_guerra"}, {"armadura_couro", "roupa_viajante"}, {"Tiro Preciso", "Chuva de Flechas", "Disparo Perfurante"}},
    {"Erudito do Gelo & Vento", "Magia de Controle · Lentidão & Afastamento", "Congela avanços inimigos com frio cortante e dissipa grupos de ameaças com rajadas de vendaval.",
     {"cajado_gelo", "orbe_vento"}, {"roupa_viajante", "armadura_couro"}, {"Seta Gélida", "Prisão de Gelo", "Lâmina de Vento", "Rajada Repulsora"}},
    {"Piromante de Batalha", "Magia Ofensiva · Dano Contínuo & Queima", "Incendeia o campo de batalha com esferas de fogo e pilares abrasadores, forçando o recuo adversário.",
     {"cajado_fogo", "foice_combate"}, {"roupa_viajante", "armadura_ferro"}, {"Bola de Fogo", "Pilar de Chamas", "Ceifa Ampla"}},
    {"Xamã Metamorfo", "Híbrido Selvagem · Enraizamento & Forma Bestial", "Invoca raízes e vinhas do solo, regenera vigor vital e transforma-se na forma de um urso implacável.",
     {"totem_natureza", "tomo_vital"}, {"armadura_couro", "roupa_viajante"}, {"Raízes Aprisionadoras", "Regeneração Vital", "Grito Feroz"}},
    {"Guardião Sagrado", "Proteção & Luz · Escudos & Restauração", "Conjura barreiras luminosas que absorvem ferimentos, purifica aliados e dispersa sombras corrompidas.",
     {"simbolo_sagrado", "foco_maldicao"}, {"armadura_pesada", "armadura_ferro"}, {"Barreira Radiante", "Luz Curativa", "Raio Entrópico"}},
};

const char* kRoman[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII", "IX", "X", "XI", "XII"};

std::string whenLabel(double time) {
  const ClockTime c = clockAt(time, ClockConfig{});
  return std::format("Dia {} · {:02d}h", c.day, static_cast<int>(std::floor(c.hour)));
}

}  // namespace

void GameUi::openBook() {
  if (panel_ != Panel::None) closePanel();
  bookOpen_ = true;
  bookWriting_ = false;
  lastTab_ = "book";
  UiSystem::setHidden(byId("book"), false);
  bookDirty_ = true;
}

void GameUi::closeBook() {
  if (!bookOpen_) return;
  bookOpen_ = false;
  UiSystem::setHidden(byId("book"), true);
  UiSystem::setInner(byId("book"), "");
}

void GameUi::bookAction(const std::string& act, const std::string& arg) {
  bookDirty_ = true;
  if (act == "close") closeBook();
  else if (act == "tab") bookTab_ = arg, bookWriting_ = false;
  else if (act == "public") bookPublic_ = !bookPublic_;
  else if (act == "write") bookWriting_ = true;
  else if (act == "cancel") bookWriting_ = false;
  else if (act == "vis") {
    const int id = std::atoi(arg.c_str());
    for (const auto& e : panelInput_.fx->book)
      if (e.id == id) panelOut_.requests.push_back(proto::ReqBookSetPublic{id, !e.isPublic});
  } else if (act == "title") {
    const auto& shown = panelInput_.fx->shownTitle;
    panelOut_.requests.push_back(proto::ReqShowTitle{shown && *shown == arg ? std::nullopt : std::optional<std::string>(arg)});
  } else if (act == "save") {
    if (auto* ta = dynamic_cast<Rml::ElementFormControlTextArea*>(byId("bk-note"))) {
      std::string t = ta->GetValue();
      t.erase(0, t.find_first_not_of(" \t\n\r"));
      t.erase(t.find_last_not_of(" \t\n\r") + 1);
      if (!t.empty()) panelOut_.requests.push_back(proto::ReqBookNote{t.substr(0, 400)});
    }
    bookWriting_ = false;
  }
}

std::string GameUi::renderBook() {
  const ClientWorld& w = *panelInput_.world;
  const FxState& fx = *panelInput_.fx;
  const proto::PrivateState& P = *w.self();
  const proto::EntityState* me = w.me();
  const std::string name = me ? me->name : std::string{};
  const OriginId origin = me ? data_.origins.find(me->origin) : OriginId{};
  const bool pub = bookPublic_;
  const auto visible = [&](const proto::BookEntry& e) { return !pub || e.isPublic; };
  const auto catOf = [](const proto::BookEntry& e) { return std::string(proto::bookCatKey(e.cat)); };

  const auto entryHtml = [&](const proto::BookEntry& e) -> std::string {
    if (!visible(e)) return {};
    std::string meta;
    if (e.count > 1) meta += std::format(R"(<span class="count">marco agregado · {}</span>)", e.count);
    if (!pub) {
      if (e.personal) meta += R"(<span class="count">anotação pessoal — não é registro do mundo</span>)";
      else meta += std::format(R"(<button class="vis{}" data-act="bk:vis" data-arg="{}">{}</button>)", e.isPublic ? " pub" : "", e.id, e.isPublic ? "Público" : "Pessoal");
    }
    return std::format(R"(<div class="entry{}"><span class="when">{}{}</span><h4>{}</h4><p>{}</p><div class="meta">{}</div></div>)", e.personal ? " personal" : "",
                       whenLabel(e.time), e.updatedTime ? " · atualizado " + whenLabel(*e.updatedTime) : "", esc(L_.bookTitle(e)), esc(L_.bookText(e)), meta);
  };

  const Tab* tab = &kTabs[0];
  for (const Tab& t : kTabs)
    if (bookTab_ == t.id) tab = &t;
  const std::string empty = std::format(R"(<p class="empty">{}</p>)", pub ? "Nada exibido publicamente nesta parte. Marque registros como “Público” para que apareçam aqui."
                                                                          : "Ainda não há registros aqui. O Livro escreve apenas o que realmente aconteceu com esta personagem.");
  std::string body;
  const std::string id = tab->id;
  if (id == "historia") {
    // book.js `chapters`: um capítulo por dia, nomeado pelo domínio que mais aparece
    std::map<int, std::vector<const proto::BookEntry*>> days;
    for (const auto& e : fx.book) days[e.day].push_back(&e);
    for (const auto& [day, list] : days) {
      std::map<std::string, int> tally;
      std::vector<std::string> order;
      for (const auto* e : list)
        if (e->domain != proto::BookDomain::None) {
          const std::string d(proto::bookDomainKey(e->domain));
          if (!tally.contains(d)) order.push_back(d);
          tally[d] += e->count;
        }
      std::string top;
      int best = 0;
      for (const auto& d : order)
        if (tally[d] > best) best = tally[d], top = d;
      std::string items;
      for (const auto* e : list) items += entryHtml(*e);
      if (items.empty()) continue;
      const std::string roman = day >= 1 && day <= 12 ? kRoman[day - 1] : std::to_string(day);
      body += std::format(R"(<div class="chapter">Capítulo {} — {}</div>{})", roman, esc(L_.bookChapter(top.empty() ? "none" : top)), items);
    }
    if (body.empty()) body = empty;
    if (!pub) {
      body += bookWriting_
                  ? R"(<div class="book-note" style="margin-top: 18px"><span class="when">Anotação pessoal (fica marcada como sua, separada dos registros do mundo)</span><textarea id="bk-note" maxlength="400"></textarea><p><button class="bk-btn" data-act="bk:save">Guardar anotação</button> <button class="bk-btn" data-act="bk:cancel">Cancelar</button></p></div>)"
                  : R"(<p style="margin-top: 18px"><button class="bk-btn" data-act="bk:write">Escrever anotação pessoal</button></p>)";
    }
  } else if (id == "trilhas") {
    const std::string curW = P.weapon ? data_.items.key(ItemId{P.weapon->item}) : std::string{};
    const std::string curA = P.armor ? data_.items.key(ItemId{P.armor->item}) : std::string{};
    std::string skills;
    if (P.weapon && data_.items[ItemId{P.weapon->item}].family.valid())
      for (const SkillId s : data_.weapons[data_.items[ItemId{P.weapon->item}].family].skills)
        if (s.valid()) skills += (skills.empty() ? "" : ", ") + L_.skill(s);
    const auto itemName = [&](const char* k) {
      const ItemId i = data_.items.find(k);
      return i.valid() ? L_.item(i) : std::string(k);
    };
    std::string cards;
    for (const PathCard& p : kPaths) {
      const bool active = std::any_of(p.weapons.begin(), p.weapons.end(), [&](const char* x) { return curW == x; }) ||
                          std::any_of(p.armors.begin(), p.armors.end(), [&](const char* x) { return curA == x; });
      std::string ws, as, ss;
      for (const char* x : p.weapons) ws += (ws.empty() ? "" : ", ") + itemName(x);
      for (const char* x : p.armors) as += (as.empty() ? "" : ", ") + itemName(x);
      for (const char* x : p.skills) ss += (ss.empty() ? "" : ", ") + std::string(x);
      cards += std::format(R"(<div class="path-card{}"><h4><span>{}</span> {}</h4><p><b>Foco:</b> {}</p><p>{}</p><div class="specs"><div><b>Armas típicas:</b> {}</div><div><b>Vestimenta:</b> {}</div><div><b>Técnicas sinérgicas:</b> {}</div></div></div>)",
                           active ? " active" : "", esc(p.name), active ? R"(<span class="tag">Afinidade</span>)" : "", esc(p.focus), esc(p.desc), esc(ws), esc(as), esc(ss));
    }
    body = std::format(
        R"(<div class="paths-intro"><strong>Capítulo 23 do Documento-Mestre:</strong> Não há classes estanques. Sua personagem é definida pelo que carrega, forja, veste e aprende no mundo. Trocar de estilo requer apenas preparar novos equipamentos e praticar suas técnicas.</div>)"
        R"(<div class="paths-hero"><h4>Configuração Atual da Personagem</h4><p>Arma: <b>{}</b> · Armadura: <b>{}</b> · Técnicas equipadas: <b>{}</b></p></div><div class="paths-grid">{}</div>)",
        esc(P.weapon ? L_.item(ItemId{P.weapon->item}) : "Nenhuma arma empunhada"), esc(P.armor ? L_.item(ItemId{P.armor->item}) : "Traje básico"),
        esc(skills.empty() ? "Nenhuma" : skills), cards);
  } else if (id == "perfil") {
    const auto stat = [&](const std::string& k) {
      const auto it = P.stats.find(k);
      return it == P.stats.end() ? 0.0 : it->second;
    };
    std::vector<std::string> profs;
    if (stat("crafted") > 0) profs.push_back(std::format("Artesanato — {} fabricações", Localization::number(stat("crafted"))));
    const double gathered = stat("gather_minerio") + stat("gather_madeira") + stat("gather_erva") + stat("gather_cristal");
    if (gathered > 0) profs.push_back(std::format("Coleta — {} recursos", Localization::number(gathered)));
    double sold = 0;
    for (const auto& [k, v] : P.stats)
      if (k.rfind("sold_", 0) == 0) sold += v;
    if (sold > 0) profs.push_back(std::format("Comércio — {} vendas, {} moedas", Localization::number(sold), Localization::number(stat("earned"))));
    if (stat("kills") > 0) profs.push_back(std::format("Combate — {} confrontos vencidos", Localization::number(stat("kills"))));
    if (stat("incursoes") > 0)
      profs.push_back(std::format("Incursões — {} ({} com extração)", Localization::number(stat("incursoes")), Localization::number(stat("extracoes"))));
    std::string profHtml;
    for (const auto& s : profs) profHtml += (profHtml.empty() ? "" : "<br/>") + esc(s);
    std::string titleHtml;
    if (fx.titles.empty()) {
      titleHtml = "Nenhum título ainda.";
    } else if (pub) {
      titleHtml = "—";
      for (const auto& t : fx.titles)
        if (fx.shownTitle && *fx.shownTitle == t.id) titleHtml = esc(L_.bookTitleName(t));
    } else {
      std::string btns;
      for (const auto& t : fx.titles)
        btns += std::format(R"(<button class="titlebtn{}" data-act="bk:title" data-arg="{}" title="{}">{}</button>)", fx.shownTitle && *fx.shownTitle == t.id ? " on" : "",
                            esc(t.id), esc(L_.bookTitleWhy(t)), esc(L_.bookTitleName(t)));
      titleHtml = std::format(R"(<div class="titles">{}</div><p class="empty" style="font-size: 13px; margin-top: 6px">Títulos são expressivos: não concedem poder.</p>)", btns);
    }
    std::string trainings;
    for (const auto& t : P.trainings) {
      const TrainingId tid = data_.trainings.find(t);
      trainings += (trainings.empty() ? "" : ", ") + (tid.valid() ? L_.training(tid) : t);
    }
    int shownCount = 0;
    for (const auto& e : fx.book) shownCount += e.isPublic ? 1 : 0;
    body = std::format(R"(<div class="profile"><dl><dt>Nome</dt><dd>{}</dd><dt>Origem</dt><dd>{} — {} A origem não define ofício.</dd>{}<dt>Ofícios</dt><dd>{}</dd>{}<dt>Título exibido</dt><dd>{}</dd>{}</dl></div>)",
                       esc(name), esc(origin.valid() ? L_.origin(origin) : ""), esc(origin.valid() ? L_.originHint(origin) : ""),
                       pub ? "" : std::format("<dt>Conhecimentos</dt><dd>{}</dd>", esc(trainings)), profHtml.empty() ? "Ainda nenhum ofício reconhecido." : profHtml,
                       pub ? "" : std::format("<dt>Montaria</dt><dd>{}</dd>", P.mount ? esc(L_.item(ItemId{P.mount->item})) : "—"), titleHtml,
                       pub ? std::format("<dt>Marcos exibidos</dt><dd>{}</dd>", shownCount) : "");
  } else {
    for (const auto& e : fx.book)
      if (catOf(e) == id) body += entryHtml(e);
    if (body.empty()) body = empty;
  }
  const std::string right = std::format(R"({}<h3>{}</h3><p class="lede">{}</p>{})",
                                        pub ? R"(<div class="pubbar">Visão pública — o que outra pessoa vê ao inspecionar sua biografia. A inspeção de equipamento é separada.</div>)" : "",
                                        tab->name, tab->lede, body);

  // contagens das abas
  const auto n = [&](const char* cat) {
    int c = 0;
    for (const auto& e : fx.book) c += catOf(e) == cat && visible(e) ? 1 : 0;
    return std::to_string(c);
  };
  int all = 0;
  for (const auto& e : fx.book) all += visible(e) ? 1 : 0;
  std::string tabs;
  for (const Tab& t : kTabs) {
    const std::string tid = t.id;
    const std::string count = tid == "historia" ? std::to_string(all) : tid == "trilhas" || tid == "perfil" ? "" : n(t.id);
    tabs += std::format(R"(<button class="tab{}" data-act="bk:tab" data-arg="{}">{}<small>{}</small></button>)", bookTab_ == tid ? " on" : "", tid, t.name, count);
  }
  std::string title;
  for (const auto& t : fx.titles)
    if (fx.shownTitle && *fx.shownTitle == t.id) title = " · " + esc(L_.bookTitleName(t));
  return std::format(R"(<i class="bracket tl"></i><i class="bracket tr"></i><i class="bracket bl"></i><i class="bracket br"></i><i class="spine"></i>)"
                     R"(<div class="bk-left"><p class="eyebrow">O Livro de</p><h2>{}</h2><p class="sub">{}{}</p><div class="band"></div><div class="tabs">{}</div>)"
                     R"(<div class="tools"><button class="bk-btn{}" data-act="bk:public">{}</button><button class="bk-btn" data-act="bk:close">Fechar o Livro <kbd>Esc</kbd></button></div></div>)"
                     R"(<div class="bk-right">{}</div>)",
                     esc(name), esc(origin.valid() ? L_.origin(origin) : ""), title, tabs, pub ? " on" : "", pub ? "Voltar ao Livro pessoal" : "Ver como os outros veem", right);
}

void GameUi::syncBook(double dt) {
  if (!bookOpen_ || !panelInput_.world || !panelInput_.world->self()) return;
  bookT_ -= dt;
  if (!bookDirty_ && bookT_ > 0) return;
  bookT_ = 0.5;
  const bool tabChange = bookDirty_;
  bookDirty_ = false;
  Rml::Element* el = byId("book");
  float scroll = 0;
  if (Rml::Element* r = el ? el->QuerySelector(".bk-right") : nullptr) scroll = r->GetScrollTop();
  // a anotação em andamento não pode sumir no redesenho
  std::string draft;
  if (auto* ta = dynamic_cast<Rml::ElementFormControlTextArea*>(byId("bk-note"))) draft = ta->GetValue();
  const bool hadNote = byId("bk-note") != nullptr;
  UiSystem::setInner(el, renderBook());
  if (Rml::Element* r = el ? el->QuerySelector(".bk-right") : nullptr) r->SetScrollTop(tabChange && bookTab_ != lastBookTab_ ? 0 : scroll);
  lastBookTab_ = bookTab_;
  if (auto* ta = dynamic_cast<Rml::ElementFormControlTextArea*>(byId("bk-note"))) {
    if (hadNote) ta->SetValue(draft);
    else ta->Focus();
  }
}

}  // namespace rpg::client
