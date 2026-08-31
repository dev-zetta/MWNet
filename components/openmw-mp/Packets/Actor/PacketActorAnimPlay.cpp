#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketActorAnimPlay.hpp"

using namespace mwmp;

PacketActorAnimPlay::PacketActorAnimPlay() : ActorPacket()
{
    packetID = ID_ACTOR_ANIM_PLAY;
}

void PacketActorAnimPlay::Actor(BaseActor &actor, bool send)
{

    Field(actor.animation.groupname);
    Field(actor.animation.mode);
    Field(actor.animation.count);
    Field(actor.animation.persist);
}
