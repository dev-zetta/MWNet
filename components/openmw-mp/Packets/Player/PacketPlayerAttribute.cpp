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

    Field(player->exchangeFullInfo);

    if (player->exchangeFullInfo)
    {
        for (int attributeIndex = 0; attributeIndex < ESM::Attribute::Length; ++attributeIndex)
            Field(player->creatureStats.mAttributes[ESM::Attribute::indexToRefId(attributeIndex)]);
        for (int attributeIndex = 0; attributeIndex < ESM::Attribute::Length; ++attributeIndex)
            Field(player->npcStats.mSkillIncrease[ESM::Attribute::indexToRefId(attributeIndex)]);
    }
    else
    {
        uint32_t count = 0;

        if (send)
            count = static_cast<uint32_t>(player->attributeIndexChanges.size());

        if (!CollectionSize(count, ESM::Attribute::Length))
            return;

        if (!send)
        {
            player->attributeIndexChanges.clear();
            player->attributeIndexChanges.resize(count);
        }

        for (auto &&attributeIndex : player->attributeIndexChanges)
        {
            Field(attributeIndex);

            if (attributeIndex >= 8)
            {
                packetValid = false;
                return;
            }

            const ESM::RefId attributeId = ESM::Attribute::indexToRefId(attributeIndex);
            Field(player->creatureStats.mAttributes[attributeId]);
            Field(player->npcStats.mSkillIncrease[attributeId]);
        }
    }
}
