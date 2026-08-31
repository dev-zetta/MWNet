#ifndef OPENMW_MP_TRANSPORT_APPLICATION_PACKET_FRAME_HPP
#define OPENMW_MP_TRANSPORT_APPLICATION_PACKET_FRAME_HPP

#include "ITransport.hpp"

#include <cstddef>

namespace mwmp::transport
{
    // Temporary adapter used while the legacy packet classes are migrated to
    // consume protocol-11 payload spans directly. Sender identity always comes
    // from the authenticated transport connection, never from packet bytes.
    struct ApplicationPacketFrame
    {
        unsigned char* data = nullptr;
        std::size_t length = 0;
        TransportConnectionId sender;
    };
}

#endif
