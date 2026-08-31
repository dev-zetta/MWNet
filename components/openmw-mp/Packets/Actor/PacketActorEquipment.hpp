#ifndef OPENMW_PACKETACTOREQUIPMENT_HPP
#define OPENMW_PACKETACTOREQUIPMENT_HPP

#include <components/openmw-mp/Packets/Actor/ActorPacket.hpp>

namespace mwmp
{
    class PacketActorEquipment : public ActorPacket
    {
    public:
        PacketActorEquipment();

        virtual void Actor(BaseActor &actor, bool send);
    };
}

#endif //OPENMW_PACKETACTOREQUIPMENT_HPP
