#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerCharGen.hpp"

mwmp::PacketPlayerCharGen::PacketPlayerCharGen() : PlayerPacket()
{
    packetID = ID_PLAYER_CHARGEN;
}

void mwmp::PacketPlayerCharGen::Packet(bool send)
{
    PlayerPacket::Packet(send);

    BasePlayer::CharGenState decoded = player->charGenState;
    auto& target = send ? player->charGenState : decoded;
    if (!RW(target.currentStage, send) || !RW(target.endStage, send) || !RW(target.isFinished, send))
        return;
    if (!send)
        player->charGenState = decoded;
}
