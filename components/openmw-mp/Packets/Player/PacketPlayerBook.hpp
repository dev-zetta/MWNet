#ifndef OPENMW_PACKETPLAYERBOOK_HPP
#define OPENMW_PACKETPLAYERBOOK_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerBook : public PlayerPacket
    {
    public:
        PacketPlayerBook();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETPLAYERBOOK_HPP
