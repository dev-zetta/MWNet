#ifndef OPENMW_PACKETACTORCAST_HPP
#define OPENMW_PACKETACTORCAST_HPP

#include <components/openmw-mp/Packets/Actor/ActorPacket.hpp>

namespace mwmp
{
    class PacketActorCast : public ActorPacket
    {
    public:
        PacketActorCast();

        virtual void Actor(BaseActor &actor, bool send);
    };
}

#endif //OPENMW_PACKETACTORCAST_HPP
