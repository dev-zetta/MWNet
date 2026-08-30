#ifndef OPENMW_PACKETSYSTEMHANDSHAKE_HPP
#define OPENMW_PACKETSYSTEMHANDSHAKE_HPP

#include <components/openmw-mp/Packets/System/SystemPacket.hpp>

namespace mwmp
{
    class PacketSystemHandshake : public SystemPacket
    {
    public:
        PacketSystemHandshake(RakNet::RakPeerInterface *peer);

        virtual void Packet(RakNet::BitStream *newBitstream, bool send);

        const static uint32_t maxNameLength = protocol::limits::playerNameBytes;
        const static uint32_t maxPasswordLength = protocol::limits::passwordBytes;
    };
}

#endif //OPENMW_PACKETSYSTEMHANDSHAKE_HPP
