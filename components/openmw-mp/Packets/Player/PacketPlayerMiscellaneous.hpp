#ifndef OPENMW_PACKETPLAYERMISCELLANEOUS_HPP
#define OPENMW_PACKETPLAYERMISCELLANEOUS_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerMiscellaneous : public PlayerPacket
    {
    public:
        PacketPlayerMiscellaneous();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETPLAYERMISCELLANEOUS_HPP
