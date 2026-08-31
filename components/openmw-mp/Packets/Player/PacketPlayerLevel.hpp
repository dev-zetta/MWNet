#ifndef OPENMW_PACKETPLAYERLEVEL_HPP
#define OPENMW_PACKETPLAYERLEVEL_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerLevel : public PlayerPacket
    {
    public:
        PacketPlayerLevel();

        virtual void Packet(RakNet::BitStream *newBitstream, bool send);
    };
}

#endif //OPENMW_PACKETPLAYERLEVEL_HPP
