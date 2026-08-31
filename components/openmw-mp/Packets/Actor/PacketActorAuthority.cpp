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

    RW(actorList->cell.mData, send, true);
    RW(actorList->cell.mName, send, true);
    RW(actorList->authorityLeaseId, send);
    RW(actorList->authorityLeaseDurationMs, send);
}
