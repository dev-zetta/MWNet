#include "PacketPlayerBounty.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerBounty::PacketPlayerBounty() : PlayerPacket()
{
    packetID = ID_PLAYER_BOUNTY;
}

void PacketPlayerBounty::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->npcStats.mBounty);
}
