#ifndef OPENMW_MP_TRANSPORT_TRANSPORT_CODEC_HPP
#define OPENMW_MP_TRANSPORT_TRANSPORT_CODEC_HPP

#include "ITransport.hpp"

#include <components/openmw-mp/Protocol/PacketCodec.hpp>

#include <span>
#include <vector>

namespace mwmp::transport
{
    bool encodeTransportMessage(
        const TransportMessage& message, std::vector<std::byte>& destination, protocol::CodecError& error);
    protocol::DecodeResult decodeTransportMessage(std::span<const std::byte> bytes,
        TransportConnectionId connection, MessageLane lane, DeliveryMode delivery, TransportMessage& message);
}

#endif
