#include "LegacyPacketFrame.hpp"

#include <components/openmw-mp/Protocol/ProtocolLimits.hpp>

#include <limits>

namespace mwmp::transport
{
    bool buildLegacyPacketFrame(const ReceivedApplicationPacket& packet,
        std::vector<unsigned char>& frame, protocol::CodecError& error)
    {
        error = protocol::CodecError::None;
        constexpr std::size_t headerBytes = sizeof(std::uint8_t) + sizeof(std::uint64_t);
        if (!protocol::isApplicationPacketId(static_cast<std::uint16_t>(packet.id))
            || packet.id == protocol::ApplicationPacketId::UserMyId
            || packet.payload.size() > protocol::limits::normalMessageBytes - headerBytes)
        {
            error = protocol::CodecError::LimitExceeded;
            return false;
        }

        std::vector<unsigned char> encoded;
        try
        {
            encoded.reserve(headerBytes + packet.payload.size());
            encoded.push_back(static_cast<unsigned char>(packet.id));
            for (std::size_t index = 0; index < sizeof(packet.subject); ++index)
                encoded.push_back(static_cast<unsigned char>(
                    (packet.subject >> (index * 8U)) & 0xffU));
            for (const std::byte value : packet.payload)
                encoded.push_back(std::to_integer<unsigned char>(value));
        }
        catch (const std::bad_alloc&)
        {
            error = protocol::CodecError::AllocationFailed;
            return false;
        }
        frame = std::move(encoded);
        return true;
    }
}
