#include "ApplicationPacketBridge.hpp"

#include <components/openmw-mp/Protocol/ProtocolLimits.hpp>

#include <new>
#include <utility>

namespace mwmp::transport
{
    namespace
    {
        using protocol::ApplicationPacketId;
        using protocol::MessageType;

        constexpr bool between(ApplicationPacketId id, ApplicationPacketId first,
            ApplicationPacketId last) noexcept
        {
            return id >= first && id <= last;
        }

        constexpr MessageType playerMessage(ApplicationPacketId id,
            ApplicationPacketFlow flow) noexcept
        {
            const bool toServer = flow == ApplicationPacketFlow::ClientToServer;
            switch (id)
            {
                case ApplicationPacketId::PlayerPosition:
                case ApplicationPacketId::PlayerMomentum:
                case ApplicationPacketId::PlayerInput:
                    return toServer ? MessageType::MovementSnapshot
                                    : MessageType::PlayerStateSnapshot;
                case ApplicationPacketId::PlayerAttack:
                    return toServer ? MessageType::AttackIntent : MessageType::CombatResult;
                case ApplicationPacketId::PlayerCast:
                    return toServer ? MessageType::CastIntent : MessageType::CombatResult;
                case ApplicationPacketId::PlayerInventory:
                case ApplicationPacketId::PlayerEquipment:
                    return toServer ? MessageType::InventoryActionIntent
                                    : MessageType::InventoryDelta;
                case ApplicationPacketId::PlayerItemUse:
                    return toServer ? MessageType::ItemUseIntent : MessageType::InventoryDelta;
                case ApplicationPacketId::PlayerDeath:
                    return toServer ? MessageType::PlayerStateIntent : MessageType::DeathResult;
                case ApplicationPacketId::PlayerResurrect:
                    return toServer ? MessageType::RespawnRequest : MessageType::RespawnResult;
                case ApplicationPacketId::PlayerJail:
                    return toServer ? MessageType::JailDecisionIntent : MessageType::JailResult;
                default:
                    return toServer ? MessageType::PlayerStateIntent
                                    : MessageType::PlayerStateSnapshot;
            }
        }

        constexpr MessageType actorMessage(ApplicationPacketId id,
            ApplicationPacketFlow flow) noexcept
        {
            if (flow == ApplicationPacketFlow::ClientToServer)
                return MessageType::ActorSimulationUpdate;
            if (id == ApplicationPacketId::ActorAuthority)
                return MessageType::AuthorityLease;
            if (id == ApplicationPacketId::ActorAttack
                || id == ApplicationPacketId::ActorCast
                || id == ApplicationPacketId::ActorDeath
                || id == ApplicationPacketId::ActorStatsDynamic)
                return MessageType::ActorCombatResult;
            return MessageType::ActorStateSnapshot;
        }

        constexpr MessageType objectMessage(ApplicationPacketId id,
            ApplicationPacketFlow flow) noexcept
        {
            if (flow == ApplicationPacketFlow::ServerToClient)
                return id == ApplicationPacketId::Container ? MessageType::ContainerDelta
                                                            : MessageType::ObjectStateUpdate;
            if (id == ApplicationPacketId::ObjectActivate)
                return MessageType::ObjectActivateIntent;
            if (id == ApplicationPacketId::Container)
                return MessageType::ContainerActionIntent;
            return MessageType::ObjectChangeIntent;
        }

        constexpr bool isUnreliableSnapshot(ApplicationPacketId id) noexcept
        {
            return id == ApplicationPacketId::PlayerPosition
                || id == ApplicationPacketId::PlayerMomentum
                || id == ApplicationPacketId::ActorPosition;
        }
    }

    bool applicationPacketRoute(ApplicationPacketId id,
        ApplicationPacketFlow flow, ApplicationPacketRoute& route) noexcept
    {
        ApplicationPacketRoute decoded;
        const bool toServer = flow == ApplicationPacketFlow::ClientToServer;

        if (id == ApplicationPacketId::GamePreInit)
            decoded.messageType = toServer ? MessageType::ContentManifest
                                           : MessageType::ContentRequirements;
        else if (id == ApplicationPacketId::SystemHandshake)
            decoded.messageType = toServer ? MessageType::AccountLogin
                                           : MessageType::AuthenticationResult;
        else if (id == ApplicationPacketId::Loaded)
            decoded.messageType = toServer ? MessageType::SpawnReady : MessageType::SpawnResult;
        else if (id == ApplicationPacketId::UserDisconnected)
            decoded.messageType = toServer ? MessageType::Disconnect
                                           : MessageType::PlayerStateSnapshot;
        else if (id == ApplicationPacketId::ChatMessage)
            decoded.messageType = toServer ? MessageType::ChatIntent : MessageType::ChatResult;
        else if (id == ApplicationPacketId::GuiMessageBox)
            decoded.messageType = toServer ? MessageType::CommandIntent : MessageType::ServerNotice;
        else if (id == ApplicationPacketId::ConsoleCommand)
            decoded.messageType = toServer ? MessageType::CommandIntent : MessageType::CommandResult;
        else if (id == ApplicationPacketId::WorldKillCount
            || id == ApplicationPacketId::WorldRegionAuthority
            || id == ApplicationPacketId::ClientScriptGlobal
            || id == ApplicationPacketId::WorldDestinationOverride)
        {
            decoded.lane = MessageLane::Worldstate;
            decoded.messageType = toServer ? MessageType::WorldstateIntent
                                           : MessageType::WorldstateUpdate;
        }
        else if (id == ApplicationPacketId::ActorSpellsActive)
        {
            decoded.lane = MessageLane::Actor;
            decoded.messageType = actorMessage(id, flow);
        }
        else if (id == ApplicationPacketId::PlayerItemUse
            || id == ApplicationPacketId::PlayerCast
            || id == ApplicationPacketId::PlayerAlly
            || id == ApplicationPacketId::PlayerCooldowns)
        {
            decoded.lane = MessageLane::Player;
            decoded.messageType = playerMessage(id, flow);
        }
        else if (between(id, ApplicationPacketId::PlayerBaseInfo,
                     ApplicationPacketId::PlayerTopic))
        {
            decoded.lane = MessageLane::Player;
            decoded.messageType = playerMessage(id, flow);
        }
        else if (between(id, ApplicationPacketId::ActorList,
                     ApplicationPacketId::ActorStatsDynamic))
        {
            decoded.lane = MessageLane::Actor;
            decoded.messageType = actorMessage(id, flow);
        }
        else if (between(id, ApplicationPacketId::ObjectActivate,
                     ApplicationPacketId::ObjectRestock))
        {
            decoded.lane = MessageLane::Object;
            decoded.messageType = objectMessage(id, flow);
        }
        else if (between(id, ApplicationPacketId::GameSettings,
                     ApplicationPacketId::WorldWeather))
        {
            decoded.lane = MessageLane::Worldstate;
            decoded.messageType = toServer ? MessageType::WorldstateIntent
                                           : MessageType::WorldstateUpdate;
        }
        else
            return false;

        if (isUnreliableSnapshot(id))
            decoded.delivery = DeliveryMode::Unreliable;
        route = decoded;
        return true;
    }

    bool encodeApplicationPacket(ApplicationPacketId id,
        ApplicationPacketFlow flow, TransportConnectionId connection, std::uint64_t subject,
        std::uint64_t sequence, std::span<const std::byte> payload,
        TransportMessage& message, protocol::CodecError& error)
    {
        error = protocol::CodecError::None;
        if (!connection || payload.size() > protocol::limits::normalMessageBytes - sizeof(std::uint16_t))
        {
            error = connection ? protocol::CodecError::LimitExceeded
                               : protocol::CodecError::InvalidValue;
            return false;
        }

        ApplicationPacketRoute route;
        if (!applicationPacketRoute(id, flow, route))
        {
            error = protocol::CodecError::InvalidValue;
            return false;
        }
        if (route.delivery == DeliveryMode::Unreliable && sequence == 0)
        {
            error = protocol::CodecError::InvalidValue;
            return false;
        }

        protocol::PacketWriter writer(protocol::limits::normalMessageBytes);
        if (!writer.writeU16(static_cast<std::uint16_t>(id)) || !writer.writeBytes(payload))
        {
            error = writer.error();
            return false;
        }

        TransportMessage encoded;
        encoded.connection = connection;
        encoded.delivery = route.delivery;
        encoded.lane = route.lane;
        encoded.messageType = static_cast<std::uint16_t>(route.messageType);
        encoded.subject = subject;
        encoded.sequence = sequence;
        encoded.payload = writer.take();
        message = std::move(encoded);
        return true;
    }

    protocol::DecodeResult decodeApplicationPacket(const TransportMessage& message,
        ApplicationPacketFlow flow, ApplicationPacket& packet)
    {
        if (!message.connection)
            return { protocol::CodecError::InvalidValue, 0 };

        protocol::PacketReader reader(message.payload);
        std::uint16_t value = 0;
        if (!reader.readU16(value))
            return reader.result();
        if (!protocol::isApplicationPacketId(value))
            return { protocol::CodecError::InvalidValue, reader.position() };

        const auto id = static_cast<protocol::ApplicationPacketId>(value);
        ApplicationPacketRoute route;
        if (!applicationPacketRoute(id, flow, route)
            || message.messageType != static_cast<std::uint16_t>(route.messageType)
            || message.lane != route.lane || message.delivery != route.delivery
            || (route.delivery == DeliveryMode::Unreliable && message.sequence == 0))
            return { protocol::CodecError::InvalidValue, reader.position() };

        ApplicationPacket decoded;
        decoded.id = id;
        decoded.subject = message.subject;
        decoded.sequence = message.sequence;
        try
        {
            decoded.payload.resize(reader.remaining());
        }
        catch (const std::bad_alloc&)
        {
            return { protocol::CodecError::AllocationFailed, reader.position() };
        }
        if (!reader.readBytes(decoded.payload) || !reader.finish())
            return reader.result();
        packet = std::move(decoded);
        return reader.result();
    }
}
