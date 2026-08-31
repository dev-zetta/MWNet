#include "PacketPlayerBaseInfo.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerBaseInfo::PacketPlayerBaseInfo() : PlayerPacket()
{
    packetID = ID_PLAYER_BASEINFO;
}

void PacketPlayerBaseInfo::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->npc.mName, true);
    Field(player->npc.mModel, true);
    Field(player->npc.mRace, true);
    Field(player->npc.mHair, true);
    Field(player->npc.mHead, true);

    Field(player->npc.mFlags);

    Field(player->birthsign, true);

    Field(player->resetStats);
}
