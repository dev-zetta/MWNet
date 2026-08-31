#ifndef OPENMW_PACKETWORLDDESTINATIONOVERRIDE_HPP
#define OPENMW_PACKETWORLDDESTINATIONOVERRIDE_HPP

#include <components/openmw-mp/Packets/Worldstate/WorldstatePacket.hpp>

namespace mwmp
{
    class PacketWorldDestinationOverride : public WorldstatePacket
    {
    public:
        PacketWorldDestinationOverride();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETWORLDDESTINATIONOVERRIDE_HPP
