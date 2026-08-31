#ifndef OPENMW_PACKETPLAYERREST_HPP
#define OPENMW_PACKETPLAYERREST_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerRest : public PlayerPacket
    {
    public:
        PacketPlayerRest();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETPLAYERREST_HPP
