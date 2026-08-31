#include <components/openmw-mp/NetworkMessages.hpp>
#include "PlayerPacket.hpp"

using namespace mwmp;

PlayerPacket::PlayerPacket() : BasePacket()
{
    packetID = 0;
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

bool PlayerPacket::beginDecodeTransaction()
{
    return mDecodeTransaction.begin(player);
}

void PlayerPacket::commitDecodeTransaction() noexcept
{
    mDecodeTransaction.commit(player);
}

void PlayerPacket::rollbackDecodeTransaction() noexcept
{
    mDecodeTransaction.rollback(player);
}
