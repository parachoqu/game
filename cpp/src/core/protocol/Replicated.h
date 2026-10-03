#pragma once
// Enumerações que cruzam a rede dentro dos snapshots (EntityState::state, ::type, WorldState::campState…).
// Ficam no protocolo para o cliente interpretar os números sem conhecer a simulação; a simulação usa
// as mesmas (sim/Entities.h e sim/Sim.h as trazem por `using`).
#include <cstdint>

namespace rpg::protocol {

// EntityState::state de jogadores e PrivateState::state
enum class PlayerState : std::uint8_t { Free, Attack, Skill, Dodge, Stagger, Channel, Down, Dead };
// EntityState::state de inimigos
enum class EnemyState : std::uint8_t { Idle, Loot, Chase, Windup, Recover, Stun, Return, Dead };

// Projéteis: EntityState::type = kind | school << 4 | owner << 8
enum class ProjectileOwner : std::uint8_t { Player, Enemy, Shadow };
enum class ProjectileKind : std::uint8_t { Arrow, Bolt, Spell };
inline ProjectileKind projectileKind(std::uint16_t type) { return static_cast<ProjectileKind>(type & 0xF); }
inline std::uint8_t projectileSchool(std::uint16_t type) { return static_cast<std::uint8_t>((type >> 4) & 0xF); }
inline ProjectileOwner projectileOwner(std::uint16_t type) { return static_cast<ProjectileOwner>((type >> 8) & 0xF); }

// Sacos de carga: EntityState::type
enum class LootLabel : std::uint8_t { Cargo, Spoils, Recoverable, YourCargo, Dropped };

// WorldState::campState
enum class CampPhase : std::uint8_t { Estabelecido, Pressionado, Deslocado, Recuperacao };

}  // namespace rpg::protocol
