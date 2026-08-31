#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketWorldRegionAuthority.hpp"

mwmp::PacketWorldRegionAuthority::PacketWorldRegionAuthority() : PlayerPacket()
{
    packetID = ID_WORLD_REGION_AUTHORITY;
}

void mwmp::PacketWorldRegionAuthority::Packet(RakNet::BitStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    RW(player->authorityRegion, send, true);
}
