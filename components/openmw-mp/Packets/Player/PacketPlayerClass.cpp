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

void mwmp::PacketPlayerClass::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->charClass.mId);

    if (player->charClass.mId.empty()) // custom class
    {
        Field(player->charClass.mName, true);
        Field(player->charClass.mDescription, true);

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
            if (!Field(attribute))
                return;
        }
        if (!Field(data.mSpecialization))
            return;
        for (auto& skillPair : data.mSkills)
        {
            for (auto& skill : skillPair)
            {
                if (!Field(skill))
                    return;
            }
        }
        if (!Field(data.mIsPlayable) || !Field(data.mServices))
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
