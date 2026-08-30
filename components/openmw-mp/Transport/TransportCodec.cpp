#include "TransportCodec.hpp"

#include <utility>

namespace mwmp::transport
{
    bool encodeTransportMessage(
        const TransportMessage& message, std::vector<std::byte>& destination, protocol::CodecError& error)
    {
        if (!message.connection)
        {
            error = protocol::CodecError::InvalidValue;
            return false;
        }

        std::uint16_t flags = message.flags & ~protocol::envelopeFlagUnreliable;
        if (message.delivery == DeliveryMode::Unreliable)
            flags |= protocol::envelopeFlagUnreliable;

        const protocol::ProtocolEnvelope envelope{
            message.messageType,
            message.subject,
            message.sequence,
            flags,
        };
        return protocol::encodeMessage(envelope, message.payload, destination, error);
    }

    protocol::DecodeResult decodeTransportMessage(std::span<const std::byte> bytes,
        TransportConnectionId connection, MessageLane lane, DeliveryMode delivery, TransportMessage& message)
    {
        if (!connection)
            return { protocol::CodecError::InvalidValue, 0 };

        protocol::ProtocolEnvelope envelope;
        std::span<const std::byte> payload;
        const protocol::DecodeResult result = protocol::decodeMessage(bytes, envelope, payload);
        if (!result)
            return result;

        const bool encodedUnreliable = (envelope.flags & protocol::envelopeFlagUnreliable) != 0;
        if (encodedUnreliable != (delivery == DeliveryMode::Unreliable))
            return { protocol::CodecError::InvalidValue, protocol::envelopeBytes };

        TransportMessage decoded;
        decoded.connection = connection;
        decoded.delivery = delivery;
        decoded.lane = lane;
        decoded.messageType = envelope.messageType;
        decoded.flags = envelope.flags;
        decoded.subject = envelope.subjectId;
        decoded.sequence = envelope.sequence;
        decoded.payload.assign(payload.begin(), payload.end());
        message = std::move(decoded);
        return result;
    }
}
