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

    if (!RW(system->playerName, send, true, maxNameLength) ||
        !RW(system->serverPassword, send, true, maxPasswordLength))
    {
        packetValid = false;
        return;
    }
}
