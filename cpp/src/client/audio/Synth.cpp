#include "client/audio/Synth.h"

#include <algorithm>
#include <cmath>

namespace rpg::client::audio {

namespace {

constexpr double kTwoPi = 6.283185307179586;
constexpr double kFloor = 0.0001;    // o "zero" das rampas exponenciais do WebAudio
constexpr double kMaster = 0.55;     // master.gain
constexpr int kCoefEvery = 16;       // amostras entre recálculos do filtro quando o corte desliza

// PolyBLEP: suaviza a descontinuidade das ondas quadrada e dente de serra (o WebAudio usa ondas
// limitadas em banda; sem isso, os agudos de 1–3 kHz em onda quadrada soariam ásperos).
double polyBlep(double t, double dt) {
  if (t < dt) {
    t /= dt;
    return t + t - t * t - 1.0;
  }
  if (t > 1.0 - dt) {
    t = (t - 1.0) / dt;
    return t * t + t + t + 1.0;
  }
  return 0.0;
}

}  // namespace

void Biquad::set(FilterType type, double freq, double q, double sampleRate) {
  const double nyquist = sampleRate * 0.5;
  const double f = std::clamp(freq, 1.0, nyquist * 0.999);
  const double w0 = kTwoPi * f / sampleRate;
  const double cw = std::cos(w0), sw = std::sin(w0);
  double nb0 = 0, nb1 = 0, nb2 = 0;
  double alpha = 0;
  switch (type) {
    case FilterType::Lowpass:
      alpha = sw / (2.0 * std::pow(10.0, q / 20.0));
      nb0 = (1.0 - cw) / 2.0;
      nb1 = 1.0 - cw;
      nb2 = (1.0 - cw) / 2.0;
      break;
    case FilterType::Highpass:
      alpha = sw / (2.0 * std::pow(10.0, q / 20.0));
      nb0 = (1.0 + cw) / 2.0;
      nb1 = -(1.0 + cw);
      nb2 = (1.0 + cw) / 2.0;
      break;
    case FilterType::Bandpass:
      alpha = sw / (2.0 * std::max(q, 1e-4));
      nb0 = alpha;
      nb1 = 0.0;
      nb2 = -alpha;
      break;
  }
  const double a0 = 1.0 + alpha;
  b0 = nb0 / a0;
  b1 = nb1 / a0;
  b2 = nb2 / a0;
  a1 = -2.0 * cw / a0;
  a2 = (1.0 - alpha) / a0;
}

double Synth::Envelope::at(double time) const {
  if (time <= t) return kFloor;
  if (time < t + a) return kFloor * std::pow(peak / kFloor, (time - t) / a);
  if (time < t + a + d) return peak * std::pow(kFloor / peak, (time - t - a) / d);
  return kFloor;
}

double Synth::Ramp::at(double time) const {
  if (time <= t0 || t1 <= t0) return time >= t1 ? v1 : v0;
  if (time >= t1) return v1;
  return v0 * std::pow(v1 / v0, (time - t0) / (t1 - t0));
}

Synth::Synth(double sampleRate, std::uint64_t seed) : rate_(sampleRate), rng_(seed, 11) {
  noiseBuf_.resize(static_cast<std::size_t>(sampleRate * 2));
  for (float& s : noiseBuf_) s = static_cast<float>(rng_.next() * 2.0 - 1.0);
}

double Synth::oscillator(Wave w, double phase, double inc) const {
  switch (w) {
    case Wave::Sine: return std::sin(kTwoPi * phase);
    case Wave::Square: {
      double v = phase < 0.5 ? 1.0 : -1.0;
      v += polyBlep(phase, inc);
      v -= polyBlep(std::fmod(phase + 0.5, 1.0), inc);
      return v;
    }
    case Wave::Triangle: return 1.0 - 4.0 * std::abs(phase - 0.5);
    case Wave::Sawtooth: return 2.0 * phase - 1.0 - polyBlep(phase, inc);
  }
  return 0.0;
}

void Synth::tone(double freq, double dur, Wave type, double peak, double slide, double delay) {
  const double t = time() + delay;
  Voice v;
  v.wave = type;
  v.freq = {freq, slide ? std::max(20.0, freq * slide) : freq, t, slide ? t + dur : t};
  v.env = {t, 0.008, dur, peak};
  v.start = t;
  v.stop = t + dur + 0.05;
  voices_.push_back(v);
}

void Synth::noise(double dur, double freq, double q, double peak, FilterType type, double slide, double delay) {
  const double t = time() + delay;
  Voice v;
  v.isNoise = true;
  v.filter = type;
  v.q = q;
  v.freq = {freq, slide ? freq * slide : freq, t, slide ? t + dur : t};
  v.env = {t, 0.005, dur, peak};
  v.start = t;
  v.stop = t + dur + 0.05;
  v.noisePos = static_cast<std::size_t>(rng_.next() * rate_);  // s.start(t, Math.random())
  v.bq.set(type, freq, q, rate_);
  voices_.push_back(v);
}

void Synth::sfx(protocol::SoundId id) {
  if (muted_) return;
  using S = protocol::SoundId;
  using W = Wave;
  using F = FilterType;
  switch (id) {
    case S::Swing: noise(0.16, 900, 1.2, 0.18, F::Bandpass, 3); break;
    case S::Hit: tone(120, 0.14, W::Triangle, 0.35, 0.5); noise(0.08, 2000, 0.8, 0.2); break;
    case S::Hitmark: tone(1950, 0.035, W::Triangle, 0.22); tone(2800, 0.025, W::Sine, 0.15, 0, 0.015); break;
    case S::Hurt: tone(90, 0.22, W::Sawtooth, 0.18, 0.6); noise(0.12, 600, 0.7, 0.25); break;
    case S::Block: tone(1400, 0.18, W::Square, 0.08, 0.9); tone(2100, 0.12, W::Sine, 0.08); break;
    case S::Dodge: noise(0.22, 500, 0.8, 0.14, F::Bandpass, 4); break;
    case S::Arrow: tone(420, 0.08, W::Triangle, 0.15, 0.4); noise(0.12, 3000, 2, 0.1); break;
    case S::Warn: tone(660, 0.07, W::Square, 0.05); break;
    case S::Pickup: tone(520, 0.08, W::Sine, 0.15); tone(780, 0.1, W::Sine, 0.13, 0, 0.07); break;
    case S::Coin: tone(1300, 0.06, W::Square, 0.06); tone(1750, 0.12, W::Square, 0.05, 0, 0.05); break;
    case S::Craft:
      tone(900, 0.25, W::Triangle, 0.15, 0.98);
      noise(0.05, 4000, 3, 0.2);
      tone(1350, 0.3, W::Sine, 0.06, 1, 0.02);
      break;
    case S::Gather: noise(0.07, 1500, 1.5, 0.18); tone(220, 0.06, W::Triangle, 0.1, 0.7); break;
    case S::Star: tone(1560, 1.6, W::Sine, 0.09, 0.5); tone(1040, 2.2, W::Sine, 0.06, 0.5, 0.25); break;
    case S::Portal: tone(70, 1.2, W::Sawtooth, 0.08, 1.4); tone(105, 1.2, W::Sine, 0.1, 0.8); break;
    case S::Death: tone(220, 1.1, W::Sine, 0.2, 0.3); tone(165, 1.3, W::Triangle, 0.12, 0.3, 0.2); break;
    case S::Book: noise(0.18, 3000, 0.6, 0.08, F::Highpass); tone(880, 0.5, W::Sine, 0.05, 1, 0.1); break;
    case S::Ui: tone(700, 0.04, W::Sine, 0.06); break;
    case S::Deny: tone(200, 0.12, W::Square, 0.06, 0.8); break;
    case S::Event:
      tone(523, 0.3, W::Triangle, 0.12);
      tone(659, 0.3, W::Triangle, 0.12, 1, 0.15);
      tone(784, 0.6, W::Triangle, 0.12, 1, 0.3);
      break;
    case S::Horn: tone(146, 0.9, W::Sawtooth, 0.07, 1.02); tone(220, 0.9, W::Sawtooth, 0.05, 1.01, 0.05); break;
    case S::Magic: tone(620, 0.28, W::Sine, 0.12, 1.4); noise(0.2, 1800, 1.5, 0.08, F::Bandpass, 1.8); break;
    case S::Roar: tone(85, 0.65, W::Sawtooth, 0.22, 0.7); noise(0.4, 450, 0.8, 0.25, F::Lowpass, 0.5); break;
    case S::Slam: tone(75, 0.35, W::Triangle, 0.35, 0.4); noise(0.25, 300, 1.2, 0.3); break;
    case S::Count: break;
  }
}

void Synth::setAmbience(Ambience kind) {
  amb_.clear();
  ambience_ = kind;
  const double t = time();
  if (kind == Ambience::World) {
    AmbVoice v;
    v.isNoise = true;
    v.gain = 0.12;
    v.bq.set(FilterType::Lowpass, 380, 1, rate_);
    amb_.push_back(v);
  } else if (kind == Ambience::Turbulent) {
    for (const double fr : {55.0, 58.3, 82.4}) {
      AmbVoice v;
      v.freq = fr;
      v.gain = 0.05;
      v.bq.set(FilterType::Lowpass, 260, 1, rate_);
      amb_.push_back(v);
    }
  }
  // cancelScheduledValues; setValueAtTime(0,0001); exponentialRampToValueAtTime(alvo, t + 1,5)
  ambGain_ = {kFloor, kind == Ambience::None ? kFloor : 0.8, t, t + 1.5};
}

void Synth::render(std::span<float> out, int channels) {
  const int ch = std::max(1, channels);
  const std::size_t frames = out.size() / static_cast<std::size_t>(ch);
  std::vector<double> mix(frames, 0.0);
  const double t0 = time();
  const double dt = 1.0 / rate_;
  const std::size_t nNoise = noiseBuf_.size();

  for (Voice& v : voices_) {
    for (std::size_t i = 0; i < frames; ++i) {
      const double t = t0 + static_cast<double>(i) * dt;
      if (t < v.start || t >= v.stop) continue;
      const double g = v.env.at(t);
      const double f = v.freq.at(t);
      double s = 0;
      if (v.isNoise) {
        if (v.freq.t1 > v.freq.t0 && --v.coefIn <= 0) {
          v.bq.set(v.filter, f, v.q, rate_);
          v.coefIn = kCoefEvery;
        }
        s = v.bq.process(static_cast<double>(noiseBuf_[v.noisePos % nNoise]));
        ++v.noisePos;
      } else {
        const double inc = f / rate_;
        s = oscillator(v.wave, v.phase, inc);
        v.phase += inc;
        v.phase -= std::floor(v.phase);
      }
      mix[i] += s * g;
    }
  }
  if (!amb_.empty()) {
    for (std::size_t i = 0; i < frames; ++i) {
      const double t = t0 + static_cast<double>(i) * dt;
      const double g = ambGain_.at(t);
      double s = 0;
      for (AmbVoice& a : amb_) {
        double x = 0;
        if (a.isNoise) {
          x = static_cast<double>(noiseBuf_[a.noisePos]);
          a.noisePos = (a.noisePos + 1) % nNoise;  // s.loop = true
        } else {
          const double inc = a.freq / rate_;
          x = oscillator(Wave::Sawtooth, a.phase, inc);
          a.phase += inc;
          a.phase -= std::floor(a.phase);
        }
        s += a.bq.process(x) * a.gain;
      }
      mix[i] += s * g;
    }
  }
  const double master = muted_ ? 0.0 : kMaster;
  for (std::size_t i = 0; i < frames; ++i) {
    const auto s = static_cast<float>(std::clamp(mix[i] * master, -1.0, 1.0));
    for (int c = 0; c < ch; ++c) out[i * static_cast<std::size_t>(ch) + static_cast<std::size_t>(c)] = s;
  }
  now_ += frames;
  const double end = time();
  std::erase_if(voices_, [&](const Voice& v) { return v.stop <= end; });
}

}  // namespace rpg::client::audio
