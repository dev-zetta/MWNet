#ifndef OPENMW_MP_TRANSPORT_LEGACY_PACKET_FRAME_HPP
#define OPENMW_MP_TRANSPORT_LEGACY_PACKET_FRAME_HPP

#include "ApplicationPacketReceiver.hpp"

#include <vector>

namespace mwmp::transport
{
    bool buildLegacyPacketFrame(const ReceivedApplicationPacket& packet,
        std::vector<unsigned char>& frame, protocol::CodecError& error);
}

#endif
