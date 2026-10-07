#include "client/audio/AudioDevice.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>

#include "core/Log.h"

namespace rpg::client::audio {

namespace {
constexpr int kRate = 48000;
constexpr int kChannels = 2;
}  // namespace

AudioDevice::AudioDevice(bool enabled) : synth_(kRate, 0x5eed) {
  if (!enabled) return;
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    log::warn("áudio indisponível: {}", SDL_GetError());
    return;
  }
  const SDL_AudioSpec spec{SDL_AUDIO_F32, kChannels, kRate};
  stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, &AudioDevice::feed, this);
  if (!stream_) {
    log::warn("áudio indisponível: {}", SDL_GetError());
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return;
  }
  SDL_ResumeAudioStreamDevice(stream_);
}

AudioDevice::~AudioDevice() {
  if (!stream_) return;
  SDL_DestroyAudioStream(stream_);  // para a thread de áudio antes de o Synth sumir
  SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

void AudioDevice::feed(void* self, SDL_AudioStream* stream, int additional, int /*total*/) {
  auto* d = static_cast<AudioDevice*>(self);
  if (additional <= 0) return;
  const std::size_t floats = static_cast<std::size_t>(additional) / sizeof(float) / kChannels * kChannels;
  {
    std::lock_guard lock(d->mutex_);
    d->buffer_.resize(floats);
    d->synth_.render(d->buffer_, kChannels);
  }
  SDL_PutAudioStreamData(stream, d->buffer_.data(), static_cast<int>(floats * sizeof(float)));
}

void AudioDevice::play(protocol::SoundId id) {
  if (!stream_) return;
  std::lock_guard lock(mutex_);
  synth_.sfx(id);
}

void AudioDevice::setAmbience(Ambience kind) {
  if (!stream_ || kind == ambience_) return;
  ambience_ = kind;
  std::lock_guard lock(mutex_);
  synth_.setAmbience(kind);
}

void AudioDevice::setMuted(bool muted) {
  if (!stream_) return;
  std::lock_guard lock(mutex_);
  synth_.setMuted(muted);
}

}  // namespace rpg::client::audio
