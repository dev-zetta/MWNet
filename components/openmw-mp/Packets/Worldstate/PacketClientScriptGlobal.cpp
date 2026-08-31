#include "PacketClientScriptGlobal.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketClientScriptGlobal::PacketClientScriptGlobal() : WorldstatePacket()
{
    packetID = ID_CLIENT_SCRIPT_GLOBAL;
}

void PacketClientScriptGlobal::Packet(RakNet::BitStream *newBitstream, bool send)
{
    WorldstatePacket::Packet(newBitstream, send);

    uint32_t clientGlobalsCount = 0;

    if (send)
        clientGlobalsCount = static_cast<uint32_t>(worldstate->clientGlobals.size());

    if (!RWCount(clientGlobalsCount, send))
        return;

    if (!send)
    {
        worldstate->clientGlobals.clear();
        worldstate->clientGlobals.resize(clientGlobalsCount);
    }

    for (auto &&clientGlobal : worldstate->clientGlobals)
    {
        RW(clientGlobal.id, send, true);
        RW(clientGlobal.variableType, send);

        if (clientGlobal.variableType == mwmp::VARIABLE_TYPE::SHORT || clientGlobal.variableType == mwmp::VARIABLE_TYPE::LONG)
            RW(clientGlobal.intValue, send);
        else if (clientGlobal.variableType == mwmp::VARIABLE_TYPE::FLOAT)
            RW(clientGlobal.floatValue, send);
    }
}
