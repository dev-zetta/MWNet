#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketObjectSound.hpp"

using namespace mwmp;

PacketObjectSound::PacketObjectSound() : ObjectPacket()
{
    packetID = ID_OBJECT_SOUND;
    hasCellData = true;
}

void PacketObjectSound::Packet(bool send)
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

        Field(baseObject.soundId, true);
        Field(baseObject.volume);
        Field(baseObject.pitch);

        if (!send)
            objectList->baseObjects.push_back(baseObject);
    }
}
