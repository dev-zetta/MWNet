#include "PacketPlayerDeath.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerDeath::PacketPlayerDeath() : PlayerPacket()
{
    packetID = ID_PLAYER_DEATH;
}

void PacketPlayerDeath::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->deathState);
    Field(player->killer.isPlayer);

    if (player->killer.isPlayer)
    {
        Field(player->killer.guid);
    }
    else
    {
        Field(player->killer.refId, true);
        Field(player->killer.refNum);
        Field(player->killer.mpNum);

        Field(player->killer.name, true);
    }
}
