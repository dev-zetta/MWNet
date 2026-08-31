#include "PacketClientScriptGlobal.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketClientScriptGlobal::PacketClientScriptGlobal() : WorldstatePacket()
{
    packetID = ID_CLIENT_SCRIPT_GLOBAL;
}

void PacketClientScriptGlobal::Packet(bool send)
{
    WorldstatePacket::Packet(send);

    uint32_t clientGlobalsCount = 0;

    if (send)
        clientGlobalsCount = static_cast<uint32_t>(worldstate->clientGlobals.size());

    if (!CollectionSize(clientGlobalsCount))
        return;

    if (!send)
    {
        worldstate->clientGlobals.clear();
        worldstate->clientGlobals.resize(clientGlobalsCount);
    }

    for (auto &&clientGlobal : worldstate->clientGlobals)
    {
        Field(clientGlobal.id, true);
        Field(clientGlobal.variableType);

        if (clientGlobal.variableType == mwmp::VARIABLE_TYPE::SHORT || clientGlobal.variableType == mwmp::VARIABLE_TYPE::LONG)
            Field(clientGlobal.intValue);
        else if (clientGlobal.variableType == mwmp::VARIABLE_TYPE::FLOAT)
            Field(clientGlobal.floatValue);
    }
}
