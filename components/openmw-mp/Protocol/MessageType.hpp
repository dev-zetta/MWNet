#ifndef OPENMW_MP_PROTOCOL_MESSAGE_TYPE_HPP
#define OPENMW_MP_PROTOCOL_MESSAGE_TYPE_HPP

#include <cstdint>
#include <span>

namespace mwmp::protocol
{
    enum class MessageType : std::uint16_t
    {
        ContentRequirements = 0x0001,
        ContentManifest = 0x0002,
        ContentVerificationResult = 0x0003,
        AccountLogin = 0x0004,
        AccountRegister = 0x0005,
        AuthenticationResult = 0x0006,
        InitialStateChunk = 0x0007,
        InitialStateComplete = 0x0008,
        SpawnReady = 0x0009,
        SpawnResult = 0x000a,
        Disconnect = 0x000b,
        Ping = 0x000c,
        Pong = 0x000d,
        ServerNotice = 0x000e,
        ChatIntent = 0x000f,
        ChatResult = 0x0010,
        CommandIntent = 0x0011,
        CommandResult = 0x0012,

        MovementSnapshot = 0x1000,
        PlayerStateSnapshot = 0x1001,
        AttackIntent = 0x1002,
        CastIntent = 0x1003,
        CombatResult = 0x1004,
        InventoryActionIntent = 0x1005,
        InventoryDelta = 0x1006,
        ItemUseIntent = 0x1007,
        DeathResult = 0x1008,
        RespawnRequest = 0x1009,
        RespawnResult = 0x100a,
        JailDecisionIntent = 0x100b,
        JailResult = 0x100c,
        PlayerStateIntent = 0x100d,

        ActorSimulationUpdate = 0x2000,
        AuthorityLease = 0x2001,
        ActorStateSnapshot = 0x2002,
        ActorCombatResult = 0x2003,

        ObjectActivateIntent = 0x3000,
        ObjectChangeIntent = 0x3001,
        ObjectStateUpdate = 0x3002,
        ContainerActionIntent = 0x3003,
        ContainerDelta = 0x3004,

        WorldstateIntent = 0x4000,
        WorldstateUpdate = 0x4001,
    };

    std::span<const MessageType> allMessageTypes() noexcept;
    bool isKnownMessageType(std::uint16_t value) noexcept;
}

#endif
