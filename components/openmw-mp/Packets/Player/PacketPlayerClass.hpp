#ifndef OPENMW_PACKETPLAYERCLASS_HPP
#define OPENMW_PACKETPLAYERCLASS_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerClass : public PlayerPacket
    {
    public:
        PacketPlayerClass();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETPLAYERCLASS_HPP
