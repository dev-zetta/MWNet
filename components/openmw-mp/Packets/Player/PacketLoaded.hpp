#ifndef OPENMW_PACKETLOADED_HPP
#define OPENMW_PACKETLOADED_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketLoaded : public PlayerPacket
    {
    public:
        PacketLoaded() : PlayerPacket()
        {
            packetID = ID_LOADED;
            orderChannel = CHANNEL_SYSTEM;
        }
    };
}

#endif //OPENMW_PACKETLOADED_HPP
