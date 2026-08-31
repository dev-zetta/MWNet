#include <components/openmw-mp/NetworkMessages.hpp>
#include "PacketSystemHandshake.hpp"

using namespace mwmp;

PacketSystemHandshake::PacketSystemHandshake() : SystemPacket()
{
    packetID = ID_SYSTEM_HANDSHAKE;
}

void PacketSystemHandshake::Packet(bool send)
{
    SystemPacket::Packet(send);

    if (!Field(system->playerName, true, maxNameLength) ||
        !Field(system->serverPassword, true, maxPasswordLength))
    {
        packetValid = false;
        return;
    }
}
