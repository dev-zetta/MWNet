#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketWorldRegionAuthority.hpp"

mwmp::PacketWorldRegionAuthority::PacketWorldRegionAuthority() : WorldstatePacket()
{
    packetID = ID_WORLD_REGION_AUTHORITY;
}

void mwmp::PacketWorldRegionAuthority::Packet(RakNet::BitStream *newBitstream, bool send)
{
    WorldstatePacket::Packet(newBitstream, send);

    RW(worldstate->authorityRegion, send, true);
}
