#ifndef OPENMW_PACKETSYSTEMHANDSHAKE_HPP
#define OPENMW_PACKETSYSTEMHANDSHAKE_HPP

#include <components/openmw-mp/Packets/System/SystemPacket.hpp>

namespace mwmp
{
    class PacketSystemHandshake : public SystemPacket
    {
    public:
        PacketSystemHandshake();

        virtual void Packet(bool send);

        static inline constexpr uint32_t maxNameLength = protocol::limits::playerNameBytes;
        static inline constexpr uint32_t maxPasswordLength = protocol::limits::passwordBytes;
    };
}

#endif //OPENMW_PACKETSYSTEMHANDSHAKE_HPP
