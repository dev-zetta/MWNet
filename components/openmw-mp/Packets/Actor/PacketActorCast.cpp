#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketActorCast.hpp"

using namespace mwmp;

PacketActorCast::PacketActorCast() : ActorPacket()
{
    packetID = ID_ACTOR_CAST;
}

void PacketActorCast::Actor(BaseActor &actor, bool send)
{
    Field(actor.cast.target.isPlayer);

    if (actor.cast.target.isPlayer)
    {
        Field(actor.cast.target.guid);
    }
    else
    {
        Field(actor.cast.target.refId, true);
        Field(actor.cast.target.refNum);
        Field(actor.cast.target.mpNum);
    }

    Field(actor.cast.type);

    if (actor.cast.type == mwmp::Cast::ITEM)
        Field(actor.cast.itemId, true);
    else
    {
        Field(actor.cast.pressed);
        Field(actor.cast.success);

        Field(actor.cast.instant);
        Field(actor.cast.spellId, true);
    }

    Field(actor.cast.hasProjectile);

    if (actor.cast.hasProjectile)
    {
        Field(actor.cast.projectileOrigin.origin[0]);
        Field(actor.cast.projectileOrigin.origin[1]);
        Field(actor.cast.projectileOrigin.origin[2]);
        Field(actor.cast.projectileOrigin.orientation[0]);
        Field(actor.cast.projectileOrigin.orientation[1]);
        Field(actor.cast.projectileOrigin.orientation[2]);
        Field(actor.cast.projectileOrigin.orientation[3]);
    }
}
