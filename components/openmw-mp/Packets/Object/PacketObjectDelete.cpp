#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketObjectDelete.hpp"

using namespace mwmp;

PacketObjectDelete::PacketObjectDelete() : ObjectPacket()
{
    packetID = ID_OBJECT_DELETE;
    hasCellData = true;
}
