#include "PacketGameSettings.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketGameSettings::PacketGameSettings() : PlayerPacket()
{
    packetID = ID_GAME_SETTINGS;
}

void PacketGameSettings::Packet(bool send)
{
    PlayerPacket::Packet(send);

    Field(player->difficulty);
    Field(player->consoleAllowed);
    Field(player->bedRestAllowed);
    Field(player->wildernessRestAllowed);
    Field(player->waitAllowed);
    Field(player->enforcedLogLevel);
    Field(player->physicsFramerate);

    std::string mapIndex;
    std::string mapValue;

    uint32_t gameSettingCount = static_cast<uint32_t>(player->gameSettings.size());
    if (!CollectionSize(gameSettingCount))
        return;

    if (send)
    {
        for (auto&& gameSetting : player->gameSettings)
        {
            mapIndex = gameSetting.first;
            mapValue = gameSetting.second;
            Field(mapIndex, false);
            Field(mapValue, false);
        }
    }
    else
    {
        player->gameSettings.clear();
        for (unsigned int n = 0; n < gameSettingCount; n++)
        {
            Field(mapIndex, false);
            Field(mapValue, false);
            player->gameSettings[mapIndex] = mapValue;
        }
    }

    uint32_t vrSettingCount = static_cast<uint32_t>(player->vrSettings.size());
    if (!CollectionSize(vrSettingCount))
        return;

    if (send)
    {
        for (auto&& vrSetting : player->vrSettings)
        {
            mapIndex = vrSetting.first;
            mapValue = vrSetting.second;
            Field(mapIndex, false);
            Field(mapValue, false);
        }
    }
    else
    {
        player->vrSettings.clear();
        for (unsigned int n = 0; n < vrSettingCount; n++)
        {
            Field(mapIndex, false);
            Field(mapValue, false);
            player->vrSettings[mapIndex] = mapValue;
        }
    }
}
