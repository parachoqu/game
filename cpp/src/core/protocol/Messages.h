#pragma once
// Identificadores do que a simulação comunica em forma de texto ou som, sem o texto nem o som.
//
// A demo montava frases em português dentro das regras (`toast(`Vendeu ${n}× …`)`). Aqui a simulação
// emite um MessageId com argumentos tipados, e o cliente localiza com data/text/pt-BR/messages.json.
// O mesmo vale para sons (SoundId → síntese do cliente) e textos flutuantes (FloatKind).
//
// A lista é única (X-macro): o enum, a chave estável usada no JSON e no protocolo, e a checagem de que
// o arquivo de textos cobre todas as chaves saem da mesma tabela.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rpg::protocol {

// ---------------------------------------------------------------- sons (engine/audio.js `sfx`)
#define RPG_SOUNDS(X)                                                                                      \
  X(Swing, "swing") X(Hit, "hit") X(Hitmark, "hitmark") X(Hurt, "hurt") X(Block, "block") X(Dodge, "dodge") \
  X(Arrow, "arrow") X(Warn, "warn") X(Pickup, "pickup") X(Coin, "coin") X(Craft, "craft") X(Gather, "gather") \
  X(Star, "star") X(Portal, "portal") X(Death, "death") X(Book, "book") X(Ui, "ui") X(Deny, "deny")          \
  X(Event, "event") X(Horn, "horn") X(Magic, "magic") X(Roar, "roar") X(Slam, "slam")

enum class SoundId : std::uint8_t {
#define RPG_X(name, key) name,
  RPG_SOUNDS(RPG_X)
#undef RPG_X
  Count
};
std::string_view soundKey(SoundId id);

// ---------------------------------------------------------------- avisos (ui/hud.js `toast`)
enum class ToastKind : std::uint8_t { Info, Warn, Danger, Item, Coin, Event, Sky, Turb, Book };
std::string_view toastKindKey(ToastKind k);

// ---------------------------------------------------------------- textos flutuantes (engine/fx.js)
enum class FloatKind : std::uint8_t { Dmg, Crit, Heal, Block, Dodge, Hurt, Coin };
std::string_view floatKindKey(FloatKind k);

// ---------------------------------------------------------------- mensagens
// Chave → texto em data/text/pt-BR/messages.json. Placeholders: {0}, {1}…; modificadores {0|lower}
// (minúsculas), {0|cap} (primeira maiúscula) e {0|s} ("s" quando o número passa de 1).
#define RPG_MESSAGES(X)                                         \
  /* acampamento */                                             \
  X(CampPressured, "camp.pressured")                            \
  X(CampDisplaced, "camp.displaced")                            \
  X(CampRecovering, "camp.recovering")                          \
  X(CampReestablished, "camp.reestablished")                    \
  /* combate */                                                 \
  X(MountKnockedOff, "combat.mountKnockedOff")                  \
  X(ItemReceived, "item.received")                              \
  X(BanditDroppedCargo, "combat.banditDroppedCargo")            \
  X(BanditLootedCargo, "combat.banditLootedCargo")              \
  X(PlayerDown, "player.down")                                  \
  X(WeaponBroken, "player.weaponBroken")                        \
  X(SkillNone, "player.skillNone")                              \
  X(VigorInsufficient, "player.vigorInsufficient")              \
  X(FieldRepairDone, "player.fieldRepairDone")                  \
  X(PotionNone, "player.potionNone")                            \
  X(PotionFullHp, "player.potionFullHp")                        \
  X(JumpOverloaded, "player.jumpOverloaded")                    \
  X(JumpNoVigor, "player.jumpNoVigor")                          \
  X(DismountToFight, "player.dismountToFight")                  \
  X(DodgeOverloaded, "player.dodgeOverloaded")                  \
  X(DodgeNoVigor, "player.dodgeNoVigor")                        \
  X(GuardRescued, "player.guardRescued")                        \
  X(ChannelCancelled, "channel.cancelled")                      \
  X(ChannelReasonCancelled, "channel.reason.cancelled")         \
  X(ChannelReasonInterrupted, "channel.reason.interrupted")     \
  X(ChannelFieldRepair, "channel.fieldRepair")                  \
  X(ChannelMine, "channel.gather.minerio")                      \
  X(ChannelChop, "channel.gather.madeira")                      \
  X(ChannelHerb, "channel.gather.erva")                         \
  X(ChannelCrystal, "channel.gather.cristal")                   \
  X(ChannelRecon, "channel.recon")                              \
  X(ChannelMounting, "channel.mounting")                        \
  X(ChannelCallingMount, "channel.callingMount")                \
  X(ChannelExtracting, "channel.extracting")                    \
  X(ChannelShrine, "channel.shrine")                            \
  /* economia */                                                \
  X(MarketSold, "market.sold")                                  \
  X(CoinsInsufficient, "market.coinsInsufficient")              \
  X(CargoNoCapacity, "market.cargoNoCapacity")                  \
  X(MarketBought, "market.bought")                              \
  X(MountAlreadyOwned, "stable.alreadyOwned")                   \
  X(MountBought, "stable.bought")                               \
  X(KitBought, "trainer.kitBought")                             \
  X(KitFree, "trainer.kitFree")                                 \
  X(TrainingLearned, "trainer.learned")                         \
  X(RepairNoCoins, "forge.repairNoCoins")                       \
  X(RepairNeedsIngot, "forge.repairNeedsIngot")                 \
  X(RepairDone, "forge.repairDone")                             \
  X(CraftRequires, "forge.requires")                            \
  X(CraftMissing, "forge.missing")                              \
  /* evento regional */                                         \
  X(EventNoneInBag, "event.noneInBag")                          \
  X(EventDelivered, "event.delivered")                          \
  X(EventReconDone, "event.reconDone")                          \
  /* coleta e interação */                                      \
  X(GatherInCombat, "gather.inCombat")                          \
  X(GatherNoCapacity, "gather.noCapacity")                      \
  X(GatherGot, "gather.got")                                    \
  X(GatherGotPartial, "gather.gotPartial")                      \
  X(NodeDepleted, "interact.nodeDepleted")                      \
  X(NodeAvailable, "interact.nodeAvailable")                    \
  /* montaria */                                                \
  X(MountNone, "mount.none")                                    \
  X(MountNoTurbulent, "mount.noTurbulent")                      \
  X(MountInCombat, "mount.inCombat")                            \
  X(MountTooFar, "mount.tooFar")                                \
  /* céu */                                                     \
  X(StarVanished, "sky.starVanished")                           \
  X(AstronomerPaid, "sky.astronomerPaid")                       \
  /* Turbulenta */                                              \
  X(PortalOpened, "turb.portalOpened")                          \
  X(PortalClosed, "turb.portalClosed")                          \
  X(TurbEntered, "turb.entered")                                \
  X(TurbExtracted, "turb.extracted")                            \
  X(TurbFragmentNoCapacity, "turb.fragmentNoCapacity")          \
  X(TurbFragmentGot, "turb.fragmentGot")                        \
  X(TurbLateShadow, "turb.lateShadow")                          \
  X(TurbCollapseWarning, "turb.collapseWarning")                \
  X(DirN, "dir.n") X(DirNE, "dir.ne") X(DirE, "dir.e") X(DirSE, "dir.se") \
  X(DirS, "dir.s") X(DirSW, "dir.sw") X(DirW, "dir.w") X(DirNW, "dir.nw") \
  /* painéis */                                                 \
  X(StorageNoCapacity, "storage.noCapacity")                    \
  X(StableBagsStored, "stable.bagsStored")                      \
  X(LootNoCapacity, "loot.noCapacity")                          \
  X(GearInCombat, "inventory.gearInCombat")                     \
  X(SaddleFull, "inventory.saddleFull")                         \
  X(BagLimit, "inventory.bagLimit")                             \
  X(GearRequiresTraining, "inventory.requiresTraining")         \
  X(ItemDropped, "inventory.dropped")                           \
  X(RequestTooFar, "request.tooFar")                            \
  /* textos flutuantes */                                       \
  X(FloatNumber, "float.number")                                \
  X(FloatHurt, "float.hurt")                                    \
  X(FloatHeal, "float.heal")                                    \
  X(FloatDodge, "float.dodge")                                  \
  X(FloatShield, "float.shield")                                \
  X(FloatSlow, "float.slow")                                    \
  X(FloatRoot, "float.root")                                    \
  X(FloatCurse, "float.curse")                                  \
  X(FloatStun, "float.stun")                                    \
  X(FloatInterrupted, "float.interrupted")                      \
  X(FloatLifeGain, "float.lifeGain")                            \
  X(FloatBeastForm, "float.beastForm")                          \
  X(FloatHolyShield, "float.holyShield")                        \
  X(FloatAura, "float.aura")                                    \
  X(FloatCoins, "float.coins")                                  \
  /* relatório de derrota: o que permanece */                   \
  X(KeptKnowledge, "death.kept.knowledge")                      \
  X(KeptBook, "death.kept.book")                                \
  X(KeptCoins, "death.kept.coins")                              \
  X(KeptStorage, "death.kept.storage")                          \
  X(KeptCargo, "death.kept.cargo")                              \
  X(KeptEquipWorn, "death.kept.equipWorn")                      \
  X(KeptMountItem, "death.kept.mountItem")                      \
  /* causas de dano (relatório de derrota) */                   \
  X(CauseWounds, "cause.wounds")                                \
  X(CauseSpell, "cause.spell")                                  \
  X(CauseArrow, "cause.arrow")                                  \
  X(CauseCollapse, "cause.collapse")                            \
  X(CauseTest, "cause.test")

enum class MessageId : std::uint16_t {
#define RPG_X(name, key) name,
  RPG_MESSAGES(RPG_X)
#undef RPG_X
  Count
};
std::string_view messageKey(MessageId id);
std::optional<MessageId> messageFromKey(std::string_view key);

// ---------------------------------------------------------------- argumentos
// Um argumento de mensagem: número, texto livre (nome do personagem, anotação) ou referência a algo
// que o cliente sabe nomear (item, lugar, treino, zona, inimigo, outra mensagem).
struct MsgArg {
  enum class Type : std::uint8_t { Int, Real, Text, Item, Place, Training, Zone, Enemy, Message };
  Type type = Type::Int;
  double number = 0.0;   // Int, Real
  std::uint16_t id = 0;  // Item, Place, Training, Zone, Enemy, Message
  std::string text;      // Text

  static MsgArg integer(long long v) { return {Type::Int, static_cast<double>(v), 0, {}}; }
  static MsgArg real(double v) { return {Type::Real, v, 0, {}}; }
  static MsgArg str(std::string s) { return {Type::Text, 0.0, 0, std::move(s)}; }
  static MsgArg ref(Type t, std::uint16_t v) { return {t, 0.0, v, {}}; }
  static MsgArg message(MessageId m) { return {Type::Message, 0.0, static_cast<std::uint16_t>(m), {}}; }

  bool operator==(const MsgArg&) const = default;

  template <class A>
  void io(A& a) {
    a(type);
    a(number);
    a(id);
    a(text);
  }
};
using MsgArgs = std::vector<MsgArg>;

}  // namespace rpg::protocol
