#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketWorldRegionAuthority.hpp"

mwmp::PacketWorldRegionAuthority::PacketWorldRegionAuthority() : PlayerPacket()
{
    packetID = ID_WORLD_REGION_AUTHORITY;
}

void mwmp::PacketWorldRegionAuthority::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->authorityRegion, true);
}
