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

    RW(player->usedItem.refId, send, true);
    RW(player->usedItem.count, send);
    RW(player->usedItem.charge, send);
    RW(player->usedItem.enchantmentCharge, send);
    RW(player->usedItem.soul, send, true);

    RW(player->usingItemMagic, send);
    RW(player->itemUseDrawState, send);
}
