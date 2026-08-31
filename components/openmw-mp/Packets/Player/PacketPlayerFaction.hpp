#ifndef OPENMW_PACKETPLAYERFACTION_HPP
#define OPENMW_PACKETPLAYERFACTION_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerFaction : public PlayerPacket
    {
    public:
        PacketPlayerFaction();

        virtual void Packet(RakNet::BitStream *newBitstream, bool send);
    };
}

#endif //OPENMW_PACKETPLAYERFACTION_HPP
