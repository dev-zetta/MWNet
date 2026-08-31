#ifndef OPENMW_PACKETACTORLIST_HPP
#define OPENMW_PACKETACTORLIST_HPP

#include <components/openmw-mp/Packets/Actor/ActorPacket.hpp>

namespace mwmp
{
    class PacketActorList : public ActorPacket
    {
    public:
        PacketActorList();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETACTORLIST_HPP
