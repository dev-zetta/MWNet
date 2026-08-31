#ifndef OPENMW_PACKETPLAYERPOSITION_HPP
#define OPENMW_PACKETPLAYERPOSITION_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerPosition : public PlayerPacket
    {
    public:
        PacketPlayerPosition();

        virtual void Packet(RakNet::BitStream *newBitstream, bool send);
    };
}

#endif //OPENMW_PACKETPLAYERPOSITION_HPP
