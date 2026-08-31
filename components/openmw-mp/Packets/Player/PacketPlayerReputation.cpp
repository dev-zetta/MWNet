#include "PacketPlayerReputation.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerReputation::PacketPlayerReputation() : PlayerPacket()
{
    packetID = ID_PLAYER_REPUTATION;
}

void PacketPlayerReputation::Packet(RakNet::BitStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    RW(player->npcStats.mReputation, send);
}
