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

    RW(player->creatureStats.mLevel, send);

    RW(player->npcStats.mLevelProgress, send);
}
