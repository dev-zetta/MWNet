#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketObjectActivate.hpp"

using namespace mwmp;

PacketObjectActivate::PacketObjectActivate() : ObjectPacket()
{
    packetID = ID_OBJECT_ACTIVATE;
    hasCellData = true;
}

void PacketObjectActivate::Packet(bool send)
{
    if (!PacketHeader(send))
        return;

    BaseObject baseObject;
    for (unsigned int i = 0; i < objectList->baseObjectCount; i++)
    {
        if (send)
            baseObject = objectList->baseObjects.at(i);

        Field(baseObject.isPlayer);

        if (baseObject.isPlayer)
            Field(baseObject.guid);
        else
            Object(baseObject, send);

        Field(baseObject.activatingActor.isPlayer);

        if (baseObject.activatingActor.isPlayer)
        {
            Field(baseObject.activatingActor.guid);
        }
        else
        {
            Field(baseObject.activatingActor.refId, true);
            Field(baseObject.activatingActor.refNum);
            Field(baseObject.activatingActor.mpNum);

            Field(baseObject.activatingActor.name);
        }

        if (!send)
            objectList->baseObjects.push_back(baseObject);
    }
}
