#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketActorAuthority.hpp"

using namespace mwmp;

PacketActorAuthority::PacketActorAuthority() : ActorPacket()
{
    packetID = ID_ACTOR_AUTHORITY;
}

void PacketActorAuthority::Packet(bool send)
{
    BasePacket::Packet(send);

    Field(actorList->cell.mData, true);
    Field(actorList->cell.mName, true);
    Field(actorList->authorityLeaseId);
    Field(actorList->authorityLeaseDurationMs);
}
