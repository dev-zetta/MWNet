#include "PacketPlayerSkill.hpp"

#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/esm3/creaturestats.hpp>
#include <components/esm3/loadskil.hpp>

using namespace mwmp;

PacketPlayerSkill::PacketPlayerSkill() : PlayerPacket()
{
    packetID = ID_PLAYER_SKILL;
}

void PacketPlayerSkill::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->exchangeFullInfo);

    if (player->exchangeFullInfo)
    {
        for (int skillIndex = 0; skillIndex < ESM::Skill::Length; ++skillIndex)
            Field(player->npcStats.mSkills[ESM::Skill::indexToRefId(skillIndex)]);
    }
    else
    {
        uint32_t count = 0;

        if (send)
            count = static_cast<uint32_t>(player->skillIndexChanges.size());

        if (!CollectionSize(count, ESM::Skill::Length))
            return;

        if (!send)
        {
            player->skillIndexChanges.clear();
            player->skillIndexChanges.resize(count);
        }

        for (auto &&skillId : player->skillIndexChanges)
        {
            Field(skillId);
            if (skillId >= 27)
            {
                packetValid = false;
                return;
            }
            Field(player->npcStats.mSkills[ESM::Skill::indexToRefId(skillId)]);
        }
    }
}
