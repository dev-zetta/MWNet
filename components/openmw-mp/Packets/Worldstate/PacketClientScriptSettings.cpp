#include "PacketClientScriptSettings.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketClientScriptSettings::PacketClientScriptSettings() : WorldstatePacket()
{
    packetID = ID_CLIENT_SCRIPT_SETTINGS;
}

void PacketClientScriptSettings::Packet(bool send)
{
    WorldstatePacket::Packet(send);

    uint32_t clientScriptsCount = 0;

    if (send)
        clientScriptsCount = static_cast<uint32_t>(worldstate->synchronizedClientScriptIds.size());

    if (!CollectionSize(clientScriptsCount))
        return;

    if (!send)
    {
        worldstate->synchronizedClientScriptIds.clear();
        worldstate->synchronizedClientScriptIds.resize(clientScriptsCount);
    }

    for (auto &&clientScriptId : worldstate->synchronizedClientScriptIds)
    {
        Field(clientScriptId, true);
    }

    uint32_t clientGlobalsCount = 0;

    if (send)
        clientGlobalsCount = static_cast<uint32_t>(worldstate->synchronizedClientGlobalIds.size());

    if (!CollectionSize(clientGlobalsCount))
        return;

    if (!send)
    {
        worldstate->synchronizedClientGlobalIds.clear();
        worldstate->synchronizedClientGlobalIds.resize(clientGlobalsCount);
    }

    for (auto &&clientGlobalId : worldstate->synchronizedClientGlobalIds)
    {
        Field(clientGlobalId, true);
    }
}
