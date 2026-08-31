#include "PacketPlayerLevel.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerLevel::PacketPlayerLevel() : PlayerPacket()
{
    packetID = ID_PLAYER_LEVEL;
}

void PacketPlayerLevel::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->creatureStats.mLevel);

    Field(player->npcStats.mLevelProgress);
}
