#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketObjectRestock.hpp"

using namespace mwmp;

PacketObjectRestock::PacketObjectRestock() : ObjectPacket()
{
    packetID = ID_OBJECT_RESTOCK;
    hasCellData = true;
}
