#include "MessageType.hpp"

#include <algorithm>
#include <array>

namespace mwmp::protocol
{
    namespace
    {
        constexpr std::array sMessageTypes{
            MessageType::ContentRequirements,
            MessageType::ContentManifest,
            MessageType::ContentVerificationResult,
            MessageType::AccountLogin,
            MessageType::AccountRegister,
            MessageType::AuthenticationResult,
            MessageType::InitialStateChunk,
            MessageType::InitialStateComplete,
            MessageType::SpawnReady,
            MessageType::SpawnResult,
            MessageType::Disconnect,
            MessageType::Ping,
            MessageType::Pong,
            MessageType::ServerNotice,
            MessageType::ChatIntent,
            MessageType::ChatResult,
            MessageType::CommandIntent,
            MessageType::CommandResult,
            MessageType::MovementSnapshot,
            MessageType::PlayerStateSnapshot,
            MessageType::AttackIntent,
            MessageType::CastIntent,
            MessageType::CombatResult,
            MessageType::InventoryActionIntent,
            MessageType::InventoryDelta,
            MessageType::ItemUseIntent,
            MessageType::DeathResult,
            MessageType::RespawnRequest,
            MessageType::RespawnResult,
            MessageType::JailDecisionIntent,
            MessageType::JailResult,
            MessageType::PlayerStateIntent,
            MessageType::ActorSimulationUpdate,
            MessageType::AuthorityLease,
            MessageType::ActorStateSnapshot,
            MessageType::ActorCombatResult,
            MessageType::ObjectActivateIntent,
            MessageType::ObjectChangeIntent,
            MessageType::ObjectStateUpdate,
            MessageType::ContainerActionIntent,
            MessageType::ContainerDelta,
            MessageType::WorldstateIntent,
            MessageType::WorldstateUpdate,
        };
    }

    std::span<const MessageType> allMessageTypes() noexcept
    {
        return sMessageTypes;
    }

    bool isKnownMessageType(std::uint16_t value) noexcept
    {
        return std::ranges::find(sMessageTypes, static_cast<MessageType>(value))
            != sMessageTypes.end();
    }
}
