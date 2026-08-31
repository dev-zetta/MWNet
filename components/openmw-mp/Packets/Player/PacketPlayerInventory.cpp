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

    Field(player->inventoryChanges.action);

    uint32_t count = 0;

    if (send)
        count = static_cast<uint32_t>(player->inventoryChanges.items.size());

    if (!CollectionSize(count))
        return;

    if (!send)
    {
        player->inventoryChanges.items.clear();
        player->inventoryChanges.items.resize(count);
    }

    for (auto &&item : player->inventoryChanges.items)
    {
        Field(item.refId, true);
        Field(item.count);
        Field(item.charge);
        Field(item.enchantmentCharge);
        Field(item.soul, true);
    }
}
