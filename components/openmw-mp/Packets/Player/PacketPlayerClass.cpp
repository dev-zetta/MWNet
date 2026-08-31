#include <array>
#include <cstdint>

#include <components/esm/attr.hpp>
#include <components/esm3/loadskil.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketPlayerClass.hpp"

namespace
{
    struct LegacyClassData
    {
        std::array<std::int32_t, 2> mAttributes;
        std::int32_t mSpecialization;
        std::array<std::array<std::int32_t, 2>, 5> mSkills;
        std::int32_t mIsPlayable;
        std::int32_t mServices;
    };

}

mwmp::PacketPlayerClass::PacketPlayerClass() : PlayerPacket()
{
    packetID = ID_PLAYER_CHARCLASS;
}

void mwmp::PacketPlayerClass::Packet(RakNet::BitStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream,  send);

    RW(player->charClass.mId, send);

    if (player->charClass.mId.empty()) // custom class
    {
        RW(player->charClass.mName, send, true);
        RW(player->charClass.mDescription, send, true);

        LegacyClassData data{};
        if (send)
        {
            for (std::size_t i = 0; i < data.mAttributes.size(); ++i)
                data.mAttributes[i] = ESM::Attribute::refIdToIndex(player->charClass.mData.mAttribute[i]);
            data.mSpecialization = player->charClass.mData.mSpecialization;
            for (std::size_t i = 0; i < data.mSkills.size(); ++i)
                for (std::size_t j = 0; j < data.mSkills[i].size(); ++j)
                    data.mSkills[i][j] = ESM::Skill::refIdToIndex(player->charClass.mData.mSkills[i][j]);
            data.mIsPlayable = player->charClass.mData.mIsPlayable;
            data.mServices = player->charClass.mData.mServices;
        }

        for (auto& attribute : data.mAttributes)
        {
            if (!RW(attribute, send))
                return;
        }
        if (!RW(data.mSpecialization, send))
            return;
        for (auto& skillPair : data.mSkills)
        {
            for (auto& skill : skillPair)
            {
                if (!RW(skill, send))
                    return;
            }
        }
        if (!RW(data.mIsPlayable, send) || !RW(data.mServices, send))
            return;

        if (!send)
        {
            for (std::size_t i = 0; i < data.mAttributes.size(); ++i)
                player->charClass.mData.mAttribute[i] = ESM::Attribute::indexToRefId(data.mAttributes[i]);
            player->charClass.mData.mSpecialization = data.mSpecialization;
            for (std::size_t i = 0; i < data.mSkills.size(); ++i)
                for (std::size_t j = 0; j < data.mSkills[i].size(); ++j)
                    player->charClass.mData.mSkills[i][j] = ESM::Skill::indexToRefId(data.mSkills[i][j]);
            player->charClass.mData.mIsPlayable = data.mIsPlayable != 0;
            player->charClass.mData.mServices = data.mServices;
        }
    }
}
