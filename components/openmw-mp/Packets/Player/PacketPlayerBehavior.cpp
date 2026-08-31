#include "PacketPlayerBehavior.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerBehavior::PacketPlayerBehavior() : PlayerPacket()
{
    packetID = ID_PLAYER_BEHAVIOR;
}

void PacketPlayerBehavior::Packet(RakNet::BitStream *newBitstream, bool send)
{
    PlayerPacket::Packet(newBitstream, send);

    // Placeholder
}
