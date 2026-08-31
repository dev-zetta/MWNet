#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketActorList.hpp"

using namespace mwmp;

PacketActorList::PacketActorList() : ActorPacket()
{
    packetID = ID_ACTOR_LIST;
}

void PacketActorList::Packet(bool send)
{
    if (!ActorPacket::PacketHeader(send))
        return;

    Field(actorList->action);

    BaseActor actor;

    for (unsigned int i = 0; i < actorList->count; i++)
    {
        if (send)
            actor = actorList->baseActors.at(i);

        Field(actor.refId);
        Field(actor.refNum);
        Field(actor.mpNum);

        if (actor.refId.empty() || (actor.refNum != 0 && actor.mpNum != 0))
        {
            actorList->isValid = false;
            return;
        }

        if (!send)
            actorList->baseActors.push_back(actor);
    }
}
