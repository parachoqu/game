#pragma once
// Saída de áudio pela SDL3 (o AudioContext da demo): um stream float estéreo a 48 kHz cuja thread pede
// amostras ao Synth. Sem dispositivo (servidor de CI, --headless) tudo vira nada, sem erro.
#include <memory>
#include <mutex>
#include <vector>

#include "client/audio/Synth.h"

struct SDL_AudioStream;

namespace rpg::client::audio {

class AudioDevice {
 public:
  explicit AudioDevice(bool enabled);
  ~AudioDevice();
  AudioDevice(const AudioDevice&) = delete;
  AudioDevice& operator=(const AudioDevice&) = delete;

  bool active() const { return stream_ != nullptr; }
  void play(protocol::SoundId id);
  void setAmbience(Ambience kind);
  void setMuted(bool muted);

 private:
  static void feed(void* self, SDL_AudioStream* stream, int additional, int total);

  SDL_AudioStream* stream_ = nullptr;
  std::mutex mutex_;
  Synth synth_;
  std::vector<float> buffer_;
  Ambience ambience_ = Ambience::None;
};

}  // namespace rpg::client::audio
