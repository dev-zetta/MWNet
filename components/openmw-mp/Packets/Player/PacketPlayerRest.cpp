#include "PacketPlayerRest.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerRest::PacketPlayerRest() : PlayerPacket()
{
    packetID = ID_PLAYER_REST;
}

void PacketPlayerRest::Packet(RakNet::BitStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    // Placeholder to be filled in later
}
