#include "PacketPlayerAttribute.hpp"

#include <components/esm/attr.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerAttribute::PacketPlayerAttribute() : PlayerPacket()
{
    packetID = ID_PLAYER_ATTRIBUTE;
}

void PacketPlayerAttribute::Packet(bool send)
{
    PlayerPacket::Packet(send);

    RW(player->exchangeFullInfo, send);

    if (player->exchangeFullInfo)
    {
        for (int attributeIndex = 0; attributeIndex < ESM::Attribute::Length; ++attributeIndex)
            RW(player->creatureStats.mAttributes[ESM::Attribute::indexToRefId(attributeIndex)], send);
        for (int attributeIndex = 0; attributeIndex < ESM::Attribute::Length; ++attributeIndex)
            RW(player->npcStats.mSkillIncrease[ESM::Attribute::indexToRefId(attributeIndex)], send);
    }
    else
    {
        uint32_t count = 0;

        if (send)
            count = static_cast<uint32_t>(player->attributeIndexChanges.size());

        if (!RWCount(count, send, ESM::Attribute::Length))
            return;

        if (!send)
        {
            player->attributeIndexChanges.clear();
            player->attributeIndexChanges.resize(count);
        }

        for (auto &&attributeIndex : player->attributeIndexChanges)
        {
            RW(attributeIndex, send);

            if (attributeIndex >= 8)
            {
                packetValid = false;
                return;
            }

            const ESM::RefId attributeId = ESM::Attribute::indexToRefId(attributeIndex);
            RW(player->creatureStats.mAttributes[attributeId], send);
            RW(player->npcStats.mSkillIncrease[attributeId], send);
        }
    }
}
