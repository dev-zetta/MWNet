#ifndef OPENMW_MP_TRANSPORT_APPLICATION_PACKET_BRIDGE_HPP
#define OPENMW_MP_TRANSPORT_APPLICATION_PACKET_BRIDGE_HPP

#include "ITransport.hpp"

#include <components/openmw-mp/Protocol/ApplicationPacketId.hpp>
#include <components/openmw-mp/Protocol/MessageType.hpp>
#include <components/openmw-mp/Protocol/PacketCodec.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace mwmp::transport
{
    enum class ApplicationPacketFlow : std::uint8_t
    {
        ClientToServer,
        ServerToClient,
    };

    struct ApplicationPacketRoute
    {
        protocol::MessageType messageType = protocol::MessageType::Disconnect;
        MessageLane lane = MessageLane::System;
        DeliveryMode delivery = DeliveryMode::ReliableOrdered;
    };

    struct ApplicationPacket
    {
        protocol::ApplicationPacketId id = protocol::ApplicationPacketId::UserMyId;
        std::uint64_t subject = 0;
        std::uint64_t sequence = 0;
        std::vector<std::byte> payload;
    };

    bool applicationPacketRoute(protocol::ApplicationPacketId id,
        ApplicationPacketFlow flow, ApplicationPacketRoute& route) noexcept;

    bool encodeApplicationPacket(protocol::ApplicationPacketId id,
        ApplicationPacketFlow flow, TransportConnectionId connection, std::uint64_t subject,
        std::uint64_t sequence, std::span<const std::byte> payload,
        TransportMessage& message, protocol::CodecError& error);

    protocol::DecodeResult decodeApplicationPacket(const TransportMessage& message,
        ApplicationPacketFlow flow, ApplicationPacket& packet);
}

#endif
