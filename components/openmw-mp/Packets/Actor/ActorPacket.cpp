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

void ActorPacket::Packet(RakNet::BitStream *newBitstream, bool send)
{
    if (!PacketHeader(newBitstream, send))
        return;

    BaseActor actor;

    for (unsigned int i = 0; i < actorList->count; i++)
    {
        if (send)
            actor = actorList->baseActors.at(i);

        RW(actor.refNum, send);
        RW(actor.mpNum, send);

        Actor(actor, send);

        if (!send)
            actorList->baseActors.push_back(actor);
    }
}

bool ActorPacket::PacketHeader(RakNet::BitStream *newBitstream, bool send)
{
    BasePacket::Packet(newBitstream, send);
    if (!packetValid || actorList == nullptr)
    {
        invalidate(protocol::CodecError::InvalidValue);
        return false;
    }

    RW(actorList->cell.mData, send, true);
    RW(actorList->cell.mName, send, true);
    RW(actorList->authorityLeaseId, send);

    if (send)
        actorList->count = (unsigned int)(actorList->baseActors.size());
    else
        actorList->baseActors.clear();

    if (!RWCount(actorList->count, send, protocol::limits::actorChanges))
    {
        actorList->isValid = false;
        return false;
    }

    return true;
}


void ActorPacket::Actor(BaseActor &actor, bool send)
{

}
