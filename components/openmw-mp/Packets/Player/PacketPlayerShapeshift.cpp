#include "PacketPlayerShapeshift.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerShapeshift::PacketPlayerShapeshift() : PlayerPacket()
{
    packetID = ID_PLAYER_SHAPESHIFT;
}

void PacketPlayerShapeshift::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->scale);
    Field(player->isWerewolf);

    Field(player->displayCreatureName);
    Field(player->creatureRefId, true);
}
