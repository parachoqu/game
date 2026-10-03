#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "client/audio/Synth.h"
#include "client/game/Settings.h"

using rpg::client::audio::Ambience;
using rpg::client::audio::Biquad;
using rpg::client::audio::FilterType;
using rpg::client::audio::Synth;
using rpg::protocol::SoundId;

namespace {

double peakOf(const std::vector<float>& v) {
  double p = 0;
  for (const float s : v) p = std::max(p, static_cast<double>(std::abs(s)));
  return p;
}

std::vector<float> renderSeconds(Synth& s, double seconds, int channels = 1) {
  std::vector<float> out(static_cast<std::size_t>(seconds * s.sampleRate()) * static_cast<std::size_t>(channels));
  s.render(out, channels);
  return out;
}

// Ganho de um biquad para um seno de `freq` (regime permanente).
double gainAt(FilterType type, double cutoff, double q, double freq) {
  Biquad b;
  b.set(type, cutoff, q, 48000);
  double peak = 0;
  for (int i = 0; i < 48000; ++i) {
    const double y = b.process(std::sin(6.283185307179586 * freq * i / 48000));
    if (i > 24000) peak = std::max(peak, std::abs(y));
  }
  return peak;
}

}  // namespace

TEST_CASE("Síntese: silêncio sem sons; um golpe soa, fica abaixo de 1 e termina", "[client][audio]") {
  Synth s(48000, 3);
  CHECK(peakOf(renderSeconds(s, 0.1)) == 0);
  s.sfx(SoundId::Hit);
  CHECK(s.voices() == 2);  // tone + noise, como em audio.js
  const auto a = renderSeconds(s, 0.25, 2);
  CHECK(peakOf(a) > 0.02);
  CHECK(peakOf(a) <= 1.0);
  for (std::size_t i = 0; i + 1 < a.size(); i += 2) REQUIRE(a[i] == a[i + 1]);  // o mesmo sinal nos dois canais
  CHECK(s.voices() == 0);
  CHECK(peakOf(renderSeconds(s, 0.05)) == 0);
}

TEST_CASE("Síntese: todos os 23 efeitos produzem som; mudo não agenda nada", "[client][audio]") {
  for (int i = 0; i < static_cast<int>(SoundId::Count); ++i) {
    Synth s(48000, 5);
    s.sfx(static_cast<SoundId>(i));
    INFO("som " << i);
    CHECK(s.voices() >= 1);
    CHECK(peakOf(renderSeconds(s, 2.5)) > 0.001);
  }
  Synth m(48000, 5);
  m.setMuted(true);
  m.sfx(SoundId::Event);
  CHECK(m.voices() == 0);
  CHECK(peakOf(renderSeconds(m, 0.2)) == 0);
}

TEST_CASE("Síntese: o atraso de `delay` e a rampa da ambiência seguem o WebAudio", "[client][audio]") {
  Synth s(48000, 9);
  s.tone(440, 0.1, rpg::client::audio::Wave::Sine, 0.2, 0, 0.5);  // começa em 0,5 s
  CHECK(peakOf(renderSeconds(s, 0.45)) == 0);
  CHECK(peakOf(renderSeconds(s, 0.2)) > 0.05);

  Synth a(48000, 9);
  a.setAmbience(Ambience::World);
  const double early = peakOf(renderSeconds(a, 0.2));
  renderSeconds(a, 1.4);
  const double late = peakOf(renderSeconds(a, 0.5));
  CHECK(late > early * 5);  // 0,0001 → 0,8 em 1,5 s
  a.setAmbience(Ambience::Turbulent);
  renderSeconds(a, 1.6);
  CHECK(peakOf(renderSeconds(a, 0.5)) > 0.001);
  a.setAmbience(Ambience::None);
  CHECK(peakOf(renderSeconds(a, 0.1)) == 0);
}

TEST_CASE("Biquad: passa-baixa, passa-alta e passa-banda nas fórmulas do WebAudio", "[client][audio]") {
  CHECK(gainAt(FilterType::Lowpass, 380, 1, 50) == Catch::Approx(1).margin(0.05));
  CHECK(gainAt(FilterType::Lowpass, 380, 1, 4000) < 0.02);
  CHECK(gainAt(FilterType::Highpass, 3000, 0.6, 200) < 0.01);
  CHECK(gainAt(FilterType::Highpass, 3000, 0.6, 12000) == Catch::Approx(1).margin(0.05));
  CHECK(gainAt(FilterType::Bandpass, 900, 1.2, 900) == Catch::Approx(1).margin(0.02));  // pico de 0 dB
  CHECK(gainAt(FilterType::Bandpass, 900, 1.2, 9000) < 0.2);
}

TEST_CASE("Preferências: gravam e voltam; arquivo ausente ou quebrado usa os padrões", "[client]") {
  std::random_device rd;
  const auto dir = std::filesystem::temp_directory_path() / ("rpg-settings-" + std::to_string(rd()));
  const auto file = dir / "settings.json";
  rpg::client::Settings d = rpg::client::Settings::load(file);
  CHECK(d.quality == "alta");
  CHECK_FALSE(d.qualityChosen);
  CHECK_FALSE(d.last);

  rpg::client::Settings s;
  s.camera.sens = rpg::client::CamSensitivity::Alta;
  s.camera.invert = true;
  s.quality = "baixa";
  s.qualityChosen = true;
  s.muted = true;
  s.last = rpg::client::Settings::Profile{"Iara", "humano", "artesao", "eve"};
  REQUIRE(s.save(file));
  const auto r = rpg::client::Settings::load(file);
  CHECK(r.camera.sens == rpg::client::CamSensitivity::Alta);
  CHECK(r.camera.invert);
  CHECK(r.quality == "baixa");
  CHECK(r.qualityChosen);
  CHECK(r.muted);
  REQUIRE(r.last);
  CHECK(r.last->name == "Iara");
  CHECK(r.last->model == "eve");

  { std::ofstream(file) << "nada disso é json"; }
  CHECK(rpg::client::Settings::load(file).quality == "alta");
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}
