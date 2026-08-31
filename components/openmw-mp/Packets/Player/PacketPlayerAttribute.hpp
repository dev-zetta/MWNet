#ifndef OPENMW_PACKETPLAYERATTRIBUTE_HPP
#define OPENMW_PACKETPLAYERATTRIBUTE_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerAttribute : public PlayerPacket
    {
    public:
        static inline constexpr int AttributeCount = 8;
        PacketPlayerAttribute();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETPLAYERATTRIBUTE_HPP
