#ifndef OPENMW_PACKETPLAYERSKILL_HPP
#define OPENMW_PACKETPLAYERSKILL_HPP

#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>

namespace mwmp
{
    class PacketPlayerSkill : public PlayerPacket
    {
    public:
        static inline constexpr int SkillCount = 27;
        static inline constexpr int AttributeCount = 8;
        PacketPlayerSkill();

        virtual void Packet(bool send);
    };
}

#endif //OPENMW_PACKETPLAYERSKILL_HPP
