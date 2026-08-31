#ifndef OPENMW_ACTORPACKET_HPP
#define OPENMW_ACTORPACKET_HPP

#include <string>
#include <components/openmw-mp/Base/BaseActor.hpp>

#include <components/openmw-mp/Packets/BasePacket.hpp>


namespace mwmp
{
    class ActorPacket : public BasePacket
    {
    public:
        ActorPacket();

        ~ActorPacket();

        void setActorList(BaseActorList *newActorList);

        virtual void Packet(bool send);
    protected:
        bool PacketHeader(bool send);
        virtual void Actor(BaseActor &actor, bool send);
        BaseActorList *actorList = nullptr;
        static const int maxActors = 3000;
    };
}

#endif //OPENMW_ACTORPACKET_HPP
