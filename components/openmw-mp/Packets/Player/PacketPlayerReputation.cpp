#include "PacketPlayerReputation.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerReputation::PacketPlayerReputation() : PlayerPacket()
{
    packetID = ID_PLAYER_REPUTATION;
}

void PacketPlayerReputation::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->npcStats.mReputation);
}
