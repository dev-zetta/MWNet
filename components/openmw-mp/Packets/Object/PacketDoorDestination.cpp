#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketDoorDestination.hpp"

using namespace mwmp;

PacketDoorDestination::PacketDoorDestination() : ObjectPacket()
{
    packetID = ID_DOOR_DESTINATION;
    hasCellData = true;
}

void PacketDoorDestination::Object(BaseObject &baseObject, bool send)
{
    ObjectPacket::Object(baseObject, send);

    Field(baseObject.teleportState);

    if (baseObject.teleportState)
    {
        Field(baseObject.destinationCell.mData, true);
        Field(baseObject.destinationCell.mName, true);

        Field(baseObject.destinationPosition.pos, true);
        Field(baseObject.destinationPosition.rot[0], true);
        Field(baseObject.destinationPosition.rot[2], true);
    }
}
