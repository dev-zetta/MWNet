#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerInventory.hpp"

using namespace mwmp;

PacketPlayerInventory::PacketPlayerInventory() : PlayerPacket()
{
    packetID = ID_PLAYER_INVENTORY;
}

void PacketPlayerInventory::Packet(bool send)
{
    PlayerPacket::Packet(send);

    RW(player->inventoryChanges.action, send);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->inventoryChanges.items.size());

    if (!RWCount(count, send))
        return;

    if (!send)
    {
        player->inventoryChanges.items.clear();
        player->inventoryChanges.items.resize(count);
    }

    for (auto &&item : player->inventoryChanges.items)
    {
        RW(item.refId, send, true);
        RW(item.count, send);
        RW(item.charge, send);
        RW(item.enchantmentCharge, send);
        RW(item.soul, send, true);
    }
}
