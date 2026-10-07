#pragma once
// Efeitos sonoros sintetizados (engine/audio.js), sem arquivos de som: o mesmo grafo que a demo montava
// no WebAudio, calculado amostra a amostra.
//
//   tone   oscilador (seno, quadrada, triângulo, dente de serra) com deslize exponencial de frequência
//   noise  ruído branco por um biquad (passa-banda, passa-baixa, passa-alta) com deslize do corte
//   env    envelope exponencial: 0,0001 → pico em `a` s → 0,0001 em `d` s (exponentialRampToValueAtTime)
//
// Os 23 efeitos de `sfx` e as duas ambiências de `setAmbience` ('world': vento; 'turb': zumbido) usam
// os mesmos números da demo. Sem plataforma: AudioDevice (SDL) chama render() na thread de áudio, e os
// testes chamam direto.
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/Rng.h"
#include "core/protocol/Messages.h"

namespace rpg::client::audio {

enum class Wave : std::uint8_t { Sine, Square, Triangle, Sawtooth };
enum class FilterType : std::uint8_t { Bandpass, Lowpass, Highpass };
enum class Ambience : std::uint8_t { None, World, Turbulent };

// Biquad do WebAudio (BiquadFilterNode, fórmulas do Audio EQ Cookbook). No passa-baixa e no passa-alta o
// Q é em dB, como no WebAudio; no passa-banda é linear.
struct Biquad {
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
  double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  void set(FilterType type, double freq, double q, double sampleRate);
  double process(double x) {
    const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
    x2 = x1;
    x1 = x;
    y2 = y1;
    y1 = y;
    return y;
  }
};

class Synth {
 public:
  explicit Synth(double sampleRate = 48000, std::uint64_t seed = 1);

  double sampleRate() const { return rate_; }
  double time() const { return static_cast<double>(now_) / rate_; }  // ctx.currentTime

  void sfx(protocol::SoundId id);
  void setAmbience(Ambience kind);
  Ambience ambience() const { return ambience_; }
  void setMuted(bool m) { muted_ = m; }
  bool muted() const { return muted_; }

  // Mistura `out.size() / channels` quadros (o mesmo sinal em todos os canais) e avança o relógio.
  void render(std::span<float> out, int channels);

  std::size_t voices() const { return voices_.size(); }

  // As primitivas de audio.js (públicas para os testes).
  void tone(double freq, double dur, Wave type = Wave::Sine, double peak = 0.2, double slide = 0, double delay = 0);
  void noise(double dur, double freq, double q = 1, double peak = 0.3, FilterType type = FilterType::Bandpass,
             double slide = 0, double delay = 0);

 private:
  struct Envelope {  // env(g, t, a, d, peak)
    double t = 0, a = 0, d = 0, peak = 0;
    double at(double time) const;
  };
  struct Ramp {  // setValueAtTime(v0, t0) + exponentialRampToValueAtTime(v1, t1)
    double v0 = 0, v1 = 0, t0 = 0, t1 = 0;
    double at(double time) const;
  };
  struct Voice {
    bool isNoise = false;
    Wave wave = Wave::Sine;
    FilterType filter = FilterType::Bandpass;
    double q = 1;
    Ramp freq;
    Envelope env;
    double start = 0, stop = 0;  // s (relógio do contexto)
    double phase = 0;
    std::size_t noisePos = 0;
    Biquad bq;
    int coefIn = 0;  // amostras até recalcular o filtro (deslize do corte)
  };
  struct AmbVoice {
    bool isNoise = false;
    double freq = 0, gain = 0;
    double phase = 0;
    std::size_t noisePos = 0;
    Biquad bq;
  };

  double oscillator(Wave w, double phase, double inc) const;

  double rate_;
  std::uint64_t now_ = 0;
  Pcg32 rng_;
  std::vector<float> noiseBuf_;  // 2 s de ruído branco (noiseBuf)
  std::vector<Voice> voices_;
  std::vector<AmbVoice> amb_;
  Ambience ambience_ = Ambience::None;
  Ramp ambGain_{0.0001, 0.0001, 0, 0};
  bool muted_ = false;
};

}  // namespace rpg::client::audio
