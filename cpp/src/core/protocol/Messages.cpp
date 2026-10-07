#include "core/protocol/Messages.h"

#include <array>

namespace rpg::protocol {
namespace {

constexpr std::array<std::string_view, static_cast<std::size_t>(SoundId::Count)> kSounds = {
#define RPG_X(name, key) key,
    RPG_SOUNDS(RPG_X)
#undef RPG_X
};

constexpr std::array<std::string_view, static_cast<std::size_t>(MessageId::Count)> kMessages = {
#define RPG_X(name, key) key,
    RPG_MESSAGES(RPG_X)
#undef RPG_X
};

}  // namespace

std::string_view soundKey(SoundId id) { return kSounds[static_cast<std::size_t>(id)]; }

std::string_view toastKindKey(ToastKind k) {
  static constexpr std::array<std::string_view, 9> kKinds = {"info", "warn", "danger", "item", "coin",
                                                             "event", "sky", "turb", "book"};
  return kKinds[static_cast<std::size_t>(k)];
}

std::string_view floatKindKey(FloatKind k) {
  static constexpr std::array<std::string_view, 7> kKinds = {"dmg", "crit", "heal", "block", "dodge", "hurt", "coin"};
  return kKinds[static_cast<std::size_t>(k)];
}

std::string_view messageKey(MessageId id) { return kMessages[static_cast<std::size_t>(id)]; }

std::optional<MessageId> messageFromKey(std::string_view key) {
  for (std::size_t i = 0; i < kMessages.size(); ++i)
    if (kMessages[i] == key) return static_cast<MessageId>(i);
  return std::nullopt;
}

}  // namespace rpg::protocol
