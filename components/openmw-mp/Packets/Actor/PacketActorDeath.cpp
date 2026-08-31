#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketActorDeath.hpp"

using namespace mwmp;

PacketActorDeath::PacketActorDeath() : ActorPacket()
{
    packetID = ID_ACTOR_DEATH;
}

void PacketActorDeath::Actor(BaseActor &actor, bool send)
{
    Field(actor.refId);

    Field(actor.deathState);
    Field(actor.isInstantDeath);
    Field(actor.killer.isPlayer);

    if (actor.killer.isPlayer)
    {
        Field(actor.killer.guid);
    }
    else
    {
        Field(actor.killer.refId, true);
        Field(actor.killer.refNum);
        Field(actor.killer.mpNum);

        Field(actor.killer.name, true);
    }
}
