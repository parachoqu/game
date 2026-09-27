#include "client/ui/Screens.h"

#include <algorithm>

#include "client/Presentation.h"
#include "client/ui/Hud.h"
#include "client/ui/Localization.h"

namespace rpg::client {

const std::vector<std::string> kSuggestedNames = {"Maren", "Ilo", "Tessaly", "Ravi", "Oriane", "Kael", "Nadja", "Bento", "Suri", "Aurel"};
const std::vector<std::string> kCharacterModels = {"paladina", "kachujin", "eve"};

namespace {

const Rgba kBone = Rgba::hex(0xece4d2), kMuted = Rgba::hex(0xa8a193), kBronze = Rgba::hex(0xa5844f), kGlaze = Rgba::hex(0x46b3a8);

bool inside(const InputState& in, float x, float y, float w, float h) {
  const auto mx = static_cast<float>(in.mouseX), my = static_cast<float>(in.mouseY);
  return mx >= x && mx <= x + w && my >= y && my <= y + h;
}

// Cartão selecionável (label.card dos formulários).
bool card(UiBatch& ui, const UiFonts& f, const InputState& in, float x, float y, float w, float h, const std::string& title,
          const std::string& desc, bool selected, const Rgba* swatchA = nullptr, const Rgba* swatchB = nullptr) {
  const bool hover = inside(in, x, y, w, h);
  ui.rect(x, y, w, h, selected ? Rgba{36, 52, 62, 245} : hover ? Rgba{28, 40, 50, 240} : Rgba{20, 30, 39, 235});
  ui.frame(x, y, w, h, selected ? 2.0f : 1.0f, selected ? kGlaze : Rgba{58, 74, 85, 255});
  float tx = x + 10;
  if (swatchA && swatchB) {
    ui.rect(tx, y + 10, 9, 18, *swatchA);
    ui.rect(tx + 9, y + 10, 9, 18, *swatchB);
    tx += 26;
  }
  ui.text(f.bold, 13, tx, y + 11, title, kBone);
  ui.paragraph(f.body, 11, x + 10, y + 34, w - 20, desc, kMuted, 1.35f);
  return hover && in.leftPressed;
}

}  // namespace

void Screens::loading(UiBatch& ui, const UiFonts& f, const InputState& in, const Localization& L, double progress, std::string_view text) {
  const float sw = static_cast<float>(in.width), sh = static_cast<float>(in.height);
  ui.rect(0, 0, sw, sh, Rgba{9, 14, 19, 255});
  ui.text(f.body, 12, sw / 2, sh / 2 - 90, L.ui("title.eyebrow"), kBronze, Align::Center);
  ui.text(f.display, 44, sw / 2, sh / 2 - 66, L.ui("title.name"), kBone, Align::Center);
  ui.text(f.body, 14, sw / 2, sh / 2 - 8, L.ui("loading.sub"), kMuted, Align::Center);
  ui.rect(sw / 2 - 200, sh / 2 + 24, 400, 8, Rgba{7, 11, 15, 255});
  ui.rect(sw / 2 - 200, sh / 2 + 24, 400 * static_cast<float>(std::clamp(progress, 0.0, 1.0)), 8, kGlaze);
  ui.text(f.body, 12, sw / 2, sh / 2 + 42, text, kBone, Align::Center);
}

Screens::Result Screens::title(UiBatch& ui, const UiFonts& f, const InputState& in, const Localization& L, bool canContinue, double fade) {
  const float sw = static_cast<float>(in.width), sh = static_cast<float>(in.height);
  const float a = static_cast<float>(std::clamp(fade, 0.0, 1.0));
  ui.rect(0, 0, sw, sh, Rgba{5, 9, 14, static_cast<std::uint8_t>(70 * a)});
  float y = sh * 0.24f;
  ui.textShadow(f.body, 12, sw / 2, y, L.ui("title.eyebrow"), kBronze.withAlpha(a), Align::Center);
  ui.textShadow(f.display, 64, sw / 2, y + 20, L.ui("title.name"), kBone.withAlpha(a), Align::Center);
  y += 104;
  const float pw = std::min(560.0f, sw - 40);
  y += ui.paragraph(f.body, 16, sw / 2 - pw / 2, y, pw, L.ui("title.promise"), kBone.withAlpha(a)) + 18;
  const float cw = std::min(180.0f, (sw - 40) / 3);
  for (int i = 0; i < 3; ++i) {
    const float cx = sw / 2 + (static_cast<float>(i) - 1.0f) * (cw + 8) - cw / 2;
    ui.rect(cx, y, cw, 48, Rgba{14, 21, 28, static_cast<std::uint8_t>(200 * a)});
    ui.frame(cx, y, cw, 48, 1, Rgba{58, 74, 85, static_cast<std::uint8_t>(255 * a)});
    const std::string k = "title.triad." + std::to_string(i + 1);
    ui.text(f.bold, 13, cx + cw / 2, y + 8, L.ui(k), kBone.withAlpha(a), Align::Center);
    ui.text(f.body, 11, cx + cw / 2, y + 27, L.ui(k + "b"), kMuted.withAlpha(a), Align::Center);
  }
  y += 72;
  Result r = Result::None;
  const float bw = 220;
  if (canContinue) {
    if (drawButton(ui, f, in, sw / 2 - bw - 6, y, bw, 42, L.ui("title.new"), true)) r = Result::NewGame;
    if (drawButton(ui, f, in, sw / 2 + 6, y, bw, 42, L.ui("title.continue"))) r = Result::Continue;
  } else if (drawButton(ui, f, in, sw / 2 - bw / 2, y, bw, 42, L.ui("title.new"), true)) {
    r = Result::NewGame;
  }
  if (in.hit(Key::Enter)) r = Result::NewGame;
  y += 62;
  y += ui.paragraph(f.body, 11, sw / 2 - pw / 2, y, pw, L.ui("title.fine"), kMuted.withAlpha(a)) + 4;
  ui.paragraph(f.body, 11, sw / 2 - pw / 2, y, pw, L.ui("title.credits"), kMuted.withAlpha(a));
  return r;
}

Screens::Result Screens::create(UiBatch& ui, const UiFonts& f, const InputState& in, const Localization& L, const GameData& data,
                                const Presentation& look, CreationForm& form) {
  const float sw = static_cast<float>(in.width), sh = static_cast<float>(in.height);
  ui.rect(0, 0, sw, sh, Rgba{5, 9, 14, 150});
  const float pw = std::min(960.0f, sw - 32);
  const float x = sw / 2 - pw / 2;
  float y = std::max(16.0f, sh / 2 - 330);
  drawPanel(ui, x, y, pw, std::min(660.0f, sh - 32));
  float cy = y + 18;
  ui.text(f.body, 12, x + 24, cy, L.ui("create.eyebrow"), kBronze);
  cy += 20;
  ui.text(f.display, 26, x + 24, cy, L.ui("create.question"), kBone);
  cy += 44;

  // nome (campo de texto sempre com foco: letras digitadas entram, Backspace apaga)
  ui.text(f.bold, 13, x + 24, cy, L.ui("create.name"), kBone);
  if (!in.text.empty() && form.name.size() < 44) form.name += in.text;
  if (in.hit(Key::Backspace) && !form.name.empty()) {
    std::size_t cut = form.name.size() - 1;
    while (cut > 0 && (static_cast<unsigned char>(form.name[cut]) & 0xC0) == 0x80) --cut;
    form.name.erase(cut);
  }
  ui.rect(x + 90, cy - 6, 300, 28, Rgba{7, 11, 15, 255});
  ui.frame(x + 90, cy - 6, 300, 28, 1, kGlaze);
  const float tw = ui.text(f.body, 14, x + 98, cy - 1, form.name, kBone);
  ui.rect(x + 99 + tw, cy - 1, 2, 17, kBone);
  cy += 40;

  const auto section = [&](std::string_view key, std::string_view hint) {
    const float w1 = ui.text(f.bold, 13, x + 24, cy, L.ui(key), kBone);
    ui.text(f.body, 11, x + 32 + w1, cy + 2, L.ui(hint), kMuted);
    cy += 22;
  };
  const float gap = 8;
  const auto grid = [&](std::size_t n, std::size_t perRow, float h, auto&& drawCard) {
    const float cw = (pw - 48 - gap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow);
    for (std::size_t i = 0; i < n; ++i) {
      const float cx = x + 24 + static_cast<float>(i % perRow) * (cw + gap);
      const float cyy = cy + static_cast<float>(i / perRow) * (h + gap);
      drawCard(i, cx, cyy, cw, h);
    }
    cy += static_cast<float>((n + perRow - 1) / perRow) * (h + gap) + 8;
  };

  section("create.models", "create.models.hint");
  grid(kCharacterModels.size(), 3, 64, [&](std::size_t i, float cx, float cyy, float cw, float h) {
    const std::string& m = kCharacterModels[i];
    if (card(ui, f, in, cx, cyy, cw, h, L.ui("model." + m + ".name"), L.ui("model." + m + ".desc"), form.model == i)) form.model = i;
  });
  section("create.origins", "create.origins.hint");
  grid(data.origins.size(), 4, 70, [&](std::size_t i, float cx, float cyy, float cw, float h) {
    const OriginId id{static_cast<std::uint16_t>(i)};
    const OriginLook& o = look.origin(data.origins.key(id));
    const Rgba a = Rgba::hex(o.skin), b = Rgba::hex(o.cloth);
    if (card(ui, f, in, cx, cyy, cw, h, L.origin(id), L.originHint(id), form.origin == i, &a, &b)) form.origin = i;
  });
  section("create.starts", "create.starts.hint");
  grid(data.starts.size(), 4, 82, [&](std::size_t i, float cx, float cyy, float cw, float h) {
    const StartId id{static_cast<std::uint16_t>(i)};
    if (card(ui, f, in, cx, cyy, cw, h, L.start(id), L.startDesc(id), form.start == i)) form.start = i;
  });

  Result r = Result::None;
  const float by = std::min(cy + 4, y + std::min(660.0f, sh - 32) - 56);
  if (drawButton(ui, f, in, x + pw - 24 - 200 - 12 - 120, by, 120, 40, L.ui("create.back"))) r = Result::Back;
  if (drawButton(ui, f, in, x + pw - 24 - 200, by, 200, 40, L.ui("create.go"), true) || in.hit(Key::Enter)) r = Result::Start;
  if (in.hit(Key::Escape)) r = Result::Back;
  return r;
}

void Screens::notice(UiBatch& ui, const UiFonts& f, const InputState& in, std::string_view text) {
  const float sw = static_cast<float>(in.width), sh = static_cast<float>(in.height);
  const float w = std::min(520.0f, sw - 40);
  drawPanel(ui, sw / 2 - w / 2, sh / 2 - 30, w, 60);
  ui.text(f.body, 15, sw / 2, sh / 2 - 9, text, kBone, Align::Center);
}

}  // namespace rpg::client
