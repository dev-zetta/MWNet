#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketClientScriptLocal.hpp"

using namespace mwmp;

PacketClientScriptLocal::PacketClientScriptLocal() : ObjectPacket()
{
    packetID = ID_CLIENT_SCRIPT_LOCAL;
    hasCellData = true;
}

void PacketClientScriptLocal::Object(BaseObject &baseObject, bool send)
{
    ObjectPacket::Object(baseObject, send);

    uint32_t clientLocalsCount = 0;

    if (send)
        clientLocalsCount = static_cast<uint32_t>(baseObject.clientLocals.size());

    if (!CollectionSize(clientLocalsCount))
        return;

    if (!send)
    {
        baseObject.clientLocals.clear();
        baseObject.clientLocals.resize(clientLocalsCount);
    }

    for (auto&& clientLocal : baseObject.clientLocals)
    {
        Field(clientLocal.internalIndex);
        Field(clientLocal.variableType);

        if (clientLocal.variableType == mwmp::VARIABLE_TYPE::SHORT || clientLocal.variableType == mwmp::VARIABLE_TYPE::LONG)
            Field(clientLocal.intValue);
        else if (clientLocal.variableType == mwmp::VARIABLE_TYPE::FLOAT)
            Field(clientLocal.floatValue);
    }
}
