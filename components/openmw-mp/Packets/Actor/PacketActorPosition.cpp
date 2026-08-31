#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketActorPosition.hpp"

using namespace mwmp;

PacketActorPosition::PacketActorPosition() : ActorPacket()
{
    packetID = ID_ACTOR_POSITION;
}

void PacketActorPosition::Actor(BaseActor &actor, bool send)
{
    Field(actor.position, true);
    Field(actor.direction, true);

    actor.hasPositionData = true;
}
