#include "PacketPlayerEquipment.hpp"

#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerEquipment::PacketPlayerEquipment() : PlayerPacket()
{
    packetID = ID_PLAYER_EQUIPMENT;
}

void PacketPlayerEquipment::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->exchangeFullInfo);

    if (player->exchangeFullInfo)
    {
        for (auto &&equipmentItem : player->equipmentItems)
        {
            ExchangeItemInformation(equipmentItem, send);
        }
    }
    else
    {
        uint32_t count = 0;
        if (send)
            count = static_cast<uint32_t>(player->equipmentIndexChanges.size());

        if (!CollectionSize(count, 19))
            return;

        if (!send)
        {
            player->equipmentIndexChanges.clear();
            player->equipmentIndexChanges.resize(count);
        }

        for (auto &&equipmentIndex : player->equipmentIndexChanges)
        {
            Field(equipmentIndex);
            if (!packetValid || equipmentIndex < 0 || equipmentIndex >= 19)
            {
                invalidate(protocol::CodecError::InvalidValue);
                return;
            }
            ExchangeItemInformation(player->equipmentItems[equipmentIndex], send);
        }
    }
}

void PacketPlayerEquipment::ExchangeItemInformation(Item &item, bool send)
{
    Field(item.refId, true);
    Field(item.count);
    Field(item.charge);
    Field(item.enchantmentCharge);
}
