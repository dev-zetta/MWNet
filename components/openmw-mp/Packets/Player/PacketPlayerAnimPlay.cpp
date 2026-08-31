#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerAnimPlay.hpp"

mwmp::PacketPlayerAnimPlay::PacketPlayerAnimPlay() : PlayerPacket()
{
    packetID = ID_PLAYER_ANIM_PLAY;
}

void mwmp::PacketPlayerAnimPlay::Packet(bool send)
{
    PlayerPacket::Packet(send);

    RW(player->animation.groupname, send);
    RW(player->animation.mode, send);
    RW(player->animation.count, send);
    RW(player->animation.persist, send);
}
