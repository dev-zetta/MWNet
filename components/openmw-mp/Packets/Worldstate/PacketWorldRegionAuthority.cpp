#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketWorldRegionAuthority.hpp"

mwmp::PacketWorldRegionAuthority::PacketWorldRegionAuthority() : WorldstatePacket()
{
    packetID = ID_WORLD_REGION_AUTHORITY;
}

void mwmp::PacketWorldRegionAuthority::Packet(bool send)
{
    WorldstatePacket::Packet(send);

    Field(worldstate->authorityRegion, true);
}
