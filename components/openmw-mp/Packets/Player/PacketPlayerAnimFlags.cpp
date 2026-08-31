#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerAnimFlags.hpp"

mwmp::PacketPlayerAnimFlags::PacketPlayerAnimFlags() : PlayerPacket()
{
    packetID = ID_PLAYER_ANIM_FLAGS;
}

void mwmp::PacketPlayerAnimFlags::Packet(bool send)
{
    PlayerPacket::Packet(send);

    RW(player->movementFlags, send);
    RW(player->drawState, send);
    RW(player->isJumping, send);
    RW(player->isFlying, send);
    RW(player->hasTcl, send);
}
