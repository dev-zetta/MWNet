#ifndef OPENMW_MP_PROTOCOL_APPLICATION_PACKET_ID_HPP
#define OPENMW_MP_PROTOCOL_APPLICATION_PACKET_ID_HPP

#include <cstdint>

namespace mwmp::protocol
{
    // Protocol 11 assigns MWNet application packet identifiers independently
    // from any transport library. The values intentionally match the final
    // protocol-10 application range only to keep the protocol-11 migration
    // reviewable. These values are MWNet-owned and transport-independent.
    enum class ApplicationPacketId : std::uint16_t
    {
        UserMyId = 136,
        UserDisconnected,
        ChatMessage,
        SystemHandshake,
        Loaded,
        GuiMessageBox,
        PlayerBaseInfo,
        PlayerBehavior,
        PlayerCharGen,
        PlayerSpellsActive,
        PlayerAnimFlags,
        PlayerAnimPlay,
        PlayerAttack,
        PlayerAttribute,
        PlayerBook,
        PlayerBounty,
        PlayerCellChange,
        PlayerCellState,
        PlayerCharClass,
        PlayerDeath,
        PlayerDisposition,
        PlayerEquipment,
        PlayerFaction,
        PlayerInput,
        PlayerInventory,
        PlayerJail,
        PlayerJournal,
        WorldKillCount,
        PlayerLevel,
        PlayerMiscellaneous,
        PlayerMomentum,
        PlayerPosition,
        PlayerQuickKeys,
        WorldRegionAuthority,
        PlayerReputation,
        PlayerResurrect,
        PlayerRest,
        PlayerShapeshift,
        PlayerSkill,
        PlayerSpeech,
        PlayerSpellbook,
        PlayerStatsDynamic,
        PlayerTopic,
        ActorList,
        ActorAuthority,
        ActorTest,
        ActorAi,
        ActorAnimFlags,
        ActorAnimPlay,
        ActorAttack,
        ActorCellChange,
        ActorDeath,
        ActorEquipment,
        ActorCast,
        ActorPosition,
        ActorSpeech,
        ActorStatsDynamic,
        ObjectActivate,
        ObjectAnimPlay,
        ObjectAttach,
        ObjectSound,
        ObjectDelete,
        ObjectLock,
        ObjectMove,
        ObjectPlace,
        ObjectHit,
        ObjectRotate,
        ObjectScale,
        ObjectSpawn,
        ObjectState,
        ObjectTrap,
        ConsoleCommand,
        Container,
        DoorDestination,
        DoorState,
        MusicPlay,
        VideoPlay,
        ClientScriptLocal,
        ObjectDialogueChoice,
        ScriptMemberShort,
        ObjectMiscellaneous,
        ClientScriptGlobal,
        ObjectRestock,
        GameSettings,
        GamePreInit,
        ClientScriptSettings,
        CellReset,
        RecordDynamic,
        WorldCollisionOverride,
        WorldMap,
        WorldTime,
        WorldWeather,
        PlayerItemUse,
        PlayerCast,
        PlayerAlly,
        WorldDestinationOverride,
        ActorSpellsActive,
        PlayerCooldowns,
    };

    constexpr std::uint16_t firstApplicationPacketId
        = static_cast<std::uint16_t>(ApplicationPacketId::UserMyId);
    constexpr std::uint16_t lastApplicationPacketId
        = static_cast<std::uint16_t>(ApplicationPacketId::PlayerCooldowns);

    constexpr bool isApplicationPacketId(std::uint16_t value) noexcept
    {
        return value >= firstApplicationPacketId && value <= lastApplicationPacketId;
    }
}

#endif
