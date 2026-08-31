#include <components/openmw-mp/NetworkMessages.hpp>
#include "ObjectPacket.hpp"

using namespace mwmp;

ObjectPacket::ObjectPacket() : BasePacket()
{
    hasCellData = false;
    packetID = 0;
}

ObjectPacket::~ObjectPacket()
{

}

void ObjectPacket::setObjectList(BaseObjectList *newObjectList)
{
    objectList = newObjectList;
    guid = objectList->guid;
}

void ObjectPacket::Packet(bool send)
{
    if (!PacketHeader(send))
        return;

    BaseObject baseObject;
    for (unsigned int i = 0; i < objectList->baseObjectCount; i++)
    {
        if (send)
            baseObject = objectList->baseObjects.at(i);

        Object(baseObject, send);

        if (!send)
            objectList->baseObjects.push_back(baseObject);
    }
}

bool ObjectPacket::PacketHeader(bool send)
{
    BasePacket::Packet(send);
    if (!packetValid || objectList == nullptr)
    {
        invalidate(protocol::CodecError::InvalidValue);
        return false;
    }

    Field(objectList->packetOrigin);

    if (objectList->packetOrigin == mwmp::CLIENT_SCRIPT_LOCAL || objectList->packetOrigin == mwmp::CLIENT_SCRIPT_GLOBAL)
        Field(objectList->originClientScript, true);

    if (send)
        objectList->baseObjectCount = (unsigned int)(objectList->baseObjects.size());
    else
        objectList->baseObjects.clear();

    if (!CollectionSize(objectList->baseObjectCount, protocol::limits::objectChanges))
    {
        objectList->isValid = false;
        return false;
    }

    if (hasCellData)
    {
        Field(objectList->cell.mData, true);
        Field(objectList->cell.mName, true);
    }

    return true;
}

void ObjectPacket::Object(BaseObject &baseObject, bool send)
{
    Field(baseObject.refId, true);
    Field(baseObject.refNum);
    Field(baseObject.mpNum);
}
