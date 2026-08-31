#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerAnimPlay.hpp"

mwmp::PacketPlayerAnimPlay::PacketPlayerAnimPlay() : PlayerPacket()
{
    packetID = ID_PLAYER_ANIM_PLAY;
}

void mwmp::PacketPlayerAnimPlay::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->animation.groupname);
    Field(player->animation.mode);
    Field(player->animation.count);
    Field(player->animation.persist);
}
