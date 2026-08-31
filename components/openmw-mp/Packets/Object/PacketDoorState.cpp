#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketDoorState.hpp"

using namespace mwmp;

PacketDoorState::PacketDoorState() : ObjectPacket()
{
    packetID = ID_DOOR_STATE;
    hasCellData = true;
}

void PacketDoorState::Object(BaseObject &baseObject, bool send)
{
    ObjectPacket::Object(baseObject, send);
    RW(baseObject.doorState, send);
}
