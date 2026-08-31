#include "PacketPlayerRest.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerRest::PacketPlayerRest() : PlayerPacket()
{
    packetID = ID_PLAYER_REST;
}

void PacketPlayerRest::Packet(bool send)
{
    PlayerPacket::Packet(send);

    // Placeholder to be filled in later
}
