#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerAnimFlags.hpp"

mwmp::PacketPlayerAnimFlags::PacketPlayerAnimFlags() : PlayerPacket()
{
    packetID = ID_PLAYER_ANIM_FLAGS;
}

void mwmp::PacketPlayerAnimFlags::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->movementFlags);
    Field(player->drawState);
    Field(player->isJumping);
    Field(player->isFlying);
    Field(player->hasTcl);
}
