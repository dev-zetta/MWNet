#include <components/openmw-mp/NetworkMessages.hpp>
#include <PacketPriority.h>
#include "PlayerPacket.hpp"

using namespace mwmp;

PlayerPacket::PlayerPacket() : BasePacket()
{
    packetID = 0;
    priority = HIGH_PRIORITY;
    reliability = RELIABLE_ORDERED;
    orderChannel = CHANNEL_PLAYER;
}

PlayerPacket::~PlayerPacket()
{

}

void PlayerPacket::setPlayer(BasePlayer *newPlayer)
{
    player = newPlayer;
    guid = player->guid;
}

BasePlayer *PlayerPacket::getPlayer()
{
    return player;
}
