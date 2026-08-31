#include "PacketPlayerInput.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>

using namespace mwmp;

PacketPlayerInput::PacketPlayerInput() : PlayerPacket()
{
    packetID = ID_PLAYER_INPUT;
}

void PacketPlayerInput::Packet(bool send)
{
    PlayerPacket::Packet(send);

    // Placeholder
}
