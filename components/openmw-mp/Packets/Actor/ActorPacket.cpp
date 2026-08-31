#include <components/openmw-mp/NetworkMessages.hpp>
#include "ActorPacket.hpp"

using namespace mwmp;

ActorPacket::ActorPacket() : BasePacket()
{
    packetID = 0;
}

ActorPacket::~ActorPacket()
{

}

void ActorPacket::setActorList(BaseActorList *newActorList)
{
    actorList = newActorList;
    guid = actorList->guid;
}

void ActorPacket::Packet(bool send)
{
    if (!PacketHeader(send))
        return;

    BaseActor actor;

    for (unsigned int i = 0; i < actorList->count; i++)
    {
        if (send)
            actor = actorList->baseActors.at(i);

        Field(actor.refNum);
        Field(actor.mpNum);

        Actor(actor, send);

        if (!send)
            actorList->baseActors.push_back(actor);
    }
}

bool ActorPacket::PacketHeader(bool send)
{
    BasePacket::Packet(send);
    if (!packetValid || actorList == nullptr)
    {
        invalidate(protocol::CodecError::InvalidValue);
        return false;
    }

    Field(actorList->cell.mData, true);
    Field(actorList->cell.mName, true);
    Field(actorList->authorityLeaseId);

    if (send)
        actorList->count = (unsigned int)(actorList->baseActors.size());
    else
        actorList->baseActors.clear();

    if (!CollectionSize(actorList->count, protocol::limits::actorChanges))
    {
        actorList->isValid = false;
        return false;
    }

    return true;
}


void ActorPacket::Actor(BaseActor &actor, bool send)
{

}
