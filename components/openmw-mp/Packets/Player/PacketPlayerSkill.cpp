#include "PacketPlayerSkill.hpp"

#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/esm3/creaturestats.hpp>
#include <components/esm3/loadskil.hpp>

using namespace mwmp;

PacketPlayerSkill::PacketPlayerSkill(RakNet::RakPeerInterface *peer) : PlayerPacket(peer)
{
    packetID = ID_PLAYER_SKILL;
}

void PacketPlayerSkill::Packet(RakNet::BitStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    RW(player->exchangeFullInfo, send);

    if (player->exchangeFullInfo)
    {
        for (int skillIndex = 0; skillIndex < ESM::Skill::Length; ++skillIndex)
            RW(player->npcStats.mSkills[ESM::Skill::indexToRefId(skillIndex)], send);
    }
    else
    {
        uint32_t count = 0;

        if (send)
            count = static_cast<uint32_t>(player->skillIndexChanges.size());

        if (!RWCount(count, send, ESM::Skill::Length))
            return;

        if (!send)
        {
            player->skillIndexChanges.clear();
            player->skillIndexChanges.resize(count);
        }

        for (auto &&skillId : player->skillIndexChanges)
        {
            RW(skillId, send);
            if (skillId >= 27)
            {
                packetValid = false;
                return;
            }
            RW(player->npcStats.mSkills[ESM::Skill::indexToRefId(skillId)], send);
        }
    }
}
