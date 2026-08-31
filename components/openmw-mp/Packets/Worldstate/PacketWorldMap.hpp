#ifndef OPENMW_PACKETWORLDMAP_HPP
#define OPENMW_PACKETWORLDMAP_HPP

#include <components/openmw-mp/Packets/Worldstate/WorldstatePacket.hpp>

namespace mwmp
{
    class PacketWorldMap : public WorldstatePacket
    {
    public:
        PacketWorldMap();

        virtual void Packet(RakNet::BitStream *newBitstream, bool send);
    };
}

#endif //OPENMW_PACKETWORLDMAP_HPP
