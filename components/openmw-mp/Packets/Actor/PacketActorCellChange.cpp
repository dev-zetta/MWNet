#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include "PacketActorCellChange.hpp"

using namespace mwmp;

PacketActorCellChange::PacketActorCellChange() : ActorPacket()
{
    packetID = ID_ACTOR_CELL_CHANGE;
}

void PacketActorCellChange::Actor(BaseActor &actor, bool send)
{
    Field(actor.cell.mData, true);
    Field(actor.cell.mName, true);

    Field(actor.position, true);
    Field(actor.direction, true);

    Field(actor.isFollowerCellChange);
}
