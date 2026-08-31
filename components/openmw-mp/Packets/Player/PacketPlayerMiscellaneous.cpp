#include "PacketPlayerMiscellaneous.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerMiscellaneous::PacketPlayerMiscellaneous() : PlayerPacket()
{
    packetID = ID_PLAYER_MISCELLANEOUS;
}

void PacketPlayerMiscellaneous::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->miscellaneousChangeType);

    if (player->miscellaneousChangeType == mwmp::MISCELLANEOUS_CHANGE_TYPE::MARK_LOCATION)
    {
        Field(player->markCell.mData, true);
        Field(player->markCell.mName, true);

        Field(player->markPosition.pos);
        Field(player->markPosition.rot[0]);
        Field(player->markPosition.rot[2]);
    }
    else if (player->miscellaneousChangeType == mwmp::MISCELLANEOUS_CHANGE_TYPE::SELECTED_SPELL)
        Field(player->selectedSpellId, true);
}
