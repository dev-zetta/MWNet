#include "PacketWorldCollisionOverride.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketWorldCollisionOverride::PacketWorldCollisionOverride() : WorldstatePacket()
{
    packetID = ID_WORLD_COLLISION_OVERRIDE;
}

void PacketWorldCollisionOverride::Packet(bool send)
{
    WorldstatePacket::Packet(send);

    Field(worldstate->hasPlayerCollision);
    Field(worldstate->hasActorCollision);
    Field(worldstate->hasPlacedObjectCollision);
    Field(worldstate->useActorCollisionForPlacedObjects);

    uint32_t enforcedCollisionCount = 0;

    if (send)
        enforcedCollisionCount = static_cast<uint32_t>(worldstate->enforcedCollisionRefIds.size());

    if (!CollectionSize(enforcedCollisionCount))
        return;

    if (!send)
    {
        worldstate->enforcedCollisionRefIds.clear();
        worldstate->enforcedCollisionRefIds.resize(enforcedCollisionCount);
    }

    for (auto &&enforcedCollisionRefId : worldstate->enforcedCollisionRefIds)
    {
        Field(enforcedCollisionRefId, true);
    }
}
