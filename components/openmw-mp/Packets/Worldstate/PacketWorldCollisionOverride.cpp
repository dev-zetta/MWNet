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

    RW(worldstate->hasPlayerCollision, send);
    RW(worldstate->hasActorCollision, send);
    RW(worldstate->hasPlacedObjectCollision, send);
    RW(worldstate->useActorCollisionForPlacedObjects, send);

    uint32_t enforcedCollisionCount = 0;

    if (send)
        enforcedCollisionCount = static_cast<uint32_t>(worldstate->enforcedCollisionRefIds.size());

    if (!RWCount(enforcedCollisionCount, send))
        return;

    if (!send)
    {
        worldstate->enforcedCollisionRefIds.clear();
        worldstate->enforcedCollisionRefIds.resize(enforcedCollisionCount);
    }

    for (auto &&enforcedCollisionRefId : worldstate->enforcedCollisionRefIds)
    {
        RW(enforcedCollisionRefId, send, true);
    }
}
