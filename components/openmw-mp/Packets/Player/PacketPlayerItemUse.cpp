#include "PacketPlayerItemUse.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerItemUse::PacketPlayerItemUse() : PlayerPacket()
{
    packetID = ID_PLAYER_ITEM_USE;
}

void PacketPlayerItemUse::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->usedItem.refId, true);
    Field(player->usedItem.count);
    Field(player->usedItem.charge);
    Field(player->usedItem.enchantmentCharge);
    Field(player->usedItem.soul, true);

    Field(player->usingItemMagic);
    Field(player->itemUseDrawState);
}
