#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketVideoPlay.hpp"

using namespace mwmp;

PacketVideoPlay::PacketVideoPlay() : ObjectPacket()
{
    packetID = ID_VIDEO_PLAY;
}

void PacketVideoPlay::Object(BaseObject &baseObject, bool send)
{
    Field(baseObject.videoFilename, true);
    Field(baseObject.allowSkipping);
}
